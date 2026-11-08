#pragma once

#include "data_models.h"
#include "did_registry.h"
#include "diagnostic_router.h"
#include "ecu_inventory_store.h"
#include "uds_client.h"
#include "vin_store.h"
#include <memory>
#include <optional>

namespace ccu_diag {

// Thread safety: This class is thread-safe. All public methods can be called
// concurrently from multiple threads. Internal state (baseline_id_, baseline_source_)
// is protected by the fact that setBaselineInfo() should be called before
// collectVehicleInventory() during initialization, not concurrently.
//
// Source mode (CCU-DIAG-DSN-CR-010 §3/D4):
//   * HARDWARE: the existing per-ECU UDS 0x22 / SOME-IP getVersion read path.
//   * SEED: no downlink diagnostics; facts come from EcuInventoryStore.
// The mode is owned by the store configuration and is mutually exclusive.
class VersionCollector {
public:
    VersionCollector(std::shared_ptr<DidRegistry> registry,
                    std::shared_ptr<DiagnosticRouter> router,
                    std::shared_ptr<TransportAdapter> docan_transport,
                    std::shared_ptr<TransportAdapter> doip_transport,
                    std::shared_ptr<VinStore> vin_store,
                    std::shared_ptr<EcuInventoryStore> inventory_store = nullptr);

    VehicleSoftwareSnapshot collectVehicleInventory();

    EcuVersionEntry collectEcuVersion(const std::string& ecu_id);

    void setBaselineInfo(const std::string& baseline_id, BaselineSource source);

private:
    std::shared_ptr<DidRegistry> registry_;
    std::shared_ptr<DiagnosticRouter> router_;
    std::shared_ptr<TransportAdapter> docan_transport_;
    std::shared_ptr<TransportAdapter> doip_transport_;
    std::shared_ptr<VinStore> vin_store_;
    std::shared_ptr<EcuInventoryStore> inventory_store_;

    std::optional<std::string> baseline_id_;
    BaselineSource baseline_source_;

    bool isSeedMode() const;
    VehicleSoftwareSnapshot collectFromHardware();
    VehicleSoftwareSnapshot collectFromSeed();
    EcuVersionEntry seedEcuVersion(const InventoryEcuFact& fact);
    EcuVersionEntry processEcuVersion(const std::string& ecu_id);
    void fillVin(VehicleSoftwareSnapshot& snapshot);
    void logCollectionCompleted(const VehicleSoftwareSnapshot& snapshot,
                                int64_t duration_ms);
    std::string getCurrentTimestamp();
    CollectionStatus determineOverallResult(const std::vector<EcuVersionEntry>& entries);
};

} // namespace ccu_diag
