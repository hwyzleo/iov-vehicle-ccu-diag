#pragma once

#include "data_models.h"
#include "version_collector.h"
#include "vin_store.h"
#include <memory>
#include <string>
#include <optional>

namespace cgw_diag {

class FotaFacade {
public:
    FotaFacade(std::shared_ptr<VersionCollector> collector,
               std::shared_ptr<DidRegistry> registry,
               std::shared_ptr<VinStore> vin_store);

    VehicleSoftwareSnapshot collectVehicleInventory();
    EcuVersionEntry getEcuVersion(const std::string& ecu_id);
    std::string getRegistryVersion();
    void setBaselineInfo(const std::string& baseline_id, BaselineSource source);
    std::optional<std::string> readVin();

private:
    std::shared_ptr<VersionCollector> collector_;
    std::shared_ptr<DidRegistry> registry_;
    std::shared_ptr<VinStore> vin_store_;
};

} // namespace cgw_diag
