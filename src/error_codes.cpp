#include "error_codes.h"

namespace cgw_diag {

const std::unordered_map<ErrorCode, std::string> ErrorCodeMapper::error_strings_ = {
    {ErrorCode::SUCCESS, "Success"},
    {ErrorCode::ECU_UNREACHABLE, "Target ECU unreachable or no route"},
    {ErrorCode::REQUEST_TIMEOUT, "UDS request timeout"},
    {ErrorCode::NEGATIVE_RESPONSE, "ECU returned negative response"},
    {ErrorCode::DID_NOT_SUPPORTED, "Version DID not supported or missing"},
    {ErrorCode::BASELINE_MISSING, "Baseline tag missing or unknown"},
    {ErrorCode::PARSE_ERROR, "Version data parsing failed"},
    {ErrorCode::ECU_NOT_REGISTERED, "Target ECU not registered in DID list"}
};

const std::unordered_map<ErrorCode, std::string> ErrorCodeMapper::diag_codes_ = {
    {ErrorCode::SUCCESS, "SUCCESS"},
    {ErrorCode::ECU_UNREACHABLE, "CGW-DIAG-1001"},
    {ErrorCode::REQUEST_TIMEOUT, "CGW-DIAG-1002"},
    {ErrorCode::NEGATIVE_RESPONSE, "CGW-DIAG-1003"},
    {ErrorCode::DID_NOT_SUPPORTED, "CGW-DIAG-1004"},
    {ErrorCode::BASELINE_MISSING, "CGW-DIAG-1005"},
    {ErrorCode::PARSE_ERROR, "CGW-DIAG-1006"},
    {ErrorCode::ECU_NOT_REGISTERED, "CGW-DIAG-1007"}
};

std::string ErrorCodeMapper::toString(ErrorCode code) {
    auto it = error_strings_.find(code);
    if (it != error_strings_.end()) {
        return it->second;
    }
    return "Unknown error";
}

std::string ErrorCodeMapper::toDiagCode(ErrorCode code) {
    auto it = diag_codes_.find(code);
    if (it != diag_codes_.end()) {
        return it->second;
    }
    return "UNKNOWN";
}

ErrorCode ErrorCodeMapper::fromNrc(uint8_t nrc) {
    // Map UDS NRC to our error codes
    // This is a simplified mapping - real implementation would be more comprehensive
    switch (nrc) {
        case 0x31: // RequestOutOfRange
        case 0x22: // ConditionsNotCorrect
            return ErrorCode::NEGATIVE_RESPONSE;
        case 0x78: // ResponsePending
            return ErrorCode::REQUEST_TIMEOUT;
        default:
            return ErrorCode::NEGATIVE_RESPONSE;
    }
}

} // namespace cgw_diag
