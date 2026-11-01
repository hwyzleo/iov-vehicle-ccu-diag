#include "diagnostic_router.h"
#include "diag_log_adapter.h"
#include "diag_log_context.h"

namespace ccu_diag {

namespace {
const char* transportTypeToString(TransportType type) {
    switch (type) {
        case TransportType::DOCAN: return "DoCAN";
        case TransportType::DOIP:  return "DoIP";
        default: return "UNKNOWN";
    }
}
} // namespace

DiagnosticRouter::DiagnosticRouter(std::shared_ptr<DidRegistry> registry,
                                   std::shared_ptr<UdsClient> uds_client,
                                   std::shared_ptr<TransportAdapter> docan_transport,
                                   std::shared_ptr<TransportAdapter> doip_transport)
    : registry_(registry), uds_client_(uds_client),
      docan_transport_(docan_transport), doip_transport_(doip_transport) {
}

RoutingResult DiagnosticRouter::routeRequest(const std::string& ecu_id, uint16_t did) {
    RoutingResult result;

    auto ecu_config = registry_->findEcuById(ecu_id);
    if (!ecu_config) {
        result.success = false;
        result.error_code = ErrorCode::ECU_NOT_REGISTERED;
        result.error_message = "ECU not registered in DID list: " + ecu_id;
        // CCU-DIAG-DSN-CR-008: 目标 ECU 未登记
        DiagLogAdapter::router().error(diag_events::ROUTE_FAILED,
            "ECU not registered in DID list",
            {dlog::f_str("ecu_id", ecu_id),
             dlog::f_str("error_code", ErrorCodeMapper::toDiagCode(ErrorCode::ECU_NOT_REGISTERED))}
        );
        return result;
    }

    if (ecu_config->read_method == ReadMethod::UDS_0x22) {
        bool did_found = false;
        for (const auto& did_entry : ecu_config->dids) {
            if (did_entry.did == did) {
                did_found = true;
                break;
            }
        }

        if (!did_found) {
            result.success = false;
            result.error_code = ErrorCode::DID_NOT_SUPPORTED;
            result.error_message = "DID not supported by ECU";
            return result;
        }
    }

    auto transport = selectTransport(ecu_config->transport);
    if (!transport) {
        result.success = false;
        result.error_code = ErrorCode::ECU_UNREACHABLE;
        result.error_message = "No transport available for ECU";
        // CCU-DIAG-DSN-CR-008: 无路由
        DiagLogAdapter::router().error(diag_events::ROUTE_FAILED,
            "No transport available for ECU",
            {dlog::f_str("ecu_id", ecu_id),
             dlog::f_str("logical_address", hex_id(ecu_config->logical_address)),
             dlog::f_str("transport", transportTypeToString(ecu_config->transport)),
             dlog::f_str("error_code", ErrorCodeMapper::toDiagCode(ErrorCode::ECU_UNREACHABLE))}
        );
        return result;
    }

    if (!transport->isReachable(ecu_config->logical_address)) {
        result.success = false;
        result.error_code = ErrorCode::ECU_UNREACHABLE;
        result.error_message = "ECU is not reachable";
        // CCU-DIAG-DSN-CR-008: ECU 不可达
        DiagLogAdapter::router().error(diag_events::ROUTE_FAILED,
            "ECU is not reachable",
            {dlog::f_str("ecu_id", ecu_id),
             dlog::f_str("logical_address", hex_id(ecu_config->logical_address)),
             dlog::f_str("transport", transport->getTransportType()),
             dlog::f_str("error_code", ErrorCodeMapper::toDiagCode(ErrorCode::ECU_UNREACHABLE))}
        );
        return result;
    }

    auto uds_response = uds_client_->readDataByIdentifier(ecu_config->logical_address, did);

    if (uds_response.success) {
        result.success = true;
        result.response_data = uds_response.data;
        result.error_code = ErrorCode::SUCCESS;
    } else {
        result.success = false;
        result.response_data = uds_response.data;

        if (uds_response.nrc != 0) {
            result.error_code = ErrorCodeMapper::fromNrc(uds_response.nrc);
        } else {
            result.error_code = ErrorCode::REQUEST_TIMEOUT;
        }

        result.error_message = uds_response.error_message;
    }

    return result;
}

bool DiagnosticRouter::isEcuReachable(const std::string& ecu_id) {
    auto ecu_config = registry_->findEcuById(ecu_id);
    if (!ecu_config) {
        return false;
    }

    auto transport = selectTransport(ecu_config->transport);
    if (!transport) {
        return false;
    }

    return transport->isReachable(ecu_config->logical_address);
}

std::shared_ptr<TransportAdapter> DiagnosticRouter::getTransportForEcu(const std::string& ecu_id) {
    auto ecu_config = registry_->findEcuById(ecu_id);
    if (!ecu_config) {
        return nullptr;
    }

    return selectTransport(ecu_config->transport);
}

std::shared_ptr<TransportAdapter> DiagnosticRouter::selectTransport(TransportType type) {
    switch (type) {
        case TransportType::DOCAN:
            return docan_transport_;
        case TransportType::DOIP:
            return doip_transport_;
        default:
            return nullptr;
    }
}

} // namespace ccu_diag