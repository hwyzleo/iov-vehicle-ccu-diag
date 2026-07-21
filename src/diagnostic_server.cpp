#include "diagnostic_server.h"
#include <algorithm>

namespace cgw_diag {

DiagnosticServer::DiagnosticServer(std::shared_ptr<VinStore> vin_store)
    : vin_store_(std::move(vin_store)) {
}

ErrorCode DiagnosticServer::handleSecurityAccess(uint8_t sub_function,
                                                  const std::vector<uint8_t>& request,
                                                  std::vector<uint8_t>& response) {
    // Sub-function 0x01: Request seed
    // Sub-function 0x02: Send key
    if (sub_function == 0x01) {
        // FIXME: CRITICAL — hardcoded seed is a security vulnerability.
        // Replace with cryptographically random seed before production.
        // The seed must be unique per session and unpredictable.
        response.resize(4);
        response[0] = 0x67;  // Positive response
        response[1] = 0x01;  // Sub-function
        response[2] = 0x12;  // Seed byte 1 (HARDCODED — DO NOT SHIP)
        response[3] = 0x34;  // Seed byte 2 (HARDCODED — DO NOT SHIP)
        return ErrorCode::SUCCESS;
    } else if (sub_function == 0x02) {
        // FIXME: CRITICAL — accepts any 4-byte key. This bypasses
        // security entirely. Replace with real seed/key validation
        // (e.g., compute key from seed using shared secret or PKI).
        if (request.size() >= 4) {
            security_level_ = SecurityLevel::LEVEL_1;
            vin_write_session_active_ = true;
            
            response.resize(2);
            response[0] = 0x67;  // Positive response
            response[1] = 0x02;  // Sub-function
            return ErrorCode::SUCCESS;
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
