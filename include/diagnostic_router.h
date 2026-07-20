#pragma once

#include "data_models.h"
#include "error_codes.h"
#include "did_registry.h"
#include "uds_client.h"
#include "transport_adapter.h"
#include <memory>
#include <optional>

namespace cgw_diag {

struct RoutingResult {
    bool success = false;
    std::vector<uint8_t> response_data;
    ErrorCode error_code = ErrorCode::SUCCESS;
    std::string error_message;
};

class DiagnosticRouter {
public:
    DiagnosticRouter(std::shared_ptr<DidRegistry> registry,
                    std::shared_ptr<UdsClient> uds_client,
                    std::shared_ptr<TransportAdapter> docan_transport,
                    std::shared_ptr<TransportAdapter> doip_transport);

    RoutingResult routeRequest(const std::string& ecu_id, uint16_t did);

    bool isEcuReachable(const std::string& ecu_id);

    std::shared_ptr<TransportAdapter> getTransportForEcu(const std::string& ecu_id);

private:
    std::shared_ptr<DidRegistry> registry_;
    std::shared_ptr<UdsClient> uds_client_;

    std::shared_ptr<TransportAdapter> docan_transport_;
    std::shared_ptr<TransportAdapter> doip_transport_;

    std::shared_ptr<TransportAdapter> selectTransport(TransportType type);
};

} // namespace cgw_diag