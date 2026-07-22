#include "someip_client_proxy.h"
#include "someip_client_proxy_vsomeip.h"
#include <mutex>
#include <atomic>
#include <condition_variable>
#include <chrono>
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
        // In real implementation, this would establish connection to remote service
        connected_ = true;
        return true;
    }

    void disconnect() override {
        connected_ = false;
    }

    bool isConnected() const override {
        return connected_;
    }

    SomeIpMessage sendRequest(const SomeIpMessage& request) override {
        SomeIpMessage response;

        if (!connected_) {
            response.header.return_code = ReturnCode::E_NOT_OK;
            return response;
        }

        // In real implementation, this would send request via SOME/IP stack
        // and wait for response with timeout
        response.header.service_id = request.header.service_id;
        response.header.method_id = request.header.method_id;
        response.header.message_type = MessageType::RESPONSE;
        response.header.return_code = ReturnCode::E_OK;

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