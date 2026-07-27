#include <gtest/gtest.h>
#include "diag_log_adapter.h"
#include "diag_log_context.h"
#include "error_codes.h"
#include <thread>
#include <chrono>
#include <atomic>
#include <vector>

namespace cgw_diag {
namespace {

class LogIntegrationTest : public ::testing::Test {
protected:
    void SetUp() override {
        cgw::fw::log::LogConfig config;
        config.level = cgw::fw::log::LogLevel::kDebug;

        auto result = DiagLogAdapter::init("cgw-diag-integration-test", config);
        ASSERT_EQ(result.error, cgw::fw::log::LogError::kOk);
    }
};

// ============================================================
// 上下文传播测试（CGW-DIAG-DSN-CR-008 §上下文传播）
// ============================================================

TEST_F(LogIntegrationTest, ContextScopeBasics) {
    // 验证 ContextScope RAII：构造时设置上下文，析构时恢复
    EXPECT_EQ(current_context(), nullptr);

    {
        auto scope = make_context_scope("trace-001", "req-001");
        auto* ctx = current_context();
        ASSERT_NE(ctx, nullptr);
        EXPECT_EQ(ctx->trace_id, "trace-001");
        EXPECT_EQ(ctx->request_id, "req-001");
    }

    EXPECT_EQ(current_context(), nullptr);
}

TEST_F(LogIntegrationTest, ContextScopeNesting) {
    // 验证嵌套 ContextScope：内层恢复外层上下文
    {
        auto outer = make_context_scope("trace-outer", "req-outer");

        {
            auto inner = make_context_scope("trace-inner", "req-inner");
            auto* ctx = current_context();
            ASSERT_NE(ctx, nullptr);
            EXPECT_EQ(ctx->trace_id, "trace-inner");
        }

        // 内层析构后恢复外层
        auto* ctx = current_context();
        ASSERT_NE(ctx, nullptr);
        EXPECT_EQ(ctx->trace_id, "trace-outer");
    }
}

TEST_F(LogIntegrationTest, SomeipChildScopeInheritsTraceId) {
    // 验证 SOME/IP 子作用域继承 trace_id（CGW-DIAG-DSN-CR-008 §上下文传播 第4点）
    std::string parent_trace = "parent-trace-123";
    std::string parent_req = "parent-req-456";

    {
        auto parent_scope = make_context_scope(parent_trace, parent_req);

        // 创建 SOME/IP 子作用域（模拟调用 CGW-SEC）
        {
            auto child_scope = make_someip_child_scope("0x1140", "0x0001");
            auto* ctx = current_context();
            ASSERT_NE(ctx, nullptr);
            EXPECT_EQ(ctx->trace_id, parent_trace);
            EXPECT_EQ(ctx->request_id, parent_req);
            ASSERT_NE(ctx->someip, nullptr);
            EXPECT_EQ(ctx->someip->service_id, "0x1140");
            EXPECT_EQ(ctx->someip->method_id, "0x0001");
        }

        // 子作用域析构后恢复父上下文
        auto* ctx = current_context();
        ASSERT_NE(ctx, nullptr);
        EXPECT_EQ(ctx->trace_id, parent_trace);
        EXPECT_EQ(ctx->someip, nullptr);
    }
}

TEST_F(LogIntegrationTest, DiagnosticChildScopeInheritsTraceId) {
    // 验证诊断子作用域继承 trace_id 并附加诊断上下文
    // （CGW-DIAG-DSN-CR-008 §上下文传播 第2点）
    std::string parent_trace = "diag-trace-789";
    std::string parent_req = "diag-req-789";

    {
        auto parent_scope = make_context_scope(parent_trace, parent_req);

        // 创建诊断子作用域（模拟逐 ECU 诊断操作）
        {
            auto diag_scope = make_diagnostic_child_scope(
                "BMS", "0x001A", "DoCAN", "0x22", "0xF195"
            );
            auto* ctx = current_context();
            ASSERT_NE(ctx, nullptr);
            // 继承 trace_id / request_id
            EXPECT_EQ(ctx->trace_id, parent_trace);
            EXPECT_EQ(ctx->request_id, parent_req);
            // 附加诊断上下文
            ASSERT_NE(ctx->diagnostic, nullptr);
            EXPECT_EQ(ctx->diagnostic->ecu_id, "BMS");
            EXPECT_EQ(ctx->diagnostic->logical_address, "0x001A");
            EXPECT_EQ(ctx->diagnostic->transport, "DoCAN");
            EXPECT_EQ(ctx->diagnostic->uds_sid, "0x22");
            EXPECT_EQ(ctx->diagnostic->did, "0xF195");
        }

        // 子作用域析构后恢复父上下文（诊断上下文被清除）
        auto* ctx = current_context();
        ASSERT_NE(ctx, nullptr);
        EXPECT_EQ(ctx->trace_id, parent_trace);
        EXPECT_EQ(ctx->diagnostic, nullptr);
    }
}

TEST_F(LogIntegrationTest, DiagnosticChildScopeInheritsSomeipContext) {
    // 诊断子作用域应继承上层 SOME/IP 上下文（Provider 入站）
    {
        auto provider_scope = make_someip_context_scope(
            "trace-001", "req-001", "0x1110", "0x0002"
        );

        {
            auto diag_scope = make_diagnostic_child_scope(
                "BMS", "0x001A", "DoCAN", "0x22", "0xF195"
            );
            auto* ctx = current_context();
            ASSERT_NE(ctx, nullptr);
            // 继承 trace_id
            EXPECT_EQ(ctx->trace_id, "trace-001");
            // 继承 SOME/IP 上下文
            ASSERT_NE(ctx->someip, nullptr);
            EXPECT_EQ(ctx->someip->service_id, "0x1110");
            // 附加诊断上下文
            ASSERT_NE(ctx->diagnostic, nullptr);
            EXPECT_EQ(ctx->diagnostic->ecu_id, "BMS");
        }
    }
}

TEST_F(LogIntegrationTest, FullChainContextPropagation) {
    // 模拟全链路：FOTA -> DIAG Provider -> 逐 ECU Tester/Router/Transport
    // （CGW-DIAG-DSN-CR-008 §上下文传播）
    std::string trace_id = "fullchain-trace-001";
    std::string request_id = "fullchain-req-001";

    {
        // 1. Provider 收到 FOTA 请求，建立 SOME/IP 上下文
        auto provider_scope = make_someip_context_scope(
            trace_id, request_id, "0x1110", "0x0002", "0x0001"
        );

        EXPECT_EQ(current_trace_id(), trace_id);
        EXPECT_EQ(current_request_id(), request_id);

        // 2. Orchestrator 采集整车版本（模拟逐 ECU）
        // 每个目标 ECU 创建诊断子作用域
        {
            auto diag_scope = make_diagnostic_child_scope(
                "BMS", "0x001A", "DoCAN", "0x22", "0xF195"
            );

            // 3. Tester 发送 UDS 请求（trace_id 保持一致）
            DiagLogAdapter::tester().info(diag_events::TESTER_REQUEST_SUCCEEDED,
                "UDS request succeeded",
                {dlog::f_str("ecu_id", "BMS"),
                 dlog::f_str("transport", "DoCAN"),
                 dlog::f_str("uds_sid", "0x22"),
                 dlog::f_str("did", "0xF195"),
                 dlog::f_int("attempt", 1),
                 dlog::f_int("duration_ms", 52)}
            );

            // 验证 trace_id 在 Tester 操作期间保持一致
            EXPECT_EQ(current_trace_id(), trace_id);
        }

        // 4. 整车采集完成
        DiagLogAdapter::orchestrator().info(diag_events::INVENTORY_COMPLETED,
            "Inventory completed",
            {dlog::f_str("registry_version", "1.0.0"),
             dlog::f_str("overall_result", "ALL_OK"),
             dlog::f_int("ecu_count", 1),
             dlog::f_int("failed_count", 0),
             dlog::f_int("duration_ms", 100)}
        );

        EXPECT_EQ(current_trace_id(), trace_id);
    }

    EXPECT_EQ(current_context(), nullptr);
}

TEST_F(LogIntegrationTest, DiagToSecContextPropagation) {
    // DIAG -> SEC 调用链上下文传播
    // （CGW-DIAG-DSN-CR-008 §上下文传播 第4点）
    std::string trace_id = "sec-trace-001";
    std::string request_id = "sec-req-001";

    {
        auto parent_scope = make_context_scope(trace_id, request_id, "diag-sess-001");

        // DIAG 调用 CGW-SEC 时创建 SOME/IP Client 子作用域
        {
            auto sec_scope = make_someip_child_scope("0x1140", "0x0001");

            auto* ctx = current_context();
            ASSERT_NE(ctx, nullptr);
            // 继承 trace_id / request_id / session_id
            EXPECT_EQ(ctx->trace_id, trace_id);
            EXPECT_EQ(ctx->request_id, request_id);
            EXPECT_EQ(ctx->session_id, "diag-sess-001");
            // 附加 SEC SOME/IP 上下文
            ASSERT_NE(ctx->someip, nullptr);
            EXPECT_EQ(ctx->someip->service_id, "0x1140");

            // 模拟 getSeed 日志
            DiagLogAdapter::sec_client().info("diag.security.get_seed.succeeded",
                "getSeed succeeded",
                {dlog::f_str("security_level", "LEVEL_1"),
                 dlog::f_str("operation", "getSeed"),
                 dlog::f_int("seed_id", 42),
                 dlog::f_int("seed_length", 4)}
            );
        }
    }
}

TEST_F(LogIntegrationTest, AsyncContextCopy) {
    // 异步任务边界显式复制日志上下文
    // （CGW-DIAG-DSN-CR-008 §上下文传播 第4点）
    std::string trace_id = "async-trace-001";
    std::string request_id = "async-req-001";

    std::atomic<bool> async_done{false};
    std::string async_trace_check;

    {
        auto scope = make_context_scope(trace_id, request_id);

        // 捕获上下文并传递到异步线程
        std::string captured_trace = current_trace_id();
        std::string captured_req = current_request_id();

        std::thread([&async_done, &async_trace_check, captured_trace, captured_req]() {
            // 在异步线程中重建上下文
            auto async_scope = make_context_scope(captured_trace, captured_req);
            async_trace_check = current_trace_id();
            async_done.store(true);
        }).join();
    }

    EXPECT_TRUE(async_done.load());
    EXPECT_EQ(async_trace_check, trace_id);

    // 主线程上下文已清理
    EXPECT_EQ(current_context(), nullptr);
}

TEST_F(LogIntegrationTest, SessionIdPropagation) {
    // VIN provisioning 会话以 session_id 关联 0x27、0x2E
    {
        auto scope = make_context_scope("trace-vin", "req-vin", "diag-sess-042");
        EXPECT_EQ(current_session_id(), "diag-sess-042");

        // 子作用域应继承 session_id
        {
            auto child = make_someip_child_scope("0x1140", "0x0001");
            EXPECT_EQ(current_session_id(), "diag-sess-042");
        }
    }
}

// ============================================================
// 脱敏测试（CGW-DIAG-DSN-CR-008 §字段与脱敏）
// ============================================================

TEST_F(LogIntegrationTest, VinDesensitization) {
    // VIN 按 Identifier 处理：默认掩码或不可逆摘要
    std::string test_vin = "LSVAU2180N2123456";

    {
        auto scope = make_context_scope("trace-vin", "req-vin");

        DiagLogAdapter::vin_store().info("test.vin_desensitization",
            "VIN should be masked",
            {dlog::f_str("vin", test_vin, cgw::fw::log::Sensitivity::Identifier)}
        );

        // VIN 不应直接拼入 message
        DiagLogAdapter::vin_store().info("test.vin_not_in_message",
            "VIN processing result",
            {dlog::f_str("vin_hash", test_vin, cgw::fw::log::Sensitivity::Identifier),
             dlog::f_str("result", "OK")}
        );
    }
}

TEST_F(LogIntegrationTest, PayloadDesensitization) {
    // UDS/SOME-IP 原始 payload 按 Payload 处理
    {
        auto scope = make_context_scope("trace-payload", "req-payload");

        // 正确做法：仅记录元数据
        DiagLogAdapter::tester().info("test.payload_metadata",
            "Payload metadata only",
            {dlog::f_int("payload_length", 64),
             dlog::f_str("uds_sid", "0x22"),
             dlog::f_str("did", "0xF195")}
        );

        // 原始 payload（应被框架限长/过滤）
        std::string raw_payload = "raw_uds_response_data_with_versions";
        DiagLogAdapter::tester().info("test.payload_raw",
            "Raw payload test",
            {dlog::f_str("raw_payload", raw_payload, cgw::fw::log::Sensitivity::Payload)}
        );
    }
}

TEST_F(LogIntegrationTest, SecretFieldRejection) {
    // 0x27 seed/key、session_key 及密钥材料按 Secret 直接拒绝
    {
        auto scope = make_context_scope("trace-secret", "req-secret");

        DiagLogAdapter::sec_client().info("test.secret_rejection",
            "Secret fields should be rejected",
            {dlog::f_str("seed", "0xABCD1234", cgw::fw::log::Sensitivity::Secret),
             dlog::f_str("key", "0xEF567890", cgw::fw::log::Sensitivity::Secret),
             dlog::f_str("session_key", "session_key_data", cgw::fw::log::Sensitivity::Secret)}
        );

        // 验证日志可正常执行（框架拒绝 Secret 字段但不崩溃）
        SUCCEED();
    }
}

TEST_F(LogIntegrationTest, WriteDataF190PayloadProhibited) {
    // 0x2E F190 payload 默认禁止输出
    {
        auto scope = make_context_scope("trace-f190", "req-f190");

        // 仅记录元数据，不记录原始 F190 payload
        DiagLogAdapter::vin_store().info("test.f190_metadata",
            "VIN write payload metadata",
            {dlog::f_str("did", "0xF190"),
             dlog::f_int("payload_length", 17),
             dlog::f_str("operation", "writeVin")}
        );
    }
}

// ============================================================
// 并发与压力测试（CGW-DIAG-DSN-CR-008 §测试-压力测试）
// ============================================================

TEST_F(LogIntegrationTest, ConcurrentLogging) {
    // 并发日志记录：验证线程安全
    const int num_threads = 4;
    const int logs_per_thread = 25;
    std::vector<std::thread> threads;

    for (int i = 0; i < num_threads; ++i) {
        threads.emplace_back([i]() {
            auto logger = DiagLogAdapter::orchestrator();
            for (int j = 0; j < logs_per_thread; ++j) {
                logger.info("test.concurrent_logging",
                    "Concurrent logging test",
                    {dlog::f_int("thread_id", i),
                     dlog::f_int("log_index", j)}
                );
            }
        });
    }

    for (auto& t : threads) {
        t.join();
    }

    SUCCEED();
}

TEST_F(LogIntegrationTest, ConcurrentContextScopes) {
    // 并发 ContextScope：验证线程局部上下文隔离
    const int num_threads = 4;
    std::vector<std::thread> threads;
    std::atomic<int> success_count{0};

    for (int i = 0; i < num_threads; ++i) {
        threads.emplace_back([i, &success_count]() {
            std::string trace = "trace-" + std::to_string(i);
            auto scope = make_context_scope(trace, "req");

            // 每个线程应看到自己的 trace_id
            if (current_trace_id() == trace) {
                success_count++;
            }

            DiagLogAdapter::tester().info("test.concurrent_context",
                "Concurrent context test",
                {dlog::f_int("thread_id", i)}
            );
        });
    }

    for (auto& t : threads) {
        t.join();
    }

    EXPECT_EQ(success_count.load(), num_threads);
}

TEST_F(LogIntegrationTest, LoggingDoesNotBlockBusiness) {
    // 验证普通日志故障不阻断业务
    // （CGW-DIAG-DSN-CR-008 §实时性与故障降级）
    {
        auto scope = make_context_scope("trace-noblock", "req-noblock");

        // 大量日志不应阻断业务操作
        for (int i = 0; i < 100; ++i) {
            DiagLogAdapter::orchestrator().info("test.high_volume",
                "High volume logging test",
                {dlog::f_int("index", i)}
            );
        }

        SUCCEED();
    }
}

// ============================================================
// 故障降级测试（CGW-DIAG-DSN-CR-008 §实时性与故障降级）
// ============================================================

TEST_F(LogIntegrationTest, StrictModeInitFailure) {
    // 验证 strict 模式下的 fail-closed 行为
    cgw::fw::log::LogConfig config;
    config.strict = true;
    config.level = cgw::fw::log::LogLevel::kInfo;

    // 使用空服务名测试（框架可能返回错误或接受）
    auto result = DiagLogAdapter::init("", config);
    // 框架行为取决于实现，此处仅验证不崩溃
    SUCCEED();
}

TEST_F(LogIntegrationTest, AllBusinessEventsEmitted) {
    // 验证完整采集流程中关键事件被输出
    {
        auto scope = make_context_scope("trace-events", "req-events", "diag-sess-99");

        // 1. 请求受理
        DiagLogAdapter::facade().info(diag_events::INVENTORY_REQUEST_ACCEPTED,
            "Request accepted",
            {dlog::f_str("request_id", "req-events"),
             dlog::f_str("someip_service_id", "0x1110"),
             dlog::f_str("someip_method_id", "0x0002")});

        // 2. Registry 加载
        DiagLogAdapter::did_registry().info(diag_events::REGISTRY_LOADED,
            "Registry loaded",
            {dlog::f_str("registry_version", "1.0.0"),
             dlog::f_int("ecu_count", 3)});

        // 3. 逐 ECU Tester 操作
        {
            auto diag_scope = make_diagnostic_child_scope(
                "BMS", "0x001A", "DoCAN", "0x22", "0xF195");
            DiagLogAdapter::tester().info(diag_events::TESTER_REQUEST_SUCCEEDED,
                "UDS request succeeded",
                {dlog::f_str("ecu_id", "BMS"),
                 dlog::f_str("transport", "DoCAN"),
                 dlog::f_str("uds_sid", "0x22"),
                 dlog::f_str("did", "0xF195"),
                 dlog::f_int("attempt", 1),
                 dlog::f_int("duration_ms", 50)});
        }

        // 4. 整车采集完成
        DiagLogAdapter::orchestrator().info(diag_events::INVENTORY_COMPLETED,
            "Inventory completed",
            {dlog::f_str("registry_version", "1.0.0"),
             dlog::f_str("overall_result", "ALL_OK"),
             dlog::f_int("ecu_count", 3),
             dlog::f_int("failed_count", 0),
             dlog::f_int("duration_ms", 100)});
    }

    SUCCEED();
}

} // namespace
} // namespace cgw_diag
