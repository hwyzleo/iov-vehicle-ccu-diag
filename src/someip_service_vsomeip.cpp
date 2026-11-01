#include "someip_service_vsomeip.h"
#include <iostream>
#include <sstream>
#include <chrono>

namespace ccu_diag {

SomeIpServiceVsomeip::SomeIpServiceVsomeip() {
    logInfo("SomeIpServiceVsomeip created");
}

SomeIpServiceVsomeip::~SomeIpServiceVsomeip() {
    stop();
    logInfo("SomeIpServiceVsomeip destroyed");
}

bool SomeIpServiceVsomeip::initialize(const SomeIpServiceConfig& config) {
    if (initialized_) {
        logWarning("Already initialized");
        return false;
    }
    
    config_ = config;
    
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

bool SomeIpServiceVsomeip::start() {
    if (!initialized_) {
        logError("Not initialized");
        return false;
    }
    
    if (running_) {
        logWarning("Already running");
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
        
        logInfo("Service started");
        return true;
        
    } catch (const std::exception& e) {
        logError("Start failed: " + std::string(e.what()));
        return false;
    }
}

void SomeIpServiceVsomeip::stop() {
    if (!running_) {
        return;
    }
    
    running_ = false;
    offering_ = false;
    
    if (io_thread_.joinable()) {
        io_thread_.join();
    }
    
    if (application_) {
        application_->stop();
    }
    
    logInfo("Service stopped");
}

bool SomeIpServiceVsomeip::isRunning() const {
    return running_;
}

void SomeIpServiceVsomeip::registerMethodHandler(MethodId method_id, SomeIpRequestHandler handler) {
    std::lock_guard<std::mutex> lock(handlers_mutex_);
    method_handlers_[method_id] = std::move(handler);
    logInfo("Method handler registered: 0x" + std::to_string(method_id));
}

void SomeIpServiceVsomeip::registerEventHandler(EventId event_id, SomeIpNotificationHandler handler) {
    std::lock_guard<std::mutex> lock(handlers_mutex_);
    event_handlers_[event_id] = std::move(handler);
    logInfo("Event handler registered: 0x" + std::to_string(event_id));
}

bool SomeIpServiceVsomeip::offerService() {
    if (!running_ || !initialized_) {
        logError("Service not running or not initialized");
        return false;
    }
    
    if (offering_) {
        logWarning("Already offering service");
        return true;
    }
    
    try {
        // 注册方法处理器
        registerMethods();
        
        // 提供服务
        application_->offer_service(config_.service_id, config_.instance_id);
        
        offering_ = true;
        std::ostringstream oss;
        oss << "Service offered: 0x" << std::hex << config_.service_id 
            << " instance: 0x" << config_.instance_id;
        logInfo(oss.str());
        return true;
        
    } catch (const std::exception& e) {
        logError("Offer service failed: " + std::string(e.what()));
        return false;
    }
}

void SomeIpServiceVsomeip::stopOfferService() {
    if (!offering_) {
        return;
    }
    
    application_->stop_offer_service(config_.service_id, config_.instance_id);
    offering_ = false;
    
    logInfo("Service offer stopped");
}

ServiceId SomeIpServiceVsomeip::getServiceId() const {
    return config_.service_id;
}

uint16_t SomeIpServiceVsomeip::getInstanceId() const {
    return config_.instance_id;
}

void SomeIpServiceVsomeip::initializeApplication() {
    // 获取 Runtime
    runtime_ = someip::Runtime::get();
    
    // 创建 Application
    std::string app_name = "CcuDiagService_" + std::to_string(config_.service_id);
    application_ = runtime_->create_application(app_name);
    
    // 初始化应用
    application_->init();
    
    logInfo("Application initialized: " + app_name);
}

void SomeIpServiceVsomeip::registerMethods() {
    // 注册所有方法处理器
    std::lock_guard<std::mutex> lock(handlers_mutex_);
    for (const auto& pair : method_handlers_) {
        MethodId method_id = pair.first;
        application_->offer_method(config_.service_id, config_.instance_id, method_id);
        
        // 注册消息处理器
        application_->register_message_handler(
            config_.service_id, config_.instance_id, method_id,
            [this](const std::shared_ptr<someip::Message>& message) {
                onMessage(message);
            });
        
        logInfo("Method offered: 0x" + std::to_string(method_id));
    }
}

void SomeIpServiceVsomeip::onMessage(const std::shared_ptr<someip::Message>& request) {
    // 转换请求
    SomeIpMessage someip_request = convertRequest(request);
    
    // 查找处理器
    SomeIpRequestHandler handler;
    {
        std::lock_guard<std::mutex> lock(handlers_mutex_);
        auto it = method_handlers_.find(someip_request.header.method_id);
        if (it != method_handlers_.end()) {
            handler = it->second;
        }
    }
    
    if (handler) {
        // 调用处理器
        SomeIpMessage someip_response = handler(someip_request);
        
        // 创建响应
        auto response = createSomeIpResponse(someip_response, request);
        
        // 发送响应
        application_->send(response);
        
        logInfo("Request handled: 0x" + std::to_string(someip_request.header.method_id));
    } else {
        logWarning("No handler for method: 0x" + std::to_string(someip_request.header.method_id));
    }
}

SomeIpMessage SomeIpServiceVsomeip::convertRequest(const std::shared_ptr<someip::Message>& request) {
    SomeIpMessage result;
    
    // 转换头部
    result.header.service_id = request->get_service();
    result.header.method_id = request->get_method();
    result.header.session_id = request->get_session();
    result.header.client_id = request->get_client();
    
    // 转换消息类型
    switch (request->get_message_type()) {
        case someip::message_type_e::REQUEST:
            result.header.message_type = MessageType::REQUEST;
            break;
        case someip::message_type_e::REQUEST_NO_RETURN:
            result.header.message_type = MessageType::REQUEST_NO_RETURN;
            break;
        default:
            result.header.message_type = MessageType::REQUEST;
            break;
    }
    
    // 转换负载
    auto payload = request->get_payload();
    if (payload) {
        result.payload.assign(payload->get_data(), 
                             payload->get_data() + payload->get_length());
    }
    
    return result;
}

std::shared_ptr<someip::Message> SomeIpServiceVsomeip::createSomeIpResponse(
    const SomeIpMessage& response, const std::shared_ptr<someip::Message>& request) {
    
    auto someip_response = runtime_->create_response(request);
    someip_response->set_service(response.header.service_id);
    someip_response->set_method(response.header.method_id);
    
    // 转换消息类型
    switch (response.header.message_type) {
        case MessageType::RESPONSE:
            someip_response->set_message_type(someip::message_type_e::RESPONSE);
            break;
        case MessageType::ERROR:
            someip_response->set_message_type(someip::message_type_e::ERROR);
            break;
        default:
            someip_response->set_message_type(someip::message_type_e::RESPONSE);
            break;
    }
    
    // 转换返回码
    switch (response.header.return_code) {
        case ReturnCode::E_OK:
            someip_response->set_return_code(someip::return_code_e::E_OK);
            break;
        case ReturnCode::E_NOT_OK:
            someip_response->set_return_code(someip::return_code_e::E_NOT_OK);
            break;
        case ReturnCode::E_TIMEOUT:
            someip_response->set_return_code(someip::return_code_e::E_TIMEOUT);
            break;
        case ReturnCode::E_NOT_REACHABLE:
            someip_response->set_return_code(someip::return_code_e::E_NOT_REACHABLE);
            break;
        default:
            someip_response->set_return_code(someip::return_code_e::E_NOT_OK);
            break;
    }
    
    // 转换负载
    auto payload = runtime_->create_payload();
    payload->set_data(response.payload);
    someip_response->set_payload(payload);
    
    return someip_response;
}

void SomeIpServiceVsomeip::logInfo(const std::string& message) {
    std::cout << "[INFO] SomeIpServiceVsomeip: " << message << std::endl;
}

void SomeIpServiceVsomeip::logError(const std::string& message) {
    std::cerr << "[ERROR] SomeIpServiceVsomeip: " << message << std::endl;
}

void SomeIpServiceVsomeip::logWarning(const std::string& message) {
    std::cout << "[WARN] SomeIpServiceVsomeip: " << message << std::endl;
}

} // namespace ccu_diag
