#include "fota_facade.h"
#include "error_codes.h"

namespace cgw_diag {

FotaFacade::FotaFacade(std::shared_ptr<VersionCollector> collector,
                       std::shared_ptr<DidRegistry> registry)
    : collector_(collector), registry_(registry) {
}

VehicleSoftwareSnapshot FotaFacade::collectVehicleInventory() {
    return collector_->collectVehicleInventory();
}

EcuVersionEntry FotaFacade::getEcuVersion(const std::string& ecu_id) {
    if (ecu_id.empty()) {
        EcuVersionEntry entry;
        entry.ecu_id = ecu_id;
        entry.status = EcuStatus::MISSING;
        entry.error_code = ErrorCodeMapper::toDiagCode(ErrorCode::ECU_NOT_REGISTERED);
        return entry;
    }
    return collector_->collectEcuVersion(ecu_id);
}

std::string FotaFacade::getRegistryVersion() {
    return registry_->getRegistryVersion();
}

void FotaFacade::setBaselineInfo(const std::string& baseline_id, BaselineSource source) {
    collector_->setBaselineInfo(baseline_id, source);
}

} // namespace cgw_diag
