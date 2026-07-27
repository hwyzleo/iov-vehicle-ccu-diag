#include "diag_log_adapter.h"

namespace cgw_diag {

bool DiagLogAdapter::s_initialized = false;

cgw::fw::log::InitResult DiagLogAdapter::init(
    const std::string& service,
    const cgw::fw::log::LogConfig& config
) {
    auto result = cgw::fw::log::Logger::init(service, config);
    if (result.error == cgw::fw::log::LogError::kOk) {
        s_initialized = true;
    }
    return result;
}

cgw::fw::log::Logger DiagLogAdapter::facade() {
    return cgw::fw::log::Logger::get("facade");
}

cgw::fw::log::Logger DiagLogAdapter::orchestrator() {
    return cgw::fw::log::Logger::get("orchestrator");
}

cgw::fw::log::Logger DiagLogAdapter::tester() {
    return cgw::fw::log::Logger::get("tester");
}

cgw::fw::log::Logger DiagLogAdapter::router() {
    return cgw::fw::log::Logger::get("router");
}

cgw::fw::log::Logger DiagLogAdapter::transport() {
    return cgw::fw::log::Logger::get("transport");
}

cgw::fw::log::Logger DiagLogAdapter::did_registry() {
    return cgw::fw::log::Logger::get("did_registry");
}

cgw::fw::log::Logger DiagLogAdapter::vin_store() {
    return cgw::fw::log::Logger::get("vin_store");
}

cgw::fw::log::Logger DiagLogAdapter::sec_client() {
    return cgw::fw::log::Logger::get("sec_client");
}

bool DiagLogAdapter::isInitialized() {
    return s_initialized;
}

} // namespace cgw_diag
