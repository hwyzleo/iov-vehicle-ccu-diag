#pragma once

#include <vector>
#include <cstdint>
#include <optional>
#include <string>

namespace cgw_diag {

struct TransportResponse {
    bool success;
    std::vector<uint8_t> data;
    std::string error_message;
};

class TransportAdapter {
public:
    virtual ~TransportAdapter() = default;
    
    // Send UDS request and receive response
    virtual TransportResponse sendRequest(uint16_t logical_address, 
                                         const std::vector<uint8_t>& request) = 0;
    
    // Check if target is reachable
    virtual bool isReachable(uint16_t logical_address) = 0;
    
    // Get transport type name
    virtual std::string getTransportType() const = 0;
};

} // namespace cgw_diag