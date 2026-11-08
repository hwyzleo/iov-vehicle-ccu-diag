#include "fota_facade.h"
#include "error_codes.h"

namespace ccu_diag {

FotaFacade::FotaFacade(std::shared_ptr<VersionCollector> collector,
                       std::shared_ptr<DidRegistry> registry,
                       std::shared_ptr<VinStore> vin_store,
                       std::shared_ptr<FotaVersionRegistrar> registrar)
    : collector_(std::move(collector)),
      registry_(std::move(registry)),
      vin_store_(std::move(vin_store)),
      registrar_(std::move(registrar)) {
}

VehicleSoftwareSnapshot FotaFacade::collectVehicleInventory() {
    // VIN is now read internally by VersionCollector
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

std::optional<std::string> FotaFacade::readVin() {
    return vin_store_->readVin();
}

InstalledVersionsResponse FotaFacade::recordInstalledVersions(
    const InstalledVersionsRequest& request) {
    if (!registrar_) {
        // Not wired (development/test only): never claim success.
        InstalledVersionsResponse response;
        response.disposition = RegistrationDisposition::RETRYABLE_FAILED;
        response.error_code = ErrorCodeMapper::toDiagCode(
            ErrorCode::REGISTRATION_PERSIST_FAILED);
        return response;
    }
    return registrar_->recordInstalledVersions(request);
}

} // namespace ccu_diag
