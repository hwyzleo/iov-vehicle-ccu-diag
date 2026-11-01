#pragma once

#include "someip_client_proxy.h"
#include "data_models.h"
#include "someip_types.h"
#include <memory>
#include <string>

namespace ccu_diag {

class FotaSomeIpProxy {
public:
    FotaSomeIpProxy(std::unique_ptr<SomeIpClientProxy> proxy);

    bool initialize(const SomeIpServiceConfig& remote_service_config);
    bool connect();
    void disconnect();
    bool isConnected() const;

    VehicleSoftwareSnapshot collectVehicleInventory();
    EcuVersionEntry getEcuVersion(const std::string& ecu_id);
    std::string getRegistryVersion();

    void setRequestTimeout(uint32_t timeout_ms);

private:
    std::unique_ptr<SomeIpClientProxy> proxy_;
};

} // namespace ccu_diag