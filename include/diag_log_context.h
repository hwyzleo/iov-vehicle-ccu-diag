#pragma once

/**
 * CGW-DIAG 上下文传播辅助
 *
 * 使用 cgw-framework-log 提供的 ContextScope 和 LogContext
 * 实现 trace_id / request_id / session_id 跨 SOME/IP 调用链与逐 ECU
 * 诊断操作的传播（CGW-DIAG-DSN-CR-008 §上下文传播）
 */

#include "log.h"
#include "log_types.h"
#include <string>
#include <sstream>
#include <iomanip>

namespace cgw_diag {

/// 创建顶层 ContextScope 的便捷函数
/// 用法：auto scope = make_context_scope(trace_id, request_id);
[[nodiscard]] cgw::fw::log::ContextScope make_context_scope(
    const std::string& trace_id,
    const std::string& request_id,
    const std::string& session_id = ""
);

/// 创建带 SOME/IP 子作用域的 ContextScope（用于调用 CGW-SEC 时）
/// 继承当前上下文的 trace_id/request_id/session_id，附加目标 Service/Method 信息
/// （CGW-DIAG-DSN-CR-008 §上下文传播 第4点：DIAG 调用 CGW-SEC 时创建 SOME/IP Client 子作用域）
[[nodiscard]] cgw::fw::log::ContextScope make_someip_child_scope(
    const std::string& service_id,
    const std::string& method_id,
    const std::string& client_id = "",
    const std::string& remote_endpoint = ""
);

/// 创建带 SOME/IP 上下文的顶层 ContextScope（用于 Provider 收到入站请求时）
/// （CGW-DIAG-DSN-CR-008 §上下文传播 第1点：SOME/IP Provider 收到 FOTA/SEC 请求时建立 ContextScope）
[[nodiscard]] cgw::fw::log::ContextScope make_someip_context_scope(
    const std::string& trace_id,
    const std::string& request_id,
    const std::string& service_id,
    const std::string& method_id,
    const std::string& client_id = "",
    const std::string& remote_endpoint = ""
);

/// 创建带诊断上下文的子作用域（DIAG 专用）
/// 继承当前上下文的 trace_id/request_id/session_id，附加 ecu_id/logical_address/transport/uds_sid/did
/// （CGW-DIAG-DSN-CR-008 §上下文传播 第2点：Orchestrator 为每个目标 ECU 创建诊断子作用域）
[[nodiscard]] cgw::fw::log::ContextScope make_diagnostic_child_scope(
    const std::string& ecu_id,
    const std::string& logical_address,
    const std::string& transport,
    const std::string& uds_sid,
    const std::string& did
);

/// 生成 trace ID（格式: diag-trace-<timestamp>-<random>）
std::string generate_trace_id();

/// 生成 request ID（格式: diag-req-<timestamp>-<random>）
std::string generate_request_id();

/// 获取当前上下文（便捷封装）
inline const cgw::fw::log::LogContext* current_context() {
    return cgw::fw::log::ContextScope::current();
}

/// 从当前上下文获取 trace_id
inline std::string current_trace_id() {
    const auto* ctx = cgw::fw::log::ContextScope::current();
    return ctx ? ctx->trace_id : "";
}

/// 从当前上下文获取 request_id
inline std::string current_request_id() {
    const auto* ctx = cgw::fw::log::ContextScope::current();
    return ctx ? ctx->request_id : "";
}

/// 从当前上下文获取 session_id
inline std::string current_session_id() {
    const auto* ctx = cgw::fw::log::ContextScope::current();
    return ctx ? ctx->session_id : "";
}

/// 将 uint16_t 格式为规范十六进制字符串（如 0x1110）
/// （CGW-FW-SPEC设计 §5.4: 规范十六进制字符串）
inline std::string hex_id(uint16_t id) {
    std::ostringstream oss;
    oss << "0x" << std::setfill('0') << std::setw(4) << std::hex << id;
    return oss.str();
}

/// 将 uint8_t 格式为规范十六进制字符串（如 0x22）
inline std::string hex_id(uint8_t id) {
    std::ostringstream oss;
    oss << "0x" << std::setfill('0') << std::setw(2) << std::hex << static_cast<int>(id);
    return oss.str();
}

} // namespace cgw_diag
