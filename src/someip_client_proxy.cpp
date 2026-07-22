#include "someip_client_proxy.h"
#include "someip_client_proxy_vsomeip.h"
#include <mutex>
#include <atomic>
#include <condition_variable>
#include <chrono>
#include <vector>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <poll.h>
#include <cstring>

namespace {

// Pseudo SOME/IP binary frame header (20 bytes: 16B core + 4B payload_length), big-endian
struct PseudoSomeIpHeader {
    uint16_t service_id;
    uint16_t method_id;
    uint8_t message_type;
    uint8_t return_code;
    uint16_t client_id;
    uint16_t session_id;
    uint8_t reserved[6];
    uint32_t payload_length;
};

static_assert(sizeof(PseudoSomeIpHeader) == 20, "PseudoSomeIpHeader must be 20 bytes");

inline void writeU16BE(uint8_t* p, uint16_t v) {
    p[0] = static_cast<uint8_t>(v >> 8);
    p[1] = static_cast<uint8_t>(v & 0xFF);
}

inline void writeU32BE(uint8_t* p, uint32_t v) {
    p[0] = static_cast<uint8_t>(v >> 24);
    p[1] = static_cast<uint8_t>((v >> 16) & 0xFF);
    p[2] = static_cast<uint8_t>((v >> 8) & 0xFF);
    p[3] = static_cast<uint8_t>(v & 0xFF);
}

inline uint16_t readU16BE(const uint8_t* p) {
    return static_cast<uint16_t>((static_cast<uint16_t>(p[0]) << 8) | p[1]);
}

inline uint32_t readU32BE(const uint8_t* p) {
    return (static_cast<uint32_t>(p[0]) << 24) |
           (static_cast<uint32_t>(p[1]) << 16) |
           (static_cast<uint32_t>(p[2]) << 8)  |
           static_cast<uint32_t>(p[3]);
}

inline void serializeHeader(uint8_t buf[20], const PseudoSomeIpHeader& h) {
    writeU16BE(buf + 0, h.service_id);
    writeU16BE(buf + 2, h.method_id);
    buf[4] = h.message_type;
    buf[5] = h.return_code;
    writeU16BE(buf + 6, h.client_id);
    writeU16BE(buf + 8, h.session_id);
    std::memset(buf + 10, 0, 6);
    writeU32BE(buf + 16, h.payload_length);
}

inline bool deserializeHeader(const uint8_t buf[20], PseudoSomeIpHeader& h) {
    h.service_id = readU16BE(buf + 0);
    h.method_id = readU16BE(buf + 2);
    h.message_type = buf[4];
    h.return_code = buf[5];
    h.client_id = readU16BE(buf + 6);
    h.session_id = readU16BE(buf + 8);
    // reserved ignored on read
    h.payload_length = readU32BE(buf + 16);
    return true;
}

inline bool readExact(int fd, void* buf, size_t n, int timeoutMs) {
    size_t left = n;
    auto* p = static_cast<uint8_t*>(buf);
    while (left > 0) {
        pollfd pfd{fd, POLLIN, 0};
        int pr = ::poll(&pfd, 1, timeoutMs);
        if (pr == 0) return false; // timeout
        if (pr < 0) {
            if (errno == EINTR) continue;
            return false;
        }
        ssize_t r = ::recv(fd, p, left, 0);
        if (r <= 0) return false;
        p += r;
        left -= static_cast<size_t>(r);
    }
    return true;
}

inline bool writeAll(int fd, const void* buf, size_t n) {
    const auto* p = static_cast<const uint8_t*>(buf);
    size_t left = n;
    while (left > 0) {
        ssize_t w = ::send(fd, p, left, 0);
        if (w <= 0) {
            if (errno == EINTR) continue;
            return false;
        }
        p += w;
        left -= static_cast<size_t>(w);
    }
    return true;
}

} // namespace

namespace cgw_diag {

#ifdef USE_MOCK_SOMEIP
// Mock 实现
class SomeIpClientProxyMock : public SomeIpClientProxy {
public:
    SomeIpClientProxyMock() = default;
    ~SomeIpClientProxyMock() override { disconnect(); }

    bool initialize(const SomeIpServiceConfig& remote_service_config) override {
        if (connected_) {
            return false;
        }
        remote_config_ = remote_service_config;
        initialized_ = true;
        return true;
    }

    bool connect() override {
        if (!initialized_ || connected_) {
            return false;
        }

        fd_ = ::socket(AF_INET, SOCK_STREAM, 0);
        if (fd_ < 0) {
            return false;
        }

        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(remote_config_.port);
        if (::inet_pton(AF_INET, remote_config_.ip_address.c_str(), &addr.sin_addr) != 1) {
            ::close(fd_);
            fd_ = -1;
            return false;
        }

        if (::connect(fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
            ::close(fd_);
            fd_ = -1;
            return false;
        }

        connected_ = true;
        return true;
    }

    void disconnect() override {
        connected_ = false;
        if (fd_ >= 0) {
            ::close(fd_);
            fd_ = -1;
        }
    }

    bool isConnected() const override {
        return connected_;
    }

    SomeIpMessage sendRequest(const SomeIpMessage& request) override {
        SomeIpMessage response;
        response.header.return_code = ReturnCode::E_NOT_OK;

        if (!connected_ || fd_ < 0) {
            return response;
        }

        uint16_t sid = ++session_counter_;

        PseudoSomeIpHeader reqHdr{};
        reqHdr.service_id = request.header.service_id;
        reqHdr.method_id = request.header.method_id;
        reqHdr.message_type = static_cast<uint8_t>(request.header.message_type);
        reqHdr.return_code = static_cast<uint8_t>(request.header.return_code);
        reqHdr.client_id = request.header.client_id;
        reqHdr.session_id = sid;
        reqHdr.payload_length = static_cast<uint32_t>(request.payload.size());

        uint8_t hdrBuf[20];
        serializeHeader(hdrBuf, reqHdr);
        if (!writeAll(fd_, hdrBuf, 20)) {
            disconnect();
            return response;
        }
        if (!request.payload.empty()) {
            if (!writeAll(fd_, request.payload.data(), request.payload.size())) {
                disconnect();
                return response;
            }
        }

        // Wait for response
        uint8_t respHdrBuf[20];
        if (!readExact(fd_, respHdrBuf, 20, static_cast<int>(request_timeout_ms_))) {
            disconnect();
            return response;
        }

        PseudoSomeIpHeader respHdr{};
        deserializeHeader(respHdrBuf, respHdr);

        if (respHdr.session_id != sid) {
            // Session mismatch
            response.header.return_code = ReturnCode::E_NOT_OK;
            return response;
        }

        std::vector<uint8_t> payload;
        if (respHdr.payload_length > 0) {
            if (respHdr.payload_length > 10 * 1024 * 1024) {
                disconnect();
                return response;
            }
            payload.resize(respHdr.payload_length);
            if (!readExact(fd_, payload.data(), respHdr.payload_length, static_cast<int>(request_timeout_ms_))) {
                disconnect();
                return response;
            }
        }

        response.header.service_id = respHdr.service_id;
        response.header.method_id = respHdr.method_id;
        response.header.message_type = static_cast<MessageType>(respHdr.message_type);
        response.header.return_code = static_cast<ReturnCode>(respHdr.return_code);
        response.header.client_id = respHdr.client_id;
        response.header.session_id = respHdr.session_id;
        response.payload = std::move(payload);

        return response;
    }

    bool sendNotification(const SomeIpMessage& notification) override {
        (void)notification;
        if (!connected_) {
            return false;
        }
        // In real implementation, this would send notification via SOME/IP stack
        return true;
    }

    void setRequestTimeout(uint32_t timeout_ms) override {
        request_timeout_ms_ = timeout_ms;
    }

    uint32_t getRequestTimeout() const override {
        return request_timeout_ms_;
    }

private:
    SomeIpServiceConfig remote_config_;
    std::atomic<bool> initialized_{false};
    std::atomic<bool> connected_{false};
    uint32_t request_timeout_ms_ = 2000;
    int fd_{-1};
    uint16_t session_counter_{0};
};

std::unique_ptr<SomeIpClientProxy> createSomeIpClientProxy() {
    return std::make_unique<SomeIpClientProxyMock>();
}

#else
// vsomeip 实现
std::unique_ptr<SomeIpClientProxy> createSomeIpClientProxy() {
    return std::make_unique<SomeIpClientProxyVsomeip>();
}
#endif

} // namespace cgw_diag