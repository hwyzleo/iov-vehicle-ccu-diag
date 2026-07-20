#pragma once

#include <string>
#include <unordered_map>

namespace cgw_diag {

enum class ErrorCode {
    SUCCESS = 0,
    // CGW-DIAG-1001: Target ECU unreachable / no route
    ECU_UNREACHABLE = 1001,
    // CGW-DIAG-1002: UDS request timeout
    REQUEST_TIMEOUT = 1002,
    // CGW-DIAG-1003: ECU returned NRC (negative response)
    NEGATIVE_RESPONSE = 1003,
    // CGW-DIAG-1004: Version DID not supported / missing
    DID_NOT_SUPPORTED = 1004,
    // CGW-DIAG-1005: Baseline tag missing / unknown
    BASELINE_MISSING = 1005,
    // CGW-DIAG-1006: Version data parsing failed
    PARSE_ERROR = 1006,
    // CGW-DIAG-1007: Target ECU not registered in DID list
    ECU_NOT_REGISTERED = 1007
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

} // namespace cgw_diag
