#include "someip_service.h"
#include "someip_service_vsomeip.h"
#include <unordered_map>
#include <mutex>
#include <atomic>
#include <thread>
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
class SomeIpServiceMock : public SomeIpService {
public:
    SomeIpServiceMock() = default;
    ~SomeIpServiceMock() override { stop(); }

    bool initialize(const SomeIpServiceConfig& config) override {
        if (running_) {
            return false;
        }
        config_ = config;

        listen_fd_ = ::socket(AF_INET, SOCK_STREAM, 0);
        if (listen_fd_ < 0) {
            std::cerr << "[ERROR] MockSomeIP socket() failed: " << std::strerror(errno) << "\n";
            return false;
        }

        int opt = 1;
        ::setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(config_.port);
        if (::inet_pton(AF_INET, config_.ip_address.c_str(), &addr.sin_addr) != 1) {
            std::cerr << "[ERROR] MockSomeIP inet_pton() failed for " << config_.ip_address << "\n";
            ::close(listen_fd_);
            listen_fd_ = -1;
            return false;
        }

        if (::bind(listen_fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
            std::cerr << "[ERROR] MockSomeIP bind() failed: " << std::strerror(errno) << "\n";
            ::close(listen_fd_);
            listen_fd_ = -1;
            return false;
        }

        if (::listen(listen_fd_, 1) < 0) {
            std::cerr << "[ERROR] MockSomeIP listen() failed: " << std::strerror(errno) << "\n";
            ::close(listen_fd_);
            listen_fd_ = -1;
            return false;
        }

        bound_address_ = config_.ip_address;
        bound_port_ = config_.port;
        initialized_ = true;
        return true;
    }

    bool start() override {
        if (!initialized_ || running_) {
            return false;
        }
        stop_requested_ = false;
        running_ = true;

        listener_thread_ = std::thread([this]() {
            std::cout << "[INFO] MockSomeIP listening on " << bound_address_ << ":" << bound_port_ << "\n";
            while (!stop_requested_) {
                pollfd pfd{listen_fd_, POLLIN, 0};
                int pr = ::poll(&pfd, 1, 200); // 200ms poll interval
                if (pr == 0) continue;
                if (pr < 0) {
                    if (errno == EINTR) continue;
                    break;
                }

                sockaddr_in cli{};
                socklen_t cliLen = sizeof(cli);
                int fd = ::accept(listen_fd_, reinterpret_cast<sockaddr*>(&cli), &cliLen);
                if (fd < 0) {
                    if (errno == EINTR) continue;
                    std::cerr << "[WARN] MockSomeIP accept() failed: " << std::strerror(errno) << "\n";
                    continue;
                }

                // Single connection model
                if (client_fd_ >= 0) {
                    ::close(fd);
                    continue;
                }
                client_fd_ = fd;
                std::cout << "[INFO] MockSomeIP client connected\n";

                // Simple synchronous request processing loop
                while (!stop_requested_) {
                    uint8_t hdrBuf[20];
                    if (!readExact(client_fd_, hdrBuf, 20, 2000)) {
                        break; // timeout or error
                    }

                    PseudoSomeIpHeader hdr{};
                    deserializeHeader(hdrBuf, hdr);

                    std::vector<uint8_t> payload;
                    if (hdr.payload_length > 0) {
                        if (hdr.payload_length > 10 * 1024 * 1024) { // 10MB sanity limit
                            break;
                        }
                        payload.resize(hdr.payload_length);
                        if (!readExact(client_fd_, payload.data(), hdr.payload_length, 5000)) {
                            break;
                        }
                    }

                    std::cout << "[INFO] MockSomeIP recv service=0x" << std::hex << hdr.service_id
                              << " method=0x" << hdr.method_id << std::dec
                              << " session=" << hdr.session_id << "\n";

                    // Map to SomeIpMessage
                    SomeIpMessage request;
                    request.header.service_id = hdr.service_id;
                    request.header.method_id = hdr.method_id;
                    request.header.client_id = hdr.client_id;
                    request.header.session_id = hdr.session_id;
                    request.header.message_type = static_cast<MessageType>(hdr.message_type);
                    request.payload = std::move(payload);

                    // Find handler
                    SomeIpRequestHandler handler;
                    {
                        std::lock_guard<std::mutex> lock(handlers_mutex_);
                        auto it = method_handlers_.find(request.header.method_id);
                        if (it != method_handlers_.end()) {
                            handler = it->second;
                        }
                    }

                    SomeIpMessage response;
                    if (handler) {
                        response = handler(request);
                    } else {
                        response.header.service_id = request.header.service_id;
                        response.header.method_id = request.header.method_id;
                        response.header.message_type = MessageType::RESPONSE;
                        response.header.return_code = ReturnCode::E_NOT_OK;
                    }

                    // Serialize response frame
                    PseudoSomeIpHeader respHdr{};
                    respHdr.service_id = response.header.service_id;
                    respHdr.method_id = response.header.method_id;
                    respHdr.message_type = static_cast<uint8_t>(response.header.message_type);
                    respHdr.return_code = static_cast<uint8_t>(response.header.return_code);
                    respHdr.client_id = response.header.client_id;
                    respHdr.session_id = hdr.session_id; // echo session
                    respHdr.payload_length = static_cast<uint32_t>(response.payload.size());

                    uint8_t respBuf[20];
                    serializeHeader(respBuf, respHdr);
                    if (!writeAll(client_fd_, respBuf, 20)) break;
                    if (!response.payload.empty()) {
                        if (!writeAll(client_fd_, response.payload.data(), response.payload.size())) break;
                    }
                }

                std::cout << "[INFO] MockSomeIP client disconnected\n";
                ::close(client_fd_);
                client_fd_ = -1;
            }
        });

        return true;
    }

    void stop() override {
        stop_requested_ = true;
        running_ = false;
        offering_ = false;

        if (listener_thread_.joinable()) {
            listener_thread_.join();
        }
        if (client_fd_ >= 0) {
            ::close(client_fd_);
            client_fd_ = -1;
        }
        if (listen_fd_ >= 0) {
            ::close(listen_fd_);
            listen_fd_ = -1;
        }
    }

    bool isRunning() const override {
        return running_;
    }

    void registerMethodHandler(MethodId method_id, SomeIpRequestHandler handler) override {
        std::lock_guard<std::mutex> lock(handlers_mutex_);
        method_handlers_[method_id] = std::move(handler);
    }

    void registerEventHandler(EventId event_id, SomeIpNotificationHandler handler) override {
        std::lock_guard<std::mutex> lock(handlers_mutex_);
        event_handlers_[event_id] = std::move(handler);
    }

    bool offerService() override {
        if (!running_ || !initialized_) {
            return false;
        }
        offering_ = true;
        return true;
    }

    void stopOfferService() override {
        offering_ = false;
    }

    ServiceId getServiceId() const override {
        return config_.service_id;
    }

    uint16_t getInstanceId() const override {
        return config_.instance_id;
    }

    uint16_t getBoundPort() const override {
        return bound_port_;
    }

private:
    SomeIpServiceConfig config_;
    std::atomic<bool> initialized_{false};
    std::atomic<bool> running_{false};
    std::atomic<bool> offering_{false};

    std::mutex handlers_mutex_;
    std::unordered_map<MethodId, SomeIpRequestHandler> method_handlers_;
    std::unordered_map<EventId, SomeIpNotificationHandler> event_handlers_;

    // TCP transport members
    int listen_fd_{-1};
    int client_fd_{-1};
    std::thread listener_thread_;
    std::atomic<bool> stop_requested_{false};
    std::string bound_address_;
    uint16_t bound_port_{0};
};

std::unique_ptr<SomeIpService> createSomeIpService() {
    return std::make_unique<SomeIpServiceMock>();
}

#else
// vsomeip 实现
std::unique_ptr<SomeIpService> createSomeIpService() {
    return std::make_unique<SomeIpServiceVsomeip>();
}
#endif

} // namespace cgw_diag
