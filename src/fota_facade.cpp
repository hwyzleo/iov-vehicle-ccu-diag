#include "fota_facade.h"

namespace cgw_diag {

FotaFacade::FotaFacade(std::shared_ptr<VersionCollector> collector,
                       std::shared_ptr<DidRegistry> registry)
    : collector_(collector), registry_(registry) {
}

VehicleSoftwareSnapshot FotaFacade::collectVehicleInventory(const std::string& vin) {
    return collector_->collectVehicleInventory(vin);
}

EcuVersionEntry FotaFacade::getEcuVersion(const std::string& ecu_id) {
    return collector_->collectEcuVersion(ecu_id);
}

std::string FotaFacade::getRegistryVersion() {
    return registry_->getRegistryVersion();
}

} // namespace cgw_diag
