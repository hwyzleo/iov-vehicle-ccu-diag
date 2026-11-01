#include "diag_log_adapter.h"

namespace ccu_diag {

bool DiagLogAdapter::s_initialized = false;

ccu::fw::log::InitResult DiagLogAdapter::init(
    const std::string& service,
    const ccu::fw::log::LogConfig& config
) {
    auto result = ccu::fw::log::Logger::init(service, config);
    if (result.error == ccu::fw::log::LogError::kOk) {
        s_initialized = true;
    }
    return result;
}

ccu::fw::log::Logger DiagLogAdapter::facade() {
    return ccu::fw::log::Logger::get("facade");
}

ccu::fw::log::Logger DiagLogAdapter::orchestrator() {
    return ccu::fw::log::Logger::get("orchestrator");
}

ccu::fw::log::Logger DiagLogAdapter::tester() {
    return ccu::fw::log::Logger::get("tester");
}

ccu::fw::log::Logger DiagLogAdapter::router() {
    return ccu::fw::log::Logger::get("router");
}

ccu::fw::log::Logger DiagLogAdapter::transport() {
    return ccu::fw::log::Logger::get("transport");
}

ccu::fw::log::Logger DiagLogAdapter::did_registry() {
    return ccu::fw::log::Logger::get("did_registry");
}

ccu::fw::log::Logger DiagLogAdapter::vin_store() {
    return ccu::fw::log::Logger::get("vin_store");
}

ccu::fw::log::Logger DiagLogAdapter::sec_client() {
    return ccu::fw::log::Logger::get("sec_client");
}

bool DiagLogAdapter::isInitialized() {
    return s_initialized;
}

} // namespace ccu_diag
