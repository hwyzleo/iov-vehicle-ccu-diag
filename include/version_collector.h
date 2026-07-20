#pragma once

#include "data_models.h"
#include "did_registry.h"
#include "diagnostic_router.h"
#include "uds_client.h"
#include <memory>
#include <optional>

namespace cgw_diag {

class VersionCollector {
public:
    VersionCollector(std::shared_ptr<DidRegistry> registry,
                    std::shared_ptr<DiagnosticRouter> router,
                    std::shared_ptr<TransportAdapter> docan_transport,
                    std::shared_ptr<TransportAdapter> doip_transport);

    VehicleSoftwareSnapshot collectVehicleInventory(const std::string& vin);

    EcuVersionEntry collectEcuVersion(const std::string& ecu_id);

    void setBaselineInfo(const std::string& baseline_id, BaselineSource source);

private:
    std::shared_ptr<DidRegistry> registry_;
    std::shared_ptr<DiagnosticRouter> router_;
    std::shared_ptr<TransportAdapter> docan_transport_;
    std::shared_ptr<TransportAdapter> doip_transport_;

    std::optional<std::string> baseline_id_;
    BaselineSource baseline_source_;

    EcuVersionEntry processEcuVersion(const std::string& ecu_id);
    std::string getCurrentTimestamp();
    CollectionStatus determineOverallResult(const std::vector<EcuVersionEntry>& entries);
};

} // namespace cgw_diag
