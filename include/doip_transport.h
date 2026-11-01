#pragma once

#include "transport_adapter.h"
#include <string>
#include <netinet/in.h>

namespace ccu_diag {

class DoIpTransport : public TransportAdapter {
public:
    DoIpTransport(const std::string& ip_address, uint16_t port);
    ~DoIpTransport() override;
    
    TransportResponse sendRequest(uint16_t logical_address, 
                                 const std::vector<uint8_t>& request) override;
    bool isReachable(uint16_t logical_address) override;
    std::string getTransportType() const override;
    
private:
    std::string ip_address_;
    uint16_t port_;
    int socket_fd_;
    bool connected_;
    struct sockaddr_in server_addr_;
    
    bool connectToServer();
    void disconnect();
    std::vector<uint8_t> createDoIpPacket(uint16_t logical_address, 
                                          const std::vector<uint8_t>& payload);
};

} // namespace ccu_diag
