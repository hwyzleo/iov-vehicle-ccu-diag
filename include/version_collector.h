#pragma once

#include "data_models.h"
#include "did_registry.h"
#include "diagnostic_router.h"
#include "uds_client.h"
#include "vin_store.h"
#include <memory>
#include <optional>

namespace ccu_diag {

// Thread safety: This class is thread-safe. All public methods can be called
// concurrently from multiple threads. Internal state (baseline_id_, baseline_source_)
// is protected by the fact that setBaselineInfo() should be called before
// collectVehicleInventory() during initialization, not concurrently.
class VersionCollector {
public:
    VersionCollector(std::shared_ptr<DidRegistry> registry,
                    std::shared_ptr<DiagnosticRouter> router,
                    std::shared_ptr<TransportAdapter> docan_transport,
                    std::shared_ptr<TransportAdapter> doip_transport,
                    std::shared_ptr<VinStore> vin_store);

    VehicleSoftwareSnapshot collectVehicleInventory();

    EcuVersionEntry collectEcuVersion(const std::string& ecu_id);

    void setBaselineInfo(const std::string& baseline_id, BaselineSource source);

private:
    std::shared_ptr<DidRegistry> registry_;
    std::shared_ptr<DiagnosticRouter> router_;
    std::shared_ptr<TransportAdapter> docan_transport_;
    std::shared_ptr<TransportAdapter> doip_transport_;
    std::shared_ptr<VinStore> vin_store_;

    std::optional<std::string> baseline_id_;
    BaselineSource baseline_source_;

    EcuVersionEntry processEcuVersion(const std::string& ecu_id);
    std::string getCurrentTimestamp();
    CollectionStatus determineOverallResult(const std::vector<EcuVersionEntry>& entries);
};

} // namespace ccu_diag
