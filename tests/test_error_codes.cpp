#include <gtest/gtest.h>
#include "error_codes.h"

namespace cgw_diag {
namespace tests {

TEST(ErrorCodeMapperTest, ToStringReturnsCorrectString) {
    EXPECT_EQ(ErrorCodeMapper::toString(ErrorCode::SUCCESS), "Success");
    EXPECT_EQ(ErrorCodeMapper::toString(ErrorCode::ECU_UNREACHABLE), "Target ECU unreachable or no route");
    EXPECT_EQ(ErrorCodeMapper::toString(ErrorCode::REQUEST_TIMEOUT), "UDS request timeout");
    EXPECT_EQ(ErrorCodeMapper::toString(ErrorCode::NEGATIVE_RESPONSE), "ECU returned negative response");
    EXPECT_EQ(ErrorCodeMapper::toString(ErrorCode::DID_NOT_SUPPORTED), "Version DID not supported or missing");
    EXPECT_EQ(ErrorCodeMapper::toString(ErrorCode::BASELINE_MISSING), "Baseline tag missing or unknown");
    EXPECT_EQ(ErrorCodeMapper::toString(ErrorCode::PARSE_ERROR), "Version data parsing failed");
    EXPECT_EQ(ErrorCodeMapper::toString(ErrorCode::ECU_NOT_REGISTERED), "Target ECU not registered in DID list");
    EXPECT_EQ(ErrorCodeMapper::toString(ErrorCode::RESPONSE_PENDING), "ECU response pending, continue polling");
    EXPECT_EQ(ErrorCodeMapper::toString(ErrorCode::VIN_NOT_PROVISIONED), "VIN not provisioned or missing");
    EXPECT_EQ(ErrorCodeMapper::toString(ErrorCode::VIN_WRITE_REJECTED), "VIN write rejected (unlock failed, validation failed, or unauthorized rewrite)");
}

TEST(ErrorCodeMapperTest, ToDiagCodeReturnsCorrectCode) {
    EXPECT_EQ(ErrorCodeMapper::toDiagCode(ErrorCode::SUCCESS), "SUCCESS");
    EXPECT_EQ(ErrorCodeMapper::toDiagCode(ErrorCode::ECU_UNREACHABLE), "CGW-DIAG-1001");
    EXPECT_EQ(ErrorCodeMapper::toDiagCode(ErrorCode::REQUEST_TIMEOUT), "CGW-DIAG-1002");
    EXPECT_EQ(ErrorCodeMapper::toDiagCode(ErrorCode::NEGATIVE_RESPONSE), "CGW-DIAG-1003");
    EXPECT_EQ(ErrorCodeMapper::toDiagCode(ErrorCode::DID_NOT_SUPPORTED), "CGW-DIAG-1004");
    EXPECT_EQ(ErrorCodeMapper::toDiagCode(ErrorCode::BASELINE_MISSING), "CGW-DIAG-1005");
    EXPECT_EQ(ErrorCodeMapper::toDiagCode(ErrorCode::PARSE_ERROR), "CGW-DIAG-1006");
    EXPECT_EQ(ErrorCodeMapper::toDiagCode(ErrorCode::ECU_NOT_REGISTERED), "CGW-DIAG-1007");
    EXPECT_EQ(ErrorCodeMapper::toDiagCode(ErrorCode::RESPONSE_PENDING), "CGW-DIAG-1008");
    EXPECT_EQ(ErrorCodeMapper::toDiagCode(ErrorCode::VIN_NOT_PROVISIONED), "CGW-DIAG-1009");
    EXPECT_EQ(ErrorCodeMapper::toDiagCode(ErrorCode::VIN_WRITE_REJECTED), "CGW-DIAG-1010");
}

TEST(ErrorCodeMapperTest, ToStringUnknownCodeReturnsFallback) {
    auto unknown = static_cast<ErrorCode>(9999);
    EXPECT_EQ(ErrorCodeMapper::toString(unknown), "Unknown error");
}

TEST(ErrorCodeMapperTest, ToDiagCodeUnknownCodeReturnsFallback) {
    auto unknown = static_cast<ErrorCode>(9999);
    EXPECT_EQ(ErrorCodeMapper::toDiagCode(unknown), "UNKNOWN");
}

TEST(ErrorCodeMapperTest, FromNrcMapsCorrectly) {
    // Test NRC 0x31 (RequestOutOfRange)
    EXPECT_EQ(ErrorCodeMapper::fromNrc(0x31), ErrorCode::NEGATIVE_RESPONSE);
    
    // Test NRC 0x22 (ConditionsNotCorrect)
    EXPECT_EQ(ErrorCodeMapper::fromNrc(0x22), ErrorCode::NEGATIVE_RESPONSE);
    
    // Test NRC 0x78 (ResponsePending) - should map to response pending
    EXPECT_EQ(ErrorCodeMapper::fromNrc(0x78), ErrorCode::RESPONSE_PENDING);
    
    // Test unknown NRC
    EXPECT_EQ(ErrorCodeMapper::fromNrc(0xFF), ErrorCode::NEGATIVE_RESPONSE);
}

} // namespace tests
} // namespace cgw_diag
