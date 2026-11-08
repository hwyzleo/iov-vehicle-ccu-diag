#pragma once

#include "someip_service.h"
#include "someip_types.h"
#include "someip_backend.h"
#include <memory>
#include <mutex>
#include <atomic>
#include <thread>
#include <unordered_map>

// 后端选择（vsomeip / mock）与进程级 application 单例见 someip_backend.h。

namespace ccu_diag {

class SomeIpServiceVsomeip : public SomeIpService {
public:
    SomeIpServiceVsomeip();
    ~SomeIpServiceVsomeip() override;

    bool initialize(const SomeIpServiceConfig& config) override;
    bool start() override;
    void stop() override;
    bool isRunning() const override;

    void registerMethodHandler(MethodId method_id, SomeIpRequestHandler handler) override;
    void registerEventHandler(EventId event_id, SomeIpNotificationHandler handler) override;

    bool offerService() override;
    void stopOfferService() override;

    ServiceId getServiceId() const override;
    uint16_t getInstanceId() const override;
    uint16_t getBoundPort() const override;

private:
    // vsomeip 组件
    std::shared_ptr<someip::Runtime> runtime_;
    std::shared_ptr<someip::Application> application_;
    
    // 配置
    SomeIpServiceConfig config_;
    
    // 状态管理
    std::atomic<bool> initialized_{false};
    std::atomic<bool> running_{false};
    std::atomic<bool> offering_{false};
    std::thread io_thread_;
    
    // 处理器
    std::mutex handlers_mutex_;
    std::unordered_map<MethodId, SomeIpRequestHandler> method_handlers_;
    std::unordered_map<EventId, SomeIpNotificationHandler> event_handlers_;
    
    // 初始化方法
    void initializeApplication();
    void registerMethods();
    void onMessage(const std::shared_ptr<someip::Message>& request);
    
    // SOME/IP 消息转换
    SomeIpMessage convertRequest(const std::shared_ptr<someip::Message>& request);
    std::shared_ptr<someip::Message> createSomeIpResponse(
        const SomeIpMessage& response, const std::shared_ptr<someip::Message>& request);
    
    // 日志
    void logInfo(const std::string& message);
    void logError(const std::string& message);
    void logWarning(const std::string& message);
};

} // namespace ccu_diag
