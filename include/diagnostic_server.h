#pragma once

#include "vin_store.h"
#include "data_models.h"
#include "error_codes.h"
#include "cgw_sec_client_proxy.h"
#include <memory>
#include <chrono>

namespace cgw_diag {

// UDS Service IDs for diagnostic server
constexpr uint8_t UDS_SECURITY_ACCESS = 0x27;
constexpr uint8_t UDS_WRITE_DATA_BY_IDENTIFIER = 0x2E;
constexpr uint16_t DID_VIN = 0xF190;

// Security levels
enum class SecurityLevel {
    LOCKED = 0,
    LEVEL_1 = 1
};

class DiagnosticServer {
public:
    DiagnosticServer(std::shared_ptr<VinStore> vin_store,
                     std::shared_ptr<CgwSecClientProxy> sec_client_proxy);
    ~DiagnosticServer() = default;
    
    // Handle UDS Security Access (0x27)
    ErrorCode handleSecurityAccess(uint8_t sub_function, 
                                   const std::vector<uint8_t>& request,
                                   std::vector<uint8_t>& response);
    
    // Handle UDS Write Data By Identifier (0x2E)
    ErrorCode handleWriteDataByIdentifier(uint16_t did,
                                          const std::vector<uint8_t>& data,
                                          std::vector<uint8_t>& response);
    
    // Check if VIN write session is active
    bool isVinWriteSessionActive() const;
    
    // Get current security level
    SecurityLevel getSecurityLevel() const;
    
    // Reset session (for timeout or explicit logout)
    void resetSession();
    
    // Check if locked out due to failed attempts
    bool isLockedOut() const;

private:
    bool validateVin(const std::vector<uint8_t>& data) const;
    
    std::shared_ptr<VinStore> vin_store_;
    std::shared_ptr<CgwSecClientProxy> sec_client_proxy_;
    SecurityLevel security_level_ = SecurityLevel::LOCKED;
    bool vin_write_session_active_ = false;
    
    // Security access state for CGW-SEC integration
    uint32_t seed_id_ = 0;
    uint8_t failed_attempts_ = 0;
    static constexpr uint8_t MAX_FAILED_ATTEMPTS = 3;
    static constexpr uint32_t LOCKOUT_DURATION_MS = 10000;  // 10 seconds
    std::chrono::steady_clock::time_point lockout_start_;
    bool is_locked_out_ = false;
};

} // namespace cgw_diag
