#include "version_collector.h"
#include "error_codes.h"
#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>

namespace cgw_diag {

VersionCollector::VersionCollector(std::shared_ptr<DidRegistry> registry,
                                   std::shared_ptr<DiagnosticRouter> router,
                                   std::shared_ptr<TransportAdapter> docan_transport,
                                   std::shared_ptr<TransportAdapter> doip_transport)
    : registry_(registry), router_(router),
      docan_transport_(docan_transport),
      doip_transport_(doip_transport),
      baseline_source_(BaselineSource::UNKNOWN) {
}

VehicleSoftwareSnapshot VersionCollector::collectVehicleInventory(const std::string& vin) {
    VehicleSoftwareSnapshot snapshot;

    snapshot.vin = vin;
    snapshot.baseline_id = baseline_id_;
    snapshot.baseline_source = baseline_source_;
    snapshot.registry_version = registry_->getRegistryVersion();
    snapshot.collected_at = getCurrentTimestamp();

    const auto& ecus = registry_->getAllEcus();
    for (const auto& ecu : ecus) {
        EcuVersionEntry entry = processEcuVersion(ecu.ecu_id);
        snapshot.ecu_list.push_back(entry);
    }

    snapshot.overall_result = determineOverallResult(snapshot.ecu_list);

    return snapshot;
}

EcuVersionEntry VersionCollector::collectEcuVersion(const std::string& ecu_id) {
    return processEcuVersion(ecu_id);
}

void VersionCollector::setBaselineInfo(const std::string& baseline_id, BaselineSource source) {
    baseline_id_ = baseline_id;
    baseline_source_ = source;
}

EcuVersionEntry VersionCollector::processEcuVersion(const std::string& ecu_id) {
    EcuVersionEntry entry;
    entry.ecu_id = ecu_id;

    if (!registry_->isEcuRegistered(ecu_id)) {
        entry.status = EcuStatus::MISSING;
        entry.error_code = ErrorCodeMapper::toDiagCode(ErrorCode::ECU_NOT_REGISTERED);
        entry.source = ReadMethod::UDS_0x22;
        return entry;
    }

    auto ecu_config = registry_->findEcuById(ecu_id);
    if (!ecu_config) {
        entry.status = EcuStatus::MISSING;
        entry.error_code = ErrorCodeMapper::toDiagCode(ErrorCode::ECU_NOT_REGISTERED);
        entry.source = ReadMethod::UDS_0x22;
        return entry;
    }

    entry.source = ecu_config->read_method;

    if (ecu_config->read_method == ReadMethod::UDS_0x22) {
        for (const auto& did_entry : ecu_config->dids) {
            auto result = router_->routeRequest(ecu_id, did_entry.did);

            if (result.success) {
                if (!result.response_data.empty()) {
                    std::string version_str;
                    for (size_t i = 0; i < result.response_data.size(); ++i) {
                        if (result.response_data[i] == 0) break;
                        version_str += static_cast<char>(result.response_data[i]);
                    }

                    if (did_entry.field == "swVersion") {
                        entry.sw_version = version_str;
                    } else if (did_entry.field == "partNumber") {
                        entry.part_number = version_str;
                    } else if (did_entry.field == "hwVersion") {
                        entry.hw_version = version_str;
                    }
                }

                entry.status = EcuStatus::OK;
            } else {
                switch (result.error_code) {
                    case ErrorCode::REQUEST_TIMEOUT:
                        entry.status = EcuStatus::TIMEOUT;
                        break;
                    case ErrorCode::ECU_UNREACHABLE:
                        entry.status = EcuStatus::UNREACHABLE;
                        break;
                    case ErrorCode::NEGATIVE_RESPONSE:
                        entry.status = EcuStatus::NRC;
                        break;
                    case ErrorCode::DID_NOT_SUPPORTED:
                        entry.status = EcuStatus::MISSING;
                        break;
                    default:
                        entry.status = EcuStatus::PARSE_ERROR;
                        break;
                }

                entry.error_code = ErrorCodeMapper::toDiagCode(result.error_code);
                break;
            }
        }
    } else if (ecu_config->read_method == ReadMethod::SOMEIP_GET_VERSION) {
        entry.status = EcuStatus::OK;
        entry.sw_version = "SOME/IP_VERSION";
    }

    return entry;
}

std::string VersionCollector::getCurrentTimestamp() {
    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);

    std::stringstream ss;
    ss << std::put_time(std::localtime(&time), "%Y-%m-%dT%H:%M:%SZ");
    return ss.str();
}

CollectionStatus VersionCollector::determineOverallResult(const std::vector<EcuVersionEntry>& entries) {
    if (entries.empty()) {
        return CollectionStatus::FAILED;
    }

    bool has_success = false;
    bool has_failure = false;

    for (const auto& entry : entries) {
        if (entry.status == EcuStatus::OK) {
            has_success = true;
        } else {
            has_failure = true;
        }
    }

    if (has_success && !has_failure) {
        return CollectionStatus::ALL_OK;
    } else if (has_success && has_failure) {
        return CollectionStatus::PARTIAL;
    } else {
        return CollectionStatus::FAILED;
    }
}

} // namespace cgw_diag
