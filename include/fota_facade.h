#pragma once

#include "data_models.h"
#include "fota_version_registrar.h"
#include "version_collector.h"
#include "vin_store.h"
#include <memory>
#include <string>
#include <optional>

namespace ccu_diag {

class FotaFacade {
public:
    FotaFacade(std::shared_ptr<VersionCollector> collector,
               std::shared_ptr<DidRegistry> registry,
               std::shared_ptr<VinStore> vin_store,
               std::shared_ptr<FotaVersionRegistrar> registrar = nullptr);

    VehicleSoftwareSnapshot collectVehicleInventory();
    EcuVersionEntry getEcuVersion(const std::string& ecu_id);
    std::string getRegistryVersion();
    void setBaselineInfo(const std::string& baseline_id, BaselineSource source);
    std::optional<std::string> readVin();

    // Thin facade entry (CCU-DIAG-DSN-CR-010): no business rules here, all
    // validation/idempotency/atomic commit lives in FotaVersionRegistrar.
    InstalledVersionsResponse recordInstalledVersions(
        const InstalledVersionsRequest& request);

private:
    std::shared_ptr<VersionCollector> collector_;
    std::shared_ptr<DidRegistry> registry_;
    std::shared_ptr<VinStore> vin_store_;
    std::shared_ptr<FotaVersionRegistrar> registrar_;
};

} // namespace ccu_diag
