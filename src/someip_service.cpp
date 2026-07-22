#include "someip_service.h"
#include "someip_service_vsomeip.h"
#include <unordered_map>
#include <mutex>
#include <atomic>
#include <thread>
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
        initialized_ = true;
        return true;
    }

    bool start() override {
        if (!initialized_ || running_) {
            return false;
        }
        running_ = true;
        return true;
    }

    void stop() override {
        if (running_) {
            stopOfferService();
            running_ = false;
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

private:
    SomeIpServiceConfig config_;
    std::atomic<bool> initialized_{false};
    std::atomic<bool> running_{false};
    std::atomic<bool> offering_{false};

    std::mutex handlers_mutex_;
    std::unordered_map<MethodId, SomeIpRequestHandler> method_handlers_;
    std::unordered_map<EventId, SomeIpNotificationHandler> event_handlers_;
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
