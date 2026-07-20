#pragma once

#include "data_models.h"
#include "version_collector.h"
#include <memory>
#include <string>

namespace cgw_diag {

class FotaFacade {
public:
    FotaFacade(std::shared_ptr<VersionCollector> collector,
               std::shared_ptr<DidRegistry> registry);

    VehicleSoftwareSnapshot collectVehicleInventory(const std::string& vin);
    EcuVersionEntry getEcuVersion(const std::string& ecu_id);
    std::string getRegistryVersion();

private:
    std::shared_ptr<VersionCollector> collector_;
    std::shared_ptr<DidRegistry> registry_;
};

} // namespace cgw_diag
