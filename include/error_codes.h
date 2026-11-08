#pragma once

#include <string>
#include <unordered_map>
#include <stdexcept>

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
    INVALID_KEY = 1011,
    // CCU-DIAG-1012: Inventory state unavailable / corrupt (never answer with
    // an empty list success - CCU-DIAG-DSN-CR-010 D9)
    INVENTORY_STATE_UNAVAILABLE = 1012,
    // CCU-DIAG-1013: seed missing / invalid / unknown version / digest mismatch
    INVENTORY_SEED_INVALID = 1013,
    // CCU-DIAG-1014: registration request invalid (missing field, unknown ECU,
    // invalid version)
    REGISTRATION_INVALID = 1014,
    // CCU-DIAG-1015: registration persistence failed (retryable)
    REGISTRATION_PERSIST_FAILED = 1015,
    // CCU-DIAG-1016: registration idempotency conflict (same key, new content)
    REGISTRATION_IDEMPOTENCY_CONFLICT = 1016
};

// Carries a stable CCU-DIAG error code across layering boundaries without
// exposing internal detail (CCU-DIAG-DSN-CR-010 D9).
class DiagException : public std::runtime_error {
public:
    DiagException(ErrorCode code, const std::string& message)
        : std::runtime_error(message), code_(code) {}

    ErrorCode code() const { return code_; }

private:
    ErrorCode code_;
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
