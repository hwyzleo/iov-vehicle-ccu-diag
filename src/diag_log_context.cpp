#include "diag_log_context.h"
#include <chrono>
#include <random>
#include <sstream>
#include <iomanip>

namespace ccu_diag {

ccu::fw::log::ContextScope make_context_scope(
    const std::string& trace_id,
    const std::string& request_id,
    const std::string& session_id
) {
    ccu::fw::log::LogContext ctx;
    ctx.trace_id = trace_id;
    ctx.request_id = request_id;
    ctx.session_id = session_id;
    return ccu::fw::log::ContextScope(std::move(ctx));
}

ccu::fw::log::ContextScope make_someip_child_scope(
    const std::string& service_id,
    const std::string& method_id,
    const std::string& client_id,
    const std::string& remote_endpoint
) {
    ccu::fw::log::LogContext ctx;

    // 继承当前上下文的 trace_id / request_id / session_id
    const auto* current = ccu::fw::log::ContextScope::current();
    if (current) {
        ctx.trace_id = current->trace_id;
        ctx.request_id = current->request_id;
        ctx.session_id = current->session_id;
    }

    // 附加 SOME/IP 调用上下文
    ctx.someip = std::make_unique<ccu::fw::log::SomeIpContext>();
    ctx.someip->service_id = service_id;
    ctx.someip->method_id = method_id;
    ctx.someip->client_id = client_id;
    ctx.someip->remote_endpoint = remote_endpoint;

    return ccu::fw::log::ContextScope(std::move(ctx));
}

ccu::fw::log::ContextScope make_someip_context_scope(
    const std::string& trace_id,
    const std::string& request_id,
    const std::string& service_id,
    const std::string& method_id,
    const std::string& client_id,
    const std::string& remote_endpoint
) {
    ccu::fw::log::LogContext ctx;
    ctx.trace_id = trace_id;
    ctx.request_id = request_id;

    ctx.someip = std::make_unique<ccu::fw::log::SomeIpContext>();
    ctx.someip->service_id = service_id;
    ctx.someip->method_id = method_id;
    ctx.someip->client_id = client_id;
    ctx.someip->remote_endpoint = remote_endpoint;

    return ccu::fw::log::ContextScope(std::move(ctx));
}

ccu::fw::log::ContextScope make_diagnostic_child_scope(
    const std::string& ecu_id,
    const std::string& logical_address,
    const std::string& transport,
    const std::string& uds_sid,
    const std::string& did
) {
    ccu::fw::log::LogContext ctx;

    // 继承当前上下文的 trace_id / request_id / session_id
    // （CCU-DIAG-DSN-CR-008 §上下文传播 第3点：同一 ECU 的重试沿用 trace/request 上下文）
    const auto* current = ccu::fw::log::ContextScope::current();
    if (current) {
        ctx.trace_id = current->trace_id;
        ctx.request_id = current->request_id;
        ctx.session_id = current->session_id;
        // 继承上层 SOME/IP 上下文（Provider 入站）
        if (current->someip) {
            ctx.someip = std::make_unique<ccu::fw::log::SomeIpContext>(*current->someip);
        }
    }

    // 附加诊断上下文（逐 ECU 子作用域）
    ctx.diagnostic = std::make_unique<ccu::fw::log::DiagnosticContext>();
    ctx.diagnostic->ecu_id = ecu_id;
    ctx.diagnostic->logical_address = logical_address;
    ctx.diagnostic->transport = transport;
    ctx.diagnostic->uds_sid = uds_sid;
    ctx.diagnostic->did = did;

    return ccu::fw::log::ContextScope(std::move(ctx));
}

std::string generate_trace_id() {
    auto now = std::chrono::system_clock::now();
    auto timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()
    ).count();

    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(1000, 9999);

    std::ostringstream oss;
    oss << "diag-trace-" << timestamp << "-" << dis(gen);
    return oss.str();
}

std::string generate_request_id() {
    auto now = std::chrono::system_clock::now();
    auto timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()
    ).count();

    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(1000, 9999);

    std::ostringstream oss;
    oss << "diag-req-" << timestamp << "-" << dis(gen);
    return oss.str();
}

} // namespace ccu_diag
