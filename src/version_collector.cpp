// =============================================================================
// src/version_collector.cpp
// Inventory collection orchestration (CCU-DIAG-DSN-CR-010 §3)
// =============================================================================
// hardware mode: existing per-ECU UDS 0x22 / SOME-IP read path.
// seed mode: no downlink diagnostics at all; facts come from the DIAG-owned
// EcuInventoryStore and the snapshot is marked inventorySource=seed.
// =============================================================================

#include "version_collector.h"
#include "constants.h"
#include "error_codes.h"
#include "diag_log_adapter.h"
#include "diag_log_context.h"
#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>

namespace ccu_diag {

namespace {
const char* transportTypeToString(TransportType type) {
    switch (type) {
        case TransportType::DOCAN: return "DoCAN";
        case TransportType::DOIP:  return "DoIP";
        default: return "UNKNOWN";
    }
}

const char* collectionStatusToString(CollectionStatus status) {
    switch (status) {
        case CollectionStatus::ALL_OK:  return "ALL_OK";
        case CollectionStatus::PARTIAL: return "PARTIAL";
        case CollectionStatus::FAILED:  return "FAILED";
        default: return "UNKNOWN";
    }
}
} // namespace

VersionCollector::VersionCollector(
    std::shared_ptr<DidRegistry> registry,
    std::shared_ptr<DiagnosticRouter> router,
    std::shared_ptr<TransportAdapter> docan_transport,
    std::shared_ptr<TransportAdapter> doip_transport,
    std::shared_ptr<VinStore> vin_store,
    std::shared_ptr<EcuInventoryStore> inventory_store)
    : registry_(registry), router_(router),
      docan_transport_(docan_transport),
      doip_transport_(doip_transport),
      vin_store_(vin_store),
      inventory_store_(std::move(inventory_store)),
      baseline_source_(BaselineSource::UNKNOWN) {
}

bool VersionCollector::isSeedMode() const {
    return inventory_store_ != nullptr &&
           inventory_store_->mode() == InventoryMode::SEED;
}

VehicleSoftwareSnapshot VersionCollector::collectVehicleInventory() {
    return isSeedMode() ? collectFromSeed() : collectFromHardware();
}

void VersionCollector::fillVin(VehicleSoftwareSnapshot& snapshot) {
    auto vin = vin_store_->readVin();
    if (vin.has_value()) {
        snapshot.vin = vin.value();
        snapshot.vin_source = VinSource::PROVISIONED;
    } else {
        snapshot.vin.clear();
        snapshot.vin_source = VinSource::UNKNOWN;
        // CCU-DIAG-DSN-CR-008: VIN 未预置或 VinStore 访问失败
        DiagLogAdapter::vin_store().warn(diag_events::VIN_READ_FAILED,
            "VIN not provisioned",
            {dlog::f_str("operation", "readVin"),
             dlog::f_str("result", "not_provisioned"),
             dlog::f_str("error_code", ErrorCodeMapper::toDiagCode(ErrorCode::VIN_NOT_PROVISIONED))}
        );
    }
}

void VersionCollector::logCollectionCompleted(const VehicleSoftwareSnapshot& snapshot,
                                              int64_t duration_ms) {
    int64_t failed_count = 0;
    for (const auto& entry : snapshot.ecu_list) {
        if (entry.status != EcuStatus::OK) {
            ++failed_count;
        }
    }

    DiagLogAdapter::orchestrator().info(diag_events::INVENTORY_COMPLETED,
        "Vehicle inventory collection completed",
        {dlog::f_str("registry_version", snapshot.registry_version),
         dlog::f_str("overall_result", collectionStatusToString(snapshot.overall_result)),
         dlog::f_str("inventory_source", inventorySourceToString(snapshot.inventory_source)),
         dlog::f_int("ecu_count", static_cast<int64_t>(snapshot.ecu_list.size())),
         dlog::f_int("failed_count", failed_count),
         dlog::f_int("duration_ms", duration_ms)}
    );
}

VehicleSoftwareSnapshot VersionCollector::collectFromHardware() {
    auto start = std::chrono::steady_clock::now();

    VehicleSoftwareSnapshot snapshot;
    snapshot.schema_version = constants::DIAG_PAYLOAD_SCHEMA_VERSION;
    snapshot.inventory_source = InventorySource::HARDWARE;

    fillVin(snapshot);

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

    auto end = std::chrono::steady_clock::now();
    auto duration_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        end - start).count();
    logCollectionCompleted(snapshot, duration_ms);

    return snapshot;
}

VehicleSoftwareSnapshot VersionCollector::collectFromSeed() {
    auto start = std::chrono::steady_clock::now();

    if (!inventory_store_ || !inventory_store_->available()) {
        ErrorCode code = ErrorCode::INVENTORY_STATE_UNAVAILABLE;
        if (inventory_store_ &&
            inventory_store_->startupResult().status ==
                InventoryStoreStatus::SEED_INVALID) {
            code = ErrorCode::INVENTORY_SEED_INVALID;
        }
        throw DiagException(code, "inventory state unavailable");
    }
    const EcuInventoryRecord& record = inventory_store_->record();
    if (record.ecus.empty()) {
        // A successful response with an empty list is forbidden (D9).
        throw DiagException(ErrorCode::INVENTORY_STATE_UNAVAILABLE,
                            "inventory record has no ECUs");
    }

    VehicleSoftwareSnapshot snapshot;
    snapshot.schema_version = constants::DIAG_PAYLOAD_SCHEMA_VERSION;
    snapshot.inventory_source = InventorySource::SEED;

    fillVin(snapshot);

    if (!record.baseline_code.empty()) {
        snapshot.baseline_id = record.baseline_code;
        // The seed declares a baseline code, not its origin; never claim
        // factory/OTA provenance for a declaration table (CR-010 Q3).
        snapshot.baseline_source = BaselineSource::UNKNOWN;
    } else {
        snapshot.baseline_id = baseline_id_;
        snapshot.baseline_source = baseline_source_;
    }
    // The DID registry version always describes the active read rules (§4).
    snapshot.registry_version = registry_->getRegistryVersion();
    snapshot.collected_at = getCurrentTimestamp();

    for (const auto& fact : record.ecus) {
        snapshot.ecu_list.push_back(seedEcuVersion(fact));
    }
    snapshot.overall_result = determineOverallResult(snapshot.ecu_list);

    auto end = std::chrono::steady_clock::now();
    auto duration_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        end - start).count();
    logCollectionCompleted(snapshot, duration_ms);

    return snapshot;
}

EcuVersionEntry VersionCollector::seedEcuVersion(const InventoryEcuFact& fact) {
    EcuVersionEntry entry;
    entry.ecu_id = fact.ecu_id;
    if (!fact.part_number.empty()) {
        entry.part_number = fact.part_number;
    }
    if (!fact.sw_version.empty()) {
        entry.sw_version = fact.sw_version;
    }
    if (!fact.hw_version.empty()) {
        entry.hw_version = fact.hw_version;
    }
    // source stays the configured read method; the snapshot-level
    // inventorySource carries the authority of the value.
    auto ecu_config = registry_->findEcuById(fact.ecu_id);
    entry.source = ecu_config ? ecu_config->read_method : ReadMethod::UDS_0x22;

    if (fact.sw_version.empty()) {
        entry.status = EcuStatus::MISSING;
        entry.error_code = ErrorCodeMapper::toDiagCode(ErrorCode::DID_NOT_SUPPORTED);
    } else {
        entry.status = EcuStatus::OK;
    }
    return entry;
}

EcuVersionEntry VersionCollector::collectEcuVersion(const std::string& ecu_id) {
    if (isSeedMode()) {
        if (!inventory_store_ || !inventory_store_->available()) {
            ErrorCode code = ErrorCode::INVENTORY_STATE_UNAVAILABLE;
            if (inventory_store_ &&
                inventory_store_->startupResult().status ==
                    InventoryStoreStatus::SEED_INVALID) {
                code = ErrorCode::INVENTORY_SEED_INVALID;
            }
            throw DiagException(code, "inventory state unavailable");
        }
        const auto* fact = inventory_store_->record().findEcu(ecu_id);
        if (fact != nullptr) {
            return seedEcuVersion(*fact);
        }
    }
    return processEcuVersion(ecu_id);
}

void VersionCollector::setBaselineInfo(const std::string& baseline_id, BaselineSource source) {
    baseline_id_ = baseline_id;
    baseline_source_ = source;
}

EcuVersionEntry VersionCollector::processEcuVersion(const std::string& ecu_id) {
    EcuVersionEntry entry;
    entry.ecu_id = ecu_id;

    auto ecu_config = registry_->findEcuById(ecu_id);
    if (!ecu_config) {
        entry.status = EcuStatus::MISSING;
        entry.error_code = ErrorCodeMapper::toDiagCode(ErrorCode::ECU_NOT_REGISTERED);
        entry.source = ReadMethod::UDS_0x22;
        // CCU-DIAG-DSN-CR-008: ECU 未登记
        DiagLogAdapter::tester().warn(diag_events::TESTER_REQUEST_FAILED,
            "ECU not registered",
            {dlog::f_str("ecu_id", ecu_id),
             dlog::f_str("error_code", ErrorCodeMapper::toDiagCode(ErrorCode::ECU_NOT_REGISTERED))}
        );
        return entry;
    }

    entry.source = ecu_config->read_method;

    if (ecu_config->read_method == ReadMethod::UDS_0x22) {
        for (const auto& did_entry : ecu_config->dids) {
            // CCU-DIAG-DSN-CR-008: 为每个目标 ECU 创建诊断子作用域
            auto scope = make_diagnostic_child_scope(
                ecu_id,
                hex_id(ecu_config->logical_address),
                transportTypeToString(ecu_config->transport),
                "0x22",
                hex_id(did_entry.did)
            );

            auto req_start = std::chrono::steady_clock::now();
            auto result = router_->routeRequest(ecu_id, did_entry.did);
            auto req_end = std::chrono::steady_clock::now();
            auto duration_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                req_end - req_start).count();

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
                // CCU-DIAG-DSN-CR-008: 单 ECU 读取成功
                DiagLogAdapter::tester().info(diag_events::TESTER_REQUEST_SUCCEEDED,
                    "UDS request succeeded",
                    {dlog::f_str("ecu_id", ecu_id),
                     dlog::f_str("logical_address", hex_id(ecu_config->logical_address)),
                     dlog::f_str("transport", transportTypeToString(ecu_config->transport)),
                     dlog::f_str("uds_sid", "0x22"),
                     dlog::f_str("did", hex_id(did_entry.did)),
                     dlog::f_int("attempt", 1),
                     dlog::f_int("duration_ms", duration_ms)}
                );
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

                // CCU-DIAG-DSN-CR-008: 不可达、超时、NRC、缺失或解析失败
                DiagLogAdapter::tester().warn(diag_events::TESTER_REQUEST_FAILED,
                    "UDS request failed",
                    {dlog::f_str("ecu_id", ecu_id),
                     dlog::f_str("logical_address", hex_id(ecu_config->logical_address)),
                     dlog::f_str("transport", transportTypeToString(ecu_config->transport)),
                     dlog::f_str("uds_sid", "0x22"),
                     dlog::f_str("did", hex_id(did_entry.did)),
                     dlog::f_str("nrc", result.error_code == ErrorCode::NEGATIVE_RESPONSE ? "present" : ""),
                     dlog::f_int("attempt", 1),
                     dlog::f_int("duration_ms", duration_ms),
                     dlog::f_str("error_code", entry.error_code.value_or(""))}
                );
                break;
            }
        }
    } else if (ecu_config->read_method == ReadMethod::SOMEIP_GET_VERSION) {
        // TODO: SOME/IP version collection not yet implemented
        entry.status = EcuStatus::PARSE_ERROR;
        entry.error_code = ErrorCodeMapper::toDiagCode(ErrorCode::PARSE_ERROR);
    } else {
        entry.status = EcuStatus::PARSE_ERROR;
        entry.error_code = ErrorCodeMapper::toDiagCode(ErrorCode::PARSE_ERROR);
    }

    return entry;
}

std::string VersionCollector::getCurrentTimestamp() {
    return EcuInventoryStore::nowIso8601();
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

} // namespace ccu_diag
