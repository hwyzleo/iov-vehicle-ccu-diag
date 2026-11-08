#include "error_codes.h"
#include <unordered_map>

namespace ccu_diag {

const std::unordered_map<ErrorCode, std::string> ErrorCodeMapper::error_strings_ = {
    {ErrorCode::SUCCESS, "Success"},
    {ErrorCode::ECU_UNREACHABLE, "Target ECU unreachable or no route"},
    {ErrorCode::REQUEST_TIMEOUT, "UDS request timeout"},
    {ErrorCode::NEGATIVE_RESPONSE, "ECU returned negative response"},
    {ErrorCode::DID_NOT_SUPPORTED, "Version DID not supported or missing"},
    {ErrorCode::BASELINE_MISSING, "Baseline tag missing or unknown"},
    {ErrorCode::PARSE_ERROR, "Version data parsing failed"},
    {ErrorCode::ECU_NOT_REGISTERED, "Target ECU not registered in DID list"},
    {ErrorCode::RESPONSE_PENDING, "ECU response pending, continue polling"},
    {ErrorCode::VIN_NOT_PROVISIONED, "VIN not provisioned or missing"},
    {ErrorCode::VIN_WRITE_REJECTED, "VIN write rejected (unlock failed, validation failed, or unauthorized rewrite)"},
    {ErrorCode::INVALID_KEY, "Invalid key provided for security access"},
    {ErrorCode::INVENTORY_STATE_UNAVAILABLE, "Inventory state unavailable or corrupt"},
    {ErrorCode::INVENTORY_SEED_INVALID, "Inventory seed missing, invalid or inconsistent"},
    {ErrorCode::REGISTRATION_INVALID, "Installed version registration request invalid"},
    {ErrorCode::REGISTRATION_PERSIST_FAILED, "Installed version registration persistence failed"},
    {ErrorCode::REGISTRATION_IDEMPOTENCY_CONFLICT, "Installed version registration idempotency conflict"}
};

const std::unordered_map<ErrorCode, std::string> ErrorCodeMapper::diag_codes_ = {
    {ErrorCode::SUCCESS, "SUCCESS"},
    {ErrorCode::ECU_UNREACHABLE, "CCU-DIAG-1001"},
    {ErrorCode::REQUEST_TIMEOUT, "CCU-DIAG-1002"},
    {ErrorCode::NEGATIVE_RESPONSE, "CCU-DIAG-1003"},
    {ErrorCode::DID_NOT_SUPPORTED, "CCU-DIAG-1004"},
    {ErrorCode::BASELINE_MISSING, "CCU-DIAG-1005"},
    {ErrorCode::PARSE_ERROR, "CCU-DIAG-1006"},
    {ErrorCode::ECU_NOT_REGISTERED, "CCU-DIAG-1007"},
    {ErrorCode::RESPONSE_PENDING, "CCU-DIAG-1008"},
    {ErrorCode::VIN_NOT_PROVISIONED, "CCU-DIAG-1009"},
    {ErrorCode::VIN_WRITE_REJECTED, "CCU-DIAG-1010"},
    {ErrorCode::INVALID_KEY, "CCU-DIAG-1011"},
    {ErrorCode::INVENTORY_STATE_UNAVAILABLE, "CCU-DIAG-1012"},
    {ErrorCode::INVENTORY_SEED_INVALID, "CCU-DIAG-1013"},
    {ErrorCode::REGISTRATION_INVALID, "CCU-DIAG-1014"},
    {ErrorCode::REGISTRATION_PERSIST_FAILED, "CCU-DIAG-1015"},
    {ErrorCode::REGISTRATION_IDEMPOTENCY_CONFLICT, "CCU-DIAG-1016"}
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
        case 0x35: // InvalidKey
            return ErrorCode::INVALID_KEY;
        case 0x78: // ResponsePending
            return ErrorCode::RESPONSE_PENDING;
        default:
            return ErrorCode::NEGATIVE_RESPONSE;
    }
}

} // namespace ccu_diag
