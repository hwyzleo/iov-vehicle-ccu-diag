#pragma once

#include "transport_adapter.h"
#include <string>

namespace cgw_diag {

class DoCanTransport : public TransportAdapter {
public:
    DoCanTransport(const std::string& interface_name, uint32_t baudrate);
    ~DoCanTransport() override = default;
    
    TransportResponse sendRequest(uint16_t logical_address, 
                                 const std::vector<uint8_t>& request) override;
    bool isReachable(uint16_t logical_address) override;
    std::string getTransportType() const override;
    
private:
    std::string interface_name_;
    uint32_t baudrate_;
    
    // Helper methods for CAN-FD frame handling
    std::vector<uint8_t> createCanFrame(uint16_t can_id, const std::vector<uint8_t>& data);
    std::vector<uint8_t> extractResponse(const std::vector<uint8_t>& frame);
};

} // namespace cgw_diag