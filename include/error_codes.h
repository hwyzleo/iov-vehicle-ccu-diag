#pragma once

#include <string>
#include <unordered_map>

namespace ccu_diag {

enum class ErrorCode {
    SUCCESS = 0,
    // CCU-DIAG-1001: Target ECU unreachable / no route
    ECU_UNREACHABLE = 1001,
    // CCU-DIAG-1002: UDS request timeout
    REQUEST_TIMEOUT = 1002,
    // CCU-DIAG-1003: ECU returned NRC (negative response)
    NEGATIVE_RESPONSE = 1003,
    // CCU-DIAG-1004: Version DID not supported / missing
    DID_NOT_SUPPORTED = 1004,
    // CCU-DIAG-1005: Baseline tag missing / unknown
    BASELINE_MISSING = 1005,
    // CCU-DIAG-1006: Version data parsing failed
    PARSE_ERROR = 1006,
    // CCU-DIAG-1007: Target ECU not registered in DID list
    ECU_NOT_REGISTERED = 1007,
    // CCU-DIAG-1008: Response pending (NRC 0x78), client should continue polling
    RESPONSE_PENDING = 1008,
    // CCU-DIAG-1009: VIN not provisioned / missing
    VIN_NOT_PROVISIONED = 1009,
    // CCU-DIAG-1010: VIN write rejected
    VIN_WRITE_REJECTED = 1010,
    // CCU-DIAG-1011: Invalid key (NRC 0x35)
    INVALID_KEY = 1011
};

class ErrorCodeMapper {
public:
    static std::string toString(ErrorCode code);
    static std::string toDiagCode(ErrorCode code);
    static ErrorCode fromNrc(uint8_t nrc);
    
private:
    static const std::unordered_map<ErrorCode, std::string> error_strings_;
    static const std::unordered_map<ErrorCode, std::string> diag_codes_;
};

} // namespace ccu_diag
