#include "someip_service.h"
#include <unordered_map>
#include <mutex>
#include <atomic>
#include <thread>

namespace cgw_diag {

class SomeIpServiceImpl : public SomeIpService {
public:
    SomeIpServiceImpl() = default;
    ~SomeIpServiceImpl() override { stop(); }

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
    return std::make_unique<SomeIpServiceImpl>();
}

} // namespace cgw_diag
