#pragma once

#include "log.h"
#include "log_types.h"
#include <string>
#include <cstdint>

namespace ccu_diag {

// ============================================================
// DIAG 事件名常量（CCU-DIAG-DSN-CR-008 事件目录）
// ============================================================
namespace diag_events {
// Service lifecycle
constexpr const char* SERVICE_STARTING        = "diag.service.starting";
constexpr const char* SERVICE_CONFIG_LOADED   = "diag.service.config_loaded";
constexpr const char* SERVICE_LOG_INITIALIZED = "diag.service.log_initialized";
constexpr const char* SERVICE_READY           = "diag.service.ready";
constexpr const char* SERVICE_SHUTTING_DOWN   = "diag.service.shutting_down";
// DID Registry
constexpr const char* REGISTRY_LOADED       = "diag.registry.loaded";
constexpr const char* REGISTRY_LOAD_FAILED  = "diag.registry.load.failed";
// Inventory (facade)
constexpr const char* INVENTORY_REQUEST_ACCEPTED = "diag.inventory.request.accepted";
constexpr const char* INVENTORY_COMPLETED        = "diag.inventory.completed";
// Inventory store / seed (CCU-DIAG-DSN-CR-010)
constexpr const char* INVENTORY_LOADED              = "diag.inventory.loaded";
constexpr const char* INVENTORY_LOAD_FAILED         = "diag.inventory.load.failed";
constexpr const char* INVENTORY_SEED_IMPORTED       = "diag.inventory.seed.imported";
constexpr const char* INVENTORY_SEED_IMPORT_FAILED  = "diag.inventory.seed.import.failed";
constexpr const char* INVENTORY_SEED_MODE_ENABLED   = "diag.inventory.seed_mode_enabled";
constexpr const char* INVENTORY_UPDATED             = "diag.inventory.updated";
// FOTA installed version registration (CCU-DIAG-DSN-CR-010)
constexpr const char* REGISTRATION_REQUESTED        = "diag.fota.registration.requested";
constexpr const char* REGISTRATION_APPLIED          = "diag.fota.registration.applied";
constexpr const char* REGISTRATION_DUPLICATE        = "diag.fota.registration.duplicate";
constexpr const char* REGISTRATION_REJECTED         = "diag.fota.registration.rejected";
constexpr const char* REGISTRATION_RETRYABLE_FAILED = "diag.fota.registration.retryable_failed";
// Tester (per-ECU)
constexpr const char* TESTER_REQUEST_SUCCEEDED = "diag.tester.request.succeeded";
constexpr const char* TESTER_REQUEST_FAILED    = "diag.tester.request.failed";
// Router
constexpr const char* ROUTE_FAILED = "diag.route.failed";
// Transport
constexpr const char* TRANSPORT_FAILED = "diag.transport.failed";
// VIN
constexpr const char* VIN_READ_FAILED       = "diag.vin.read.failed";
constexpr const char* VIN_WRITE_SUCCEEDED   = "diag.vin.write.succeeded";
constexpr const char* VIN_WRITE_FAILED      = "diag.vin.write.failed";
// Security access
constexpr const char* SECURITY_ACCESS_FAILED = "diag.security.access.failed";
} // namespace diag_events

// ============================================================
// 便捷字段构造器 - 减少业务代码中的冗长写法
// ============================================================
namespace dlog {
inline ccu::fw::log::Field f_str(const std::string& key, const std::string& val,
                                  ccu::fw::log::Sensitivity s = ccu::fw::log::Sensitivity::Normal) {
    return ccu::fw::log::Field(key, ccu::fw::log::FieldValue::makeString(val), s);
}
inline ccu::fw::log::Field f_int(const std::string& key, int64_t val) {
    return ccu::fw::log::Field(key, ccu::fw::log::FieldValue::makeInt(val));
}
inline ccu::fw::log::Field f_bool(const std::string& key, bool val) {
    return ccu::fw::log::Field(key, ccu::fw::log::FieldValue::makeBool(val));
}
inline ccu::fw::log::Field f_double(const std::string& key, double val) {
    return ccu::fw::log::Field(key, ccu::fw::log::FieldValue::makeDouble(val));
}
} // namespace dlog

// ============================================================
// DiagLogAdapter - 封装 ccu-framework-log 的初始化和模块 Logger
//
// 模块划分（CCU-DIAG-DSN-CR-008 §启动与模块 Logger）：
// - facade:       SOME/IP 入站方法、响应及调用方上下文
// - orchestrator: 整车采集、逐 ECU 编排、聚合结果
// - tester:       UDS 0x22 / SOME-IP getVersion 请求、响应、NRC 与重试
// - router:       逻辑地址解析、目标子网选择与路由失败
// - transport:    DoCAN / DoIP 发送、接收、超时与链路异常
// - did_registry: 配置加载、版本和条目校验
// - vin_store:    VIN 读取、受限写入及持久化结果
// - sec_client:   调用 CCU-SEC 的 getSeed/verifyKey 结果，仅记录非密钥元数据
// ============================================================
class DiagLogAdapter {
public:
    /// 初始化日志系统（在 main.cpp 中调用一次）
    static ccu::fw::log::InitResult init(
        const std::string& service,
        const ccu::fw::log::LogConfig& config
    );

    /// 获取各模块的 Logger 实例
    static ccu::fw::log::Logger facade();
    static ccu::fw::log::Logger orchestrator();
    static ccu::fw::log::Logger tester();
    static ccu::fw::log::Logger router();
    static ccu::fw::log::Logger transport();
    static ccu::fw::log::Logger did_registry();
    static ccu::fw::log::Logger vin_store();
    static ccu::fw::log::Logger inventory_store();
    static ccu::fw::log::Logger fota_registrar();
    static ccu::fw::log::Logger sec_client();

    /// 是否已初始化
    static bool isInitialized();

private:
    static bool s_initialized;
};

} // namespace ccu_diag
