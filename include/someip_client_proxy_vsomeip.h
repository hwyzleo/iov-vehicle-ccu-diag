#pragma once

#include "someip_client_proxy.h"
#include "someip_types.h"
#include <memory>
#include <mutex>
#include <atomic>
#include <condition_variable>
#include <thread>

// 根据编译选项选择 SOME/IP 实现
#ifdef USE_MOCK_SOMEIP
#include "mock_someip.h"
namespace someip = mock_someip;
#else
#include <vsomeip/vsomeip.hpp>
namespace someip = vsomeip;
#endif

namespace ccu_diag {

class SomeIpClientProxyVsomeip : public SomeIpClientProxy {
public:
    SomeIpClientProxyVsomeip();
    ~SomeIpClientProxyVsomeip() override;

    bool initialize(const SomeIpServiceConfig& remote_service_config) override;
    bool connect() override;
    void disconnect() override;
    bool isConnected() const override;

    SomeIpMessage sendRequest(const SomeIpMessage& request) override;
    bool sendNotification(const SomeIpMessage& notification) override;

    void setRequestTimeout(uint32_t timeout_ms) override;
    uint32_t getRequestTimeout() const override;

private:
    // vsomeip 组件
    std::shared_ptr<someip::Runtime> runtime_;
    std::shared_ptr<someip::Application> application_;
    
    // 配置
    SomeIpServiceConfig remote_config_;
    
    // 状态管理
    std::atomic<bool> initialized_{false};
    std::atomic<bool> connected_{false};
    std::atomic<bool> running_{false};
    std::thread io_thread_;
    
    // 同步机制
    std::mutex mutex_;
    std::condition_variable cv_;
    bool response_received_ = false;
    SomeIpMessage response_data_;
    
    // 请求超时
    uint32_t request_timeout_ms_ = 2000;
    
    // 会话管理
    std::atomic<uint16_t> session_id_{0};
    
    // 初始化方法
    void initializeApplication();
    void registerServiceDiscovery();
    void onServiceAvailability(someip::service_t service, someip::instance_t instance, bool available);
    void onMessage(const std::shared_ptr<someip::Message>& response);
    
    // SOME/IP 消息转换
    std::shared_ptr<someip::Request> createSomeIpRequest(const SomeIpMessage& request);
    SomeIpMessage convertResponse(const std::shared_ptr<someip::Message>& response);
    
    // 日志
    void logInfo(const std::string& message);
    void logError(const std::string& message);
    void logWarning(const std::string& message);
};

} // namespace ccu_diag
