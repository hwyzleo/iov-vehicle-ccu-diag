#include <gtest/gtest.h>
#include "diag_log_adapter.h"
#include "diag_log_context.h"
#include <regex>

namespace cgw_diag {
namespace {

class DiagLogAdapterTest : public ::testing::Test {
protected:
    void SetUp() override {
        cgw::fw::log::LogConfig config;
        config.level = cgw::fw::log::LogLevel::kDebug;

        auto result = DiagLogAdapter::init("cgw-diag-test", config);
        ASSERT_EQ(result.error, cgw::fw::log::LogError::kOk);
    }
};

TEST_F(DiagLogAdapterTest, InitSucceeds) {
    // SetUp 已验证初始化成功
    SUCCEED();
}

TEST_F(DiagLogAdapterTest, IsInitializedFlag) {
    EXPECT_TRUE(DiagLogAdapter::isInitialized());
}

// ============================================================
// 模块 Logger 获取（CGW-DIAG-DSN-CR-008 §启动与模块 Logger）
// ============================================================
TEST_F(DiagLogAdapterTest, GetFacadeLogger) {
    auto logger = DiagLogAdapter::facade();
    logger.info("test.event", "test message");
}

TEST_F(DiagLogAdapterTest, GetOrchestratorLogger) {
    auto logger = DiagLogAdapter::orchestrator();
    logger.info("test.event", "test message");
}

TEST_F(DiagLogAdapterTest, GetTesterLogger) {
    auto logger = DiagLogAdapter::tester();
    logger.info("test.event", "test message");
}

TEST_F(DiagLogAdapterTest, GetRouterLogger) {
    auto logger = DiagLogAdapter::router();
    logger.info("test.event", "test message");
}

TEST_F(DiagLogAdapterTest, GetTransportLogger) {
    auto logger = DiagLogAdapter::transport();
    logger.info("test.event", "test message");
}

TEST_F(DiagLogAdapterTest, GetDidRegistryLogger) {
    auto logger = DiagLogAdapter::did_registry();
    logger.info("test.event", "test message");
}

TEST_F(DiagLogAdapterTest, GetVinStoreLogger) {
    auto logger = DiagLogAdapter::vin_store();
    logger.info("test.event", "test message");
}

TEST_F(DiagLogAdapterTest, GetSecClientLogger) {
    auto logger = DiagLogAdapter::sec_client();
    logger.info("test.event", "test message");
}

// ============================================================
// 字段类型验证
// ============================================================
TEST_F(DiagLogAdapterTest, FieldTypesWork) {
    DiagLogAdapter::orchestrator().info("test.event",
        "Test field types",
        {dlog::f_str("string_field", "value"),
         dlog::f_int("int_field", 42),
         dlog::f_bool("bool_field", true),
         dlog::f_double("double_field", 3.14)}
    );
}

// ============================================================
// 敏感度级别验证（CGW-DIAG-DSN-CR-008 §字段与脱敏）
// ============================================================
TEST_F(DiagLogAdapterTest, SensitivityLevelsWork) {
    DiagLogAdapter::orchestrator().info("test.event",
        "Test sensitivity levels",
        {dlog::f_str("normal_field", "normal"),
         dlog::f_str("identifier_field", "VIN123", cgw::fw::log::Sensitivity::Identifier),
         dlog::f_str("payload_field", "raw_data", cgw::fw::log::Sensitivity::Payload),
         dlog::f_str("secret_field", "secret", cgw::fw::log::Sensitivity::Secret)}
    );
}

// ============================================================
// 日志级别方法验证
// ============================================================
TEST_F(DiagLogAdapterTest, LogLevelMethods) {
    auto logger = DiagLogAdapter::orchestrator();
    logger.trace("test.trace", "trace message");
    logger.debug("test.debug", "debug message");
    logger.info("test.info", "info message");
    logger.warn("test.warn", "warn message");
    logger.error("test.error", "error message");
}

// ============================================================
// ID 生成验证
// ============================================================
TEST_F(DiagLogAdapterTest, GenerateTraceId) {
    std::string id1 = generate_trace_id();
    std::string id2 = generate_trace_id();

    EXPECT_NE(id1, id2);
    EXPECT_EQ(id1.substr(0, 11), "diag-trace-");
}

TEST_F(DiagLogAdapterTest, GenerateRequestId) {
    std::string id1 = generate_request_id();
    std::string id2 = generate_request_id();

    EXPECT_NE(id1, id2);
    EXPECT_EQ(id1.substr(0, 9), "diag-req-");
}

// ============================================================
// 十六进制 ID 格式验证（CGW-FW-SPEC设计 §5.4）
// ============================================================
TEST_F(DiagLogAdapterTest, HexIdFormatUint16) {
    EXPECT_EQ(hex_id(static_cast<uint16_t>(0x1110)), "0x1110");
    EXPECT_EQ(hex_id(static_cast<uint16_t>(0x0001)), "0x0001");
    EXPECT_EQ(hex_id(static_cast<uint16_t>(0xF190)), "0xf190");
}

TEST_F(DiagLogAdapterTest, HexIdFormatUint8) {
    EXPECT_EQ(hex_id(static_cast<uint8_t>(0x22)), "0x22");
    EXPECT_EQ(hex_id(static_cast<uint8_t>(0x27)), "0x27");
    EXPECT_EQ(hex_id(static_cast<uint8_t>(0x7F)), "0x7f");
}

TEST_F(DiagLogAdapterTest, FlushWorks) {
    auto logger = DiagLogAdapter::orchestrator();
    logger.info("test.before_flush", "Message before flush");
    logger.flush();
    logger.info("test.after_flush", "Message after flush");
}

} // namespace
} // namespace cgw_diag
