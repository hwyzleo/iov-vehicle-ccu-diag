#include "diagnostic_server.h"
#include <algorithm>

namespace cgw_diag {

DiagnosticServer::DiagnosticServer(std::shared_ptr<VinStore> vin_store,
                                   std::shared_ptr<CgwSecClientProxy> sec_client_proxy)
    : vin_store_(std::move(vin_store))
    , sec_client_proxy_(std::move(sec_client_proxy)) {
}

ErrorCode DiagnosticServer::handleSecurityAccess(uint8_t sub_function,
                                                  const std::vector<uint8_t>& request,
                                                  std::vector<uint8_t>& response) {
    // Check lockout status
    if (is_locked_out_) {
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            now - lockout_start_).count();
        if (elapsed < LOCKOUT_DURATION_MS) {
            return ErrorCode::VIN_WRITE_REJECTED;
        }
        // Lockout period expired, reset
        is_locked_out_ = false;
        failed_attempts_ = 0;
    }

    // Sub-function 0x01: Request seed
    if (sub_function == 0x01) {
        // Get seed from CGW-SEC
        auto seed_result = sec_client_proxy_->getSeed(SecLevel::LEVEL_1);
        if (!seed_result.has_value()) {
            return ErrorCode::VIN_WRITE_REJECTED;
        }
        
        // Store seed_id for key verification
        seed_id_ = seed_result->seed_id;
        
        // Build response: positive response + seed
        response.resize(2 + seed_result->seed.size());
        response[0] = 0x67;  // Positive response
        response[1] = 0x01;  // Sub-function
        std::copy(seed_result->seed.begin(), seed_result->seed.end(), 
                  response.begin() + 2);
        
        return ErrorCode::SUCCESS;
    } 
    // Sub-function 0x02: Send key
    else if (sub_function == 0x02) {
        // Extract key from request (skip sub-function byte)
        std::vector<uint8_t> key(request.begin() + 1, request.end());
        
        // Verify key with CGW-SEC
        auto verify_result = sec_client_proxy_->verifyKey(
            SecLevel::LEVEL_1, seed_id_, key);
        
        if (verify_result == SecResult::SUCCESS) {
            // Reset failed attempts on success
            failed_attempts_ = 0;
            security_level_ = SecurityLevel::LEVEL_1;
            vin_write_session_active_ = true;
            
            response.resize(2);
            response[0] = 0x67;  // Positive response
            response[1] = 0x02;  // Sub-function
            return ErrorCode::SUCCESS;
        } else if (verify_result == SecResult::INVALID_KEY) {
            // Increment failed attempts
            failed_attempts_++;
            
            // Check if lockout should be triggered
            if (failed_attempts_ >= MAX_FAILED_ATTEMPTS) {
                is_locked_out_ = true;
                lockout_start_ = std::chrono::steady_clock::now();
            }
            
            return ErrorCode::INVALID_KEY;
        } else {
            // Other errors (service unavailable, timeout, etc.)
            return ErrorCode::VIN_WRITE_REJECTED;
        }
    }
    
    return ErrorCode::VIN_WRITE_REJECTED;
}

ErrorCode DiagnosticServer::handleWriteDataByIdentifier(uint16_t did,
                                                         const std::vector<uint8_t>& data,
                                                         std::vector<uint8_t>& response) {
    // Only allow VIN write in active VIN write session
    if (!vin_write_session_active_ || security_level_ != SecurityLevel::LEVEL_1) {
        return ErrorCode::VIN_WRITE_REJECTED;
    }
    
    // Only allow VIN DID (F190)
    if (did != DID_VIN) {
        return ErrorCode::VIN_WRITE_REJECTED;
    }
    
    // Validate VIN data
    if (!validateVin(data)) {
        return ErrorCode::VIN_WRITE_REJECTED;
    }
    
    // Convert to string
    std::string vin(data.begin(), data.end());
    
    // Write to VIN store
    if (!vin_store_->writeVin(vin)) {
        return ErrorCode::VIN_WRITE_REJECTED;
    }
    
    // Success response
    response.resize(3);
    response[0] = 0x6E;  // Positive response for WriteDataByIdentifier
    response[1] = (DID_VIN >> 8) & 0xFF;  // DID high byte
    response[2] = DID_VIN & 0xFF;  // DID low byte
    
    return ErrorCode::SUCCESS;
}

bool DiagnosticServer::isVinWriteSessionActive() const {
    return vin_write_session_active_;
}

SecurityLevel DiagnosticServer::getSecurityLevel() const {
    return security_level_;
}

void DiagnosticServer::resetSession() {
    security_level_ = SecurityLevel::LOCKED;
    vin_write_session_active_ = false;
    failed_attempts_ = 0;
    is_locked_out_ = false;
    seed_id_ = 0;
}

bool DiagnosticServer::isLockedOut() const {
    if (!is_locked_out_) {
        return false;
    }
    
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        now - lockout_start_).count();
    
    return elapsed < LOCKOUT_DURATION_MS;
}

bool DiagnosticServer::validateVin(const std::vector<uint8_t>& data) const {
    // Check VIN length (17 bytes)
    if (data.size() != VinStore::VIN_LENGTH) {
        return false;
    }
    
    // Convert to string for validation
    std::string vin(data.begin(), data.end());
    return VinStore::isValidVin(vin);
}

} // namespace cgw_diag
