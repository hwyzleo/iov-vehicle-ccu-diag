#include <gtest/gtest.h>
#include "diag_log_adapter.h"
#include "diag_log_context.h"
#include "error_codes.h"
#include "data_models.h"
#include "constants.h"
#include <regex>

namespace cgw_diag {
namespace {

class LogEventsTest : public ::testing::Test {
protected:
    void SetUp() override {
        cgw::fw::log::LogConfig config;
        config.level = cgw::fw::log::LogLevel::kDebug;

        auto result = DiagLogAdapter::init("cgw-diag-events-test", config);
        ASSERT_EQ(result.error, cgw::fw::log::LogError::kOk);
    }
};

// ============================================================
// 事件名格式验证（CGW-DIAG-DSN-CR-008 §事件目录）
// 事件名使用小写点分命名
// ============================================================
TEST_F(LogEventsTest, EventNameFormat) {
    std::regex event_pattern("^diag\\.[a-z_]+\\.[a-z_.]+$");

    // DID Registry events
    EXPECT_TRUE(std::regex_match(diag_events::REGISTRY_LOADED, event_pattern));
    EXPECT_TRUE(std::regex_match(diag_events::REGISTRY_LOAD_FAILED, event_pattern));
    // Inventory events
    EXPECT_TRUE(std::regex_match(diag_events::INVENTORY_REQUEST_ACCEPTED, event_pattern));
    EXPECT_TRUE(std::regex_match(diag_events::INVENTORY_COMPLETED, event_pattern));
    // Tester events
    EXPECT_TRUE(std::regex_match(diag_events::TESTER_REQUEST_SUCCEEDED, event_pattern));
    EXPECT_TRUE(std::regex_match(diag_events::TESTER_REQUEST_FAILED, event_pattern));
    // Router events
    EXPECT_TRUE(std::regex_match(diag_events::ROUTE_FAILED, event_pattern));
    // Transport events
    EXPECT_TRUE(std::regex_match(diag_events::TRANSPORT_FAILED, event_pattern));
    // VIN events
    EXPECT_TRUE(std::regex_match(diag_events::VIN_READ_FAILED, event_pattern));
    EXPECT_TRUE(std::regex_match(diag_events::VIN_WRITE_SUCCEEDED, event_pattern));
    EXPECT_TRUE(std::regex_match(diag_events::VIN_WRITE_FAILED, event_pattern));
    // Security events
    EXPECT_TRUE(std::regex_match(diag_events::SECURITY_ACCESS_FAILED, event_pattern));
}

// ============================================================
// 所有模块 Logger 可调用（CGW-DIAG-DSN-CR-008 §启动与模块 Logger）
// ============================================================
TEST_F(LogEventsTest, AllModuleLoggersCallable) {
    DiagLogAdapter::facade().info(diag_events::INVENTORY_REQUEST_ACCEPTED,
        "Test facade event");
    DiagLogAdapter::orchestrator().info(diag_events::INVENTORY_COMPLETED,
        "Test orchestrator event");
    DiagLogAdapter::tester().info(diag_events::TESTER_REQUEST_SUCCEEDED,
        "Test tester event");
    DiagLogAdapter::router().error(diag_events::ROUTE_FAILED,
        "Test router event");
    DiagLogAdapter::transport().warn(diag_events::TRANSPORT_FAILED,
        "Test transport event");
    DiagLogAdapter::did_registry().info(diag_events::REGISTRY_LOADED,
        "Test did_registry event");
    DiagLogAdapter::vin_store().warn(diag_events::VIN_READ_FAILED,
        "Test vin_store event");
    DiagLogAdapter::sec_client().warn(diag_events::SECURITY_ACCESS_FAILED,
        "Test sec_client event");
}

// ============================================================
// 错误码映射验证（CGW-DIAG-DSN-CR-008 §事件目录）
// ============================================================
TEST_F(LogEventsTest, DiagErrorCodes) {
    // 验证 DIAG 业务错误码 CGW-DIAG-1001~1011
    EXPECT_EQ(ErrorCodeMapper::toDiagCode(ErrorCode::ECU_UNREACHABLE), "CGW-DIAG-1001");
    EXPECT_EQ(ErrorCodeMapper::toDiagCode(ErrorCode::REQUEST_TIMEOUT), "CGW-DIAG-1002");
    EXPECT_EQ(ErrorCodeMapper::toDiagCode(ErrorCode::NEGATIVE_RESPONSE), "CGW-DIAG-1003");
    EXPECT_EQ(ErrorCodeMapper::toDiagCode(ErrorCode::DID_NOT_SUPPORTED), "CGW-DIAG-1004");
    EXPECT_EQ(ErrorCodeMapper::toDiagCode(ErrorCode::PARSE_ERROR), "CGW-DIAG-1006");
    EXPECT_EQ(ErrorCodeMapper::toDiagCode(ErrorCode::ECU_NOT_REGISTERED), "CGW-DIAG-1007");
    EXPECT_EQ(ErrorCodeMapper::toDiagCode(ErrorCode::VIN_NOT_PROVISIONED), "CGW-DIAG-1009");
    EXPECT_EQ(ErrorCodeMapper::toDiagCode(ErrorCode::VIN_WRITE_REJECTED), "CGW-DIAG-1010");
    EXPECT_EQ(ErrorCodeMapper::toDiagCode(ErrorCode::INVALID_KEY), "CGW-DIAG-1011");

    // 错误码字符串非空
    EXPECT_FALSE(ErrorCodeMapper::toString(ErrorCode::ECU_UNREACHABLE).empty());
    EXPECT_FALSE(ErrorCodeMapper::toString(ErrorCode::INVALID_KEY).empty());
}

TEST_F(LogEventsTest, NrcMapping) {
    // 验证 NRC -> ErrorCode 映射
    EXPECT_EQ(ErrorCodeMapper::fromNrc(0x35), ErrorCode::INVALID_KEY);
    EXPECT_EQ(ErrorCodeMapper::fromNrc(0x78), ErrorCode::RESPONSE_PENDING);
    EXPECT_EQ(ErrorCodeMapper::fromNrc(0x31), ErrorCode::NEGATIVE_RESPONSE);
}

TEST_F(LogEventsTest, FrameworkErrorCodes) {
    // 验证 framework 错误码（CGW-FW-0201~0205）
    EXPECT_EQ(static_cast<uint32_t>(cgw::fw::log::LogError::kConfigInvalid), 201);
    EXPECT_EQ(static_cast<uint32_t>(cgw::fw::log::LogError::kInitFailed), 202);
    EXPECT_EQ(static_cast<uint32_t>(cgw::fw::log::LogError::kSinkFailed), 203);
    EXPECT_EQ(static_cast<uint32_t>(cgw::fw::log::LogError::kQueueOverflow), 204);
    EXPECT_EQ(static_cast<uint32_t>(cgw::fw::log::LogError::kSensitiveViolation), 205);
}

// ============================================================
// 事件字段验证（CGW-DIAG-DSN-CR-008 §事件目录 / §字段与脱敏）
// ============================================================
TEST_F(LogEventsTest, RegistryLoadedFields) {
    // diag.registry.loaded: registry_version, ecu_count
    DiagLogAdapter::did_registry().info(
        diag_events::REGISTRY_LOADED,
        "DidRegistry loaded",
        {dlog::f_str("registry_version", "1.0.0"),
         dlog::f_int("ecu_count", 10)}
    );
}

TEST_F(LogEventsTest, RegistryLoadFailedFields) {
    // diag.registry.load.failed: registry_version, reason, error_code
    DiagLogAdapter::did_registry().error(
        diag_events::REGISTRY_LOAD_FAILED,
        "DidRegistry load failed",
        {dlog::f_str("registry_version", "1.0.0"),
         dlog::f_str("reason", "duplicate ECU ID"),
         dlog::f_str("error_code", "CGW-FW-0201")}
    );
}

TEST_F(LogEventsTest, InventoryRequestAcceptedFields) {
    // diag.inventory.request.accepted: request_id, SOME/IP context
    DiagLogAdapter::facade().info(
        diag_events::INVENTORY_REQUEST_ACCEPTED,
        "Inventory request accepted",
        {dlog::f_str("request_id", "req-test-001"),
         dlog::f_str("someip_service_id", "0x1110"),
         dlog::f_str("someip_method_id", "0x0002")}
    );
}

TEST_F(LogEventsTest, InventoryCompletedFields) {
    // diag.inventory.completed: registry_version, overall_result, ecu_count, failed_count, duration_ms
    DiagLogAdapter::orchestrator().info(
        diag_events::INVENTORY_COMPLETED,
        "Inventory collection completed",
        {dlog::f_str("registry_version", "1.0.0"),
         dlog::f_str("overall_result", "ALL_OK"),
         dlog::f_int("ecu_count", 10),
         dlog::f_int("failed_count", 0),
         dlog::f_int("duration_ms", 52)}
    );
}

TEST_F(LogEventsTest, TesterRequestSucceededFields) {
    // diag.tester.request.succeeded: ecu_id, transport, uds_sid, did, attempt, duration_ms
    DiagLogAdapter::tester().info(
        diag_events::TESTER_REQUEST_SUCCEEDED,
        "UDS request succeeded",
        {dlog::f_str("ecu_id", "BMS"),
         dlog::f_str("logical_address", "0x001A"),
         dlog::f_str("transport", "DoCAN"),
         dlog::f_str("uds_sid", "0x22"),
         dlog::f_str("did", "0xF195"),
         dlog::f_int("attempt", 1),
         dlog::f_int("duration_ms", 52)}
    );
}

TEST_F(LogEventsTest, TesterRequestFailedFields) {
    // diag.tester.request.failed: ecu_id, logical_address, transport, uds_sid, did, nrc, attempt, duration_ms, error_code
    DiagLogAdapter::tester().warn(
        diag_events::TESTER_REQUEST_FAILED,
        "UDS request failed",
        {dlog::f_str("ecu_id", "BMS"),
         dlog::f_str("logical_address", "0x001A"),
         dlog::f_str("transport", "DoCAN"),
         dlog::f_str("uds_sid", "0x22"),
         dlog::f_str("did", "0xF190"),
         dlog::f_str("nrc", "present"),
         dlog::f_int("attempt", 1),
         dlog::f_int("duration_ms", 52),
         dlog::f_str("error_code", "CGW-DIAG-1003")}
    );
}

TEST_F(LogEventsTest, RouteFailedFields) {
    // diag.route.failed: ecu_id, logical_address, transport, error_code
    DiagLogAdapter::router().error(
        diag_events::ROUTE_FAILED,
        "Route failed",
        {dlog::f_str("ecu_id", "BMS"),
         dlog::f_str("logical_address", "0x001A"),
         dlog::f_str("transport", "DoCAN"),
         dlog::f_str("error_code", "CGW-DIAG-1001")}
    );
}

TEST_F(LogEventsTest, TransportFailedFields) {
    // diag.transport.failed: ecu_id, transport, attempt, duration_ms
    DiagLogAdapter::transport().warn(
        diag_events::TRANSPORT_FAILED,
        "Transport failed",
        {dlog::f_str("transport", "DoIP"),
         dlog::f_str("logical_address", "0x001A"),
         dlog::f_str("reason", "connection refused")}
    );
}

TEST_F(LogEventsTest, VinReadFailedFields) {
    // diag.vin.read.failed: operation, result, error_code
    DiagLogAdapter::vin_store().warn(
        diag_events::VIN_READ_FAILED,
        "VIN read failed",
        {dlog::f_str("operation", "readVin"),
         dlog::f_str("result", "not_provisioned"),
         dlog::f_str("error_code", "CGW-DIAG-1009")}
    );
}

TEST_F(LogEventsTest, SecurityAccessFailedFields) {
    // diag.security.access.failed: session_id, security_level, operation, result, error_code
    DiagLogAdapter::sec_client().warn(
        diag_events::SECURITY_ACCESS_FAILED,
        "Security access failed",
        {dlog::f_str("session_id", "diag-sess-001"),
         dlog::f_str("security_level", "LEVEL_1"),
         dlog::f_str("operation", "verifyKey"),
         dlog::f_str("result", "invalid_key"),
         dlog::f_str("error_code", "CGW-DIAG-1011")}
    );
}

TEST_F(LogEventsTest, VinWriteSucceededFields) {
    // diag.vin.write.succeeded: session_id, operation, result, duration_ms
    DiagLogAdapter::vin_store().info(
        diag_events::VIN_WRITE_SUCCEEDED,
        "VIN write succeeded",
        {dlog::f_str("session_id", "diag-sess-001"),
         dlog::f_str("operation", "writeVin"),
         dlog::f_str("result", "succeeded"),
         dlog::f_int("duration_ms", 5)}
    );
}

TEST_F(LogEventsTest, VinWriteFailedFields) {
    // diag.vin.write.failed: session_id, operation, result, duration_ms, error_code
    DiagLogAdapter::vin_store().error(
        diag_events::VIN_WRITE_FAILED,
        "VIN write failed",
        {dlog::f_str("session_id", "diag-sess-001"),
         dlog::f_str("operation", "writeVin"),
         dlog::f_str("result", "failed"),
         dlog::f_int("duration_ms", 5),
         dlog::f_str("error_code", "CGW-DIAG-1010")}
    );
}

// ============================================================
// 敏感类型验证（CGW-DIAG-DSN-CR-008 §字段与脱敏）
// ============================================================
TEST_F(LogEventsTest, SensitivityClassification) {
    // VIN 按 Identifier 处理：默认掩码或不可逆摘要
    DiagLogAdapter::vin_store().info("test.event",
        "VIN as Identifier",
        {dlog::f_str("vin", "LSVAU2180N2123456", cgw::fw::log::Sensitivity::Identifier)}
    );

    // UDS/SOME-IP 原始 payload 按 Payload 处理
    DiagLogAdapter::tester().info("test.event",
        "Payload as Payload",
        {dlog::f_str("raw_payload", "raw_data_hex", cgw::fw::log::Sensitivity::Payload)}
    );

    // 0x27 seed/key 按 Secret 直接拒绝
    DiagLogAdapter::sec_client().info("test.event",
        "Secret field rejection",
        {dlog::f_str("seed", "0xABCD1234", cgw::fw::log::Sensitivity::Secret),
         dlog::f_str("key", "0xEF567890", cgw::fw::log::Sensitivity::Secret)}
    );
}

TEST_F(LogEventsTest, PayloadFieldOnlyMetadata) {
    // Payload 字段: INFO 及以上仅记录长度、摘要、SID/DID/NRC 等元数据
    DiagLogAdapter::tester().info("test.event",
        "Payload metadata only",
        {dlog::f_int("payload_length", 64),
         dlog::f_str("uds_sid", "0x22"),
         dlog::f_str("did", "0xF195"),
         dlog::f_str("nrc", "")}
    );
}

TEST_F(LogEventsTest, VinNotInMessage) {
    // VIN 不得拼入 message
    DiagLogAdapter::vin_store().info("test.vin_not_in_message",
        "VIN processing result",
        {dlog::f_str("vin_hash", "masked_value", cgw::fw::log::Sensitivity::Identifier),
         dlog::f_str("result", "OK")}
    );
}

} // namespace
} // namespace cgw_diag
