#include "someip_client_proxy_vsomeip.h"
#include "constants.h"
#include <iostream>
#include <sstream>
#include <chrono>

namespace ccu_diag {

SomeIpClientProxyVsomeip::SomeIpClientProxyVsomeip() {
    logInfo("SomeIpClientProxyVsomeip created");
}

SomeIpClientProxyVsomeip::~SomeIpClientProxyVsomeip() {
    disconnect();
    logInfo("SomeIpClientProxyVsomeip destroyed");
}

bool SomeIpClientProxyVsomeip::initialize(const SomeIpServiceConfig& remote_service_config) {
    if (initialized_) {
        logWarning("Already initialized");
        return false;
    }
    
    remote_config_ = remote_service_config;
    
    try {
        initializeApplication();
        initialized_ = true;
        logInfo("Initialized successfully");
        return true;
    } catch (const std::exception& e) {
        logError("Initialization failed: " + std::string(e.what()));
        return false;
    }
}

bool SomeIpClientProxyVsomeip::connect() {
    if (!initialized_) {
        logError("Not initialized");
        return false;
    }
    
    if (connected_) {
        logWarning("Already connected");
        return true;
    }
    
    try {
        // 启动应用
        if (!application_->start()) {
            logError("Failed to start application");
            return false;
        }
        
        running_ = true;
        
        // 启动 IO 线程
        io_thread_ = std::thread([this]() {
            while (running_) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
        });
        
        // 注册服务发现
        registerServiceDiscovery();
        
        // 请求服务
        application_->request_service(remote_config_.service_id, remote_config_.instance_id);
        
        logInfo("Connected to service");
        connected_ = true;
        return true;
        
    } catch (const std::exception& e) {
        logError("Connection failed: " + std::string(e.what()));
        return false;
    }
}

void SomeIpClientProxyVsomeip::disconnect() {
    if (!connected_) {
        return;
    }
    
    running_ = false;
    connected_ = false;
    
    if (io_thread_.joinable()) {
        io_thread_.join();
    }
    
    if (application_) {
        application_->stop();
    }
    
    logInfo("Disconnected");
}

bool SomeIpClientProxyVsomeip::isConnected() const {
    return connected_;
}

SomeIpMessage SomeIpClientProxyVsomeip::sendRequest(const SomeIpMessage& request) {
    SomeIpMessage response;
    
    if (!connected_) {
        logError("Not connected");
        response.header.return_code = ReturnCode::E_NOT_OK;
        return response;
    }
    
    try {
        // 创建 SOME/IP 请求
        auto someip_request = createSomeIpRequest(request);
        
        // 重置响应状态
        {
            std::lock_guard<std::mutex> lock(mutex_);
            response_received_ = false;
        }
        
        // 发送请求
        application_->send(someip_request);
        
        // 等待响应
        std::unique_lock<std::mutex> lock(mutex_);
        if (!cv_.wait_for(lock, std::chrono::milliseconds(request_timeout_ms_),
                          [this]() { return response_received_; })) {
            logError("Request timeout");
            response.header.return_code = ReturnCode::E_TIMEOUT;
            return response;
        }
        
        // 返回响应
        response = response_data_;
        
    } catch (const std::exception& e) {
        logError("Send request failed: " + std::string(e.what()));
        response.header.return_code = ReturnCode::E_NOT_OK;
    }
    
    return response;
}

bool SomeIpClientProxyVsomeip::sendNotification(const SomeIpMessage& notification) {
    if (!connected_) {
        logError("Not connected");
        return false;
    }
    
    try {
        // 创建 SOME/IP 消息
        auto someip_message = runtime_->create_request();
        someip_message->set_service(notification.header.service_id);
        someip_message->set_instance(remote_config_.instance_id);
        someip_message->set_method(notification.header.method_id);
        someip_message->set_message_type(someip::message_type_e::NOTIFICATION);
        
        // 设置负载
        auto payload = runtime_->create_payload();
        payload->set_data(notification.payload);
        someip_message->set_payload(payload);
        
        // 发送通知
        application_->send(someip_message);
        
        logInfo("Notification sent");
        return true;
        
    } catch (const std::exception& e) {
        logError("Send notification failed: " + std::string(e.what()));
        return false;
    }
}

void SomeIpClientProxyVsomeip::setRequestTimeout(uint32_t timeout_ms) {
    request_timeout_ms_ = timeout_ms;
}

uint32_t SomeIpClientProxyVsomeip::getRequestTimeout() const {
    return request_timeout_ms_;
}

void SomeIpClientProxyVsomeip::initializeApplication() {
    // client proxy 与 service 必须复用同一进程 application（同一个已登记名），
    // 否则第二个名字未在 vsomeip.json 登记，会落到默认 routing 域外。
    std::string app_name = remote_config_.application_name;
    if (app_name.empty()) {
        app_name = constants::DEFAULT_SOMEIP_APPLICATION_NAME;
    }

    application_ = acquireProcessApplication(app_name);
    if (!application_) {
        throw std::runtime_error("failed to acquire process vsomeip application: " + app_name);
    }

    // runtime_ 供 create_request/create_payload 使用：与 application 同一 Runtime。
    runtime_ = someip::Runtime::get();

    logInfo("Application acquired: " + app_name);
}

void SomeIpClientProxyVsomeip::registerServiceDiscovery() {
    // 注册服务可用性处理器
    application_->register_availability_handler(
        remote_config_.service_id,
        remote_config_.instance_id,
        [this](someip::service_t service, someip::instance_t instance, bool available) {
            onServiceAvailability(service, instance, available);
        });
    
    logInfo("Service discovery registered");
}

void SomeIpClientProxyVsomeip::onServiceAvailability(
    someip::service_t service, someip::instance_t instance, bool available) {
    
    if (available) {
        std::ostringstream oss;
        oss << "Service available: 0x" << std::hex << service 
            << " instance: 0x" << instance;
        logInfo(oss.str());
        
        // 注册消息处理器
        application_->register_message_handler(
            service, instance, someip::ANY_METHOD,
            [this](const std::shared_ptr<someip::Message>& message) {
                onMessage(message);
            });
        
        connected_ = true;
    } else {
        std::ostringstream oss2;
        oss2 << "Service unavailable: 0x" << std::hex << service 
             << " instance: 0x" << instance;
        logWarning(oss2.str());
        connected_ = false;
    }
}

void SomeIpClientProxyVsomeip::onMessage(const std::shared_ptr<someip::Message>& message) {
    // 转换响应
    response_data_ = convertResponse(message);
    
    // 通知等待的线程
    {
        std::lock_guard<std::mutex> lock(mutex_);
        response_received_ = true;
    }
    cv_.notify_one();
    
    logInfo("Response received");
}

std::shared_ptr<someip::Request> SomeIpClientProxyVsomeip::createSomeIpRequest(
    const SomeIpMessage& request) {
    
    auto someip_request = runtime_->create_request();
    someip_request->set_service(request.header.service_id);
    someip_request->set_instance(remote_config_.instance_id);
    someip_request->set_method(request.header.method_id);
    someip_request->set_session(session_id_++);
    someip_request->set_message_type(someip::message_type_e::REQUEST);
    
    // 设置负载
    auto payload = runtime_->create_payload();
    payload->set_data(request.payload);
    someip_request->set_payload(payload);
    
    return someip_request;
}

SomeIpMessage SomeIpClientProxyVsomeip::convertResponse(
    const std::shared_ptr<someip::Message>& response) {
    
    SomeIpMessage result;
    
    // 转换头部
    result.header.service_id = response->get_service();
    result.header.method_id = response->get_method();
    result.header.session_id = response->get_session();
    result.header.client_id = response->get_client();
    
    // 转换消息类型
    switch (response->get_message_type()) {
        case someip::message_type_e::RESPONSE:
            result.header.message_type = MessageType::RESPONSE;
            break;
        case someip::message_type_e::ERROR:
            result.header.message_type = MessageType::ERROR;
            break;
        default:
            result.header.message_type = MessageType::RESPONSE;
            break;
    }
    
    // 转换返回码
    switch (response->get_return_code()) {
        case someip::return_code_e::E_OK:
            result.header.return_code = ReturnCode::E_OK;
            break;
        case someip::return_code_e::E_NOT_OK:
            result.header.return_code = ReturnCode::E_NOT_OK;
            break;
        case someip::return_code_e::E_TIMEOUT:
            result.header.return_code = ReturnCode::E_TIMEOUT;
            break;
        case someip::return_code_e::E_NOT_REACHABLE:
            result.header.return_code = ReturnCode::E_NOT_REACHABLE;
            break;
        default:
            result.header.return_code = ReturnCode::E_NOT_OK;
            break;
    }
    
    // 转换负载
    auto payload = response->get_payload();
    if (payload) {
        result.payload.assign(payload->get_data(), 
                             payload->get_data() + payload->get_length());
    }
    
    return result;
}

void SomeIpClientProxyVsomeip::logInfo(const std::string& message) {
    std::cout << "[INFO] SomeIpClientProxyVsomeip: " << message << std::endl;
}

void SomeIpClientProxyVsomeip::logError(const std::string& message) {
    std::cerr << "[ERROR] SomeIpClientProxyVsomeip: " << message << std::endl;
}

void SomeIpClientProxyVsomeip::logWarning(const std::string& message) {
    std::cout << "[WARN] SomeIpClientProxyVsomeip: " << message << std::endl;
}

} // namespace ccu_diag
