#include "fota_someip_proxy.h"
#include "constants.h"
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>

namespace ccu_diag {

namespace {

using json = nlohmann::json;

const json* findField(const json& j, std::initializer_list<const char*> keys) {
    for (const char* key : keys) {
        auto it = j.find(key);
        if (it != j.end() && !it->is_null()) return &(*it);
    }
    return nullptr;
}

std::string normalizeEnum(const std::string& raw) {
    std::string out;
    for (char c : raw) {
        const auto uc = static_cast<unsigned char>(c);
        if (std::isalnum(uc)) {
            out.push_back(static_cast<char>(std::toupper(uc)));
        } else if (!out.empty() && out.back() != '_') {
            out.push_back('_');
        }
    }
    while (!out.empty() && out.back() == '_') out.pop_back();
    return out;
}

std::string getString(const json& j, std::initializer_list<const char*> keys,
                      const std::string& fallback) {
    const json* field = findField(j, keys);
    if (field == nullptr || !field->is_string()) return fallback;
    return field->get<std::string>();
}

} // namespace

FotaSomeIpProxy::FotaSomeIpProxy(std::unique_ptr<SomeIpClientProxy> proxy)
    : proxy_(std::move(proxy)) {
}

bool FotaSomeIpProxy::initialize(const SomeIpServiceConfig& remote_service_config) {
    return proxy_->initialize(remote_service_config);
}

bool FotaSomeIpProxy::connect() {
    return proxy_->connect();
}

void FotaSomeIpProxy::disconnect() {
    proxy_->disconnect();
}

bool FotaSomeIpProxy::isConnected() const {
    return proxy_->isConnected();
}

VehicleSoftwareSnapshot FotaSomeIpProxy::collectVehicleInventory() {
    VehicleSoftwareSnapshot snapshot;
    snapshot.overall_result = CollectionStatus::FAILED;

    if (!proxy_->isConnected()) {
        return snapshot;
    }

    SomeIpMessage request;
    request.header.service_id = constants::CCU_DIAG_SERVICE_ID;
    request.header.method_id = constants::METHOD_COLLECT_VEHICLE_INVENTORY;
    request.header.message_type = MessageType::REQUEST;

    auto response = proxy_->sendRequest(request);

    if (response.header.return_code == ReturnCode::E_OK) {
        try {
            std::string json_str(response.payload.begin(), response.payload.end());
            nlohmann::json j = nlohmann::json::parse(json_str);

            const json* schema = findField(j, {"schemaVersion", "schema_version"});
            if (schema != nullptr) {
                if (schema->is_number_integer() || schema->is_number_unsigned()) {
                    snapshot.schema_version = static_cast<uint32_t>(
                        schema->get<int64_t>());
                } else if (schema->is_string()) {
                    snapshot.schema_version = static_cast<uint32_t>(
                        std::stoul(schema->get<std::string>()));
                }
            }
            snapshot.inventory_source = stringToInventorySource(
                getString(j, {"inventorySource", "inventory_source"}, "hardware"));

            snapshot.vin = getString(j, {"vin"}, "");
            snapshot.registry_version =
                getString(j, {"registryVersion", "registry_version"}, "");
            snapshot.collected_at = getString(j, {"collectedAt", "collected_at"}, "");

            const std::string vin_source_str =
                getString(j, {"vinSource", "vin_source"}, "unknown");
            snapshot.vin_source =
                (normalizeEnum(vin_source_str) == "PROVISIONED")
                    ? VinSource::PROVISIONED
                    : VinSource::UNKNOWN;

            const json* baseline = findField(j, {"baselineId", "baseline_id",
                                                 "baselineCode", "baseline_code"});
            if (baseline != nullptr && baseline->is_string()) {
                snapshot.baseline_id = baseline->get<std::string>();
            } else {
                snapshot.baseline_id.reset();
            }

            const std::string baseline_source_str =
                getString(j, {"baselineSource", "baseline_source"}, "unknown");
            const std::string baseline_norm = normalizeEnum(baseline_source_str);
            if (baseline_norm == "FACTORY") {
                snapshot.baseline_source = BaselineSource::FACTORY;
            } else if (baseline_norm == "LAST_OTA") {
                snapshot.baseline_source = BaselineSource::LAST_OTA;
            } else {
                snapshot.baseline_source = BaselineSource::UNKNOWN;
            }

            const std::string overall_str =
                getString(j, {"overallResult", "overall_result"}, "failed");
            const std::string overall_norm = normalizeEnum(overall_str);
            if (overall_norm == "ALL_OK" || overall_norm == "OK") {
                snapshot.overall_result = CollectionStatus::ALL_OK;
            } else if (overall_norm == "PARTIAL") {
                snapshot.overall_result = CollectionStatus::PARTIAL;
            } else {
                snapshot.overall_result = CollectionStatus::FAILED;
            }

            const json* ecu_list = findField(j, {"ecuList", "ecu_list"});
            if (ecu_list == nullptr || !ecu_list->is_array() || ecu_list->empty()) {
                // Empty/absent list is not a successful inventory (D9).
                snapshot.overall_result = CollectionStatus::FAILED;
                snapshot.ecu_list.clear();
                return snapshot;
            }

            for (const auto& item : *ecu_list) {
                if (!item.is_object()) {
                    snapshot.overall_result = CollectionStatus::FAILED;
                    snapshot.ecu_list.clear();
                    return snapshot;
                }
                EcuVersionEntry entry;
                entry.ecu_id = getString(item, {"ecuId", "ecu_id"}, "");
                const std::string part = getString(item, {"partNumber", "part_number"}, "");
                // CCU-DIAG-DSN-CR-012: independent software part number fact.
                const std::string software_part = getString(
                    item, {"softwarePartNumber", "software_part_number"}, "");
                const std::string sw = getString(item, {"swVersion", "sw_version"}, "");
                const std::string hw = getString(item, {"hwVersion", "hw_version"}, "");
                const std::string error_code =
                    getString(item, {"errorCode", "error_code"}, "");
                if (!part.empty()) entry.part_number = part;
                if (!software_part.empty()) entry.software_part_number = software_part;
                if (!sw.empty()) entry.sw_version = sw;
                if (!hw.empty()) entry.hw_version = hw;
                if (!error_code.empty()) entry.error_code = error_code;

                if (normalizeEnum(getString(item, {"source"}, "UDS_0x22")) ==
                    "SOMEIP_GET_VERSION") {
                    entry.source = ReadMethod::SOMEIP_GET_VERSION;
                }

                const std::string status = normalizeEnum(
                    getString(item, {"status"}, "parse_error"));
                if (status == "OK") {
                    entry.status = EcuStatus::OK;
                } else if (status == "MISSING") {
                    entry.status = EcuStatus::MISSING;
                } else if (status == "NRC") {
                    entry.status = EcuStatus::NRC;
                } else if (status == "TIMEOUT") {
                    entry.status = EcuStatus::TIMEOUT;
                } else if (status == "UNREACHABLE") {
                    entry.status = EcuStatus::UNREACHABLE;
                } else {
                    entry.status = EcuStatus::PARSE_ERROR;
                }
                snapshot.ecu_list.push_back(std::move(entry));
            }
        } catch (const std::exception&) {
            snapshot.overall_result = CollectionStatus::FAILED;
            snapshot.ecu_list.clear();
        }
    }

    return snapshot;
}

EcuVersionEntry FotaSomeIpProxy::getEcuVersion(const std::string& ecu_id) {
    EcuVersionEntry entry;
    entry.ecu_id = ecu_id;
    entry.status = EcuStatus::MISSING;

    if (!proxy_->isConnected()) {
        return entry;
    }

    SomeIpMessage request;
    request.header.service_id = constants::CCU_DIAG_SERVICE_ID;
    request.header.method_id = constants::METHOD_GET_ECU_VERSION;
    request.header.message_type = MessageType::REQUEST;
    request.payload.assign(ecu_id.begin(), ecu_id.end());

    auto response = proxy_->sendRequest(request);

    if (response.header.return_code == ReturnCode::E_OK) {
        try {
            std::string json_str(response.payload.begin(), response.payload.end());
            nlohmann::json j = nlohmann::json::parse(json_str);

            entry.ecu_id = j.value("ecu_id", ecu_id);

            std::string source_str = j.value("source", "UDS_0x22");
            if (source_str == "SOMEIP_GET_VERSION") {
                entry.source = ReadMethod::SOMEIP_GET_VERSION;
            } else {
                entry.source = ReadMethod::UDS_0x22;
            }

            std::string status_str = j.value("status", "MISSING");
            if (status_str == "OK") {
                entry.status = EcuStatus::OK;
            } else if (status_str == "NRC") {
                entry.status = EcuStatus::NRC;
            } else if (status_str == "TIMEOUT") {
                entry.status = EcuStatus::TIMEOUT;
            } else if (status_str == "UNREACHABLE") {
                entry.status = EcuStatus::UNREACHABLE;
            } else if (status_str == "PARSE_ERROR") {
                entry.status = EcuStatus::PARSE_ERROR;
            } else {
                entry.status = EcuStatus::MISSING;
            }

            if (j.contains("sw_version")) {
                entry.sw_version = j["sw_version"].get<std::string>();
            }
            if (j.contains("part_number")) {
                entry.part_number = j["part_number"].get<std::string>();
            }
            if (j.contains("hw_version")) {
                entry.hw_version = j["hw_version"].get<std::string>();
            }
            if (j.contains("error_code")) {
                entry.error_code = j["error_code"].get<std::string>();
            }
        } catch (const std::exception&) {
            entry.status = EcuStatus::PARSE_ERROR;
        }
    }

    return entry;
}

std::string FotaSomeIpProxy::getRegistryVersion() {
    if (!proxy_->isConnected()) {
        return "";
    }

    SomeIpMessage request;
    request.header.service_id = constants::CCU_DIAG_SERVICE_ID;
    request.header.method_id = constants::METHOD_GET_REGISTRY_VERSION;
    request.header.message_type = MessageType::REQUEST;

    auto response = proxy_->sendRequest(request);

    if (response.header.return_code == ReturnCode::E_OK) {
        return std::string(response.payload.begin(), response.payload.end());
    }

    return "";
}

InstalledVersionsResponse FotaSomeIpProxy::recordInstalledVersions(
    const InstalledVersionsRequest& request_data) {
    InstalledVersionsResponse result;
    result.disposition = RegistrationDisposition::RETRYABLE_FAILED;

    if (!proxy_->isConnected()) {
        return result;
    }

    json items = json::array();
    for (const auto& item : request_data.items) {
        items.push_back(json{{"ecuId", item.ecu_id},
                             {"packageId", item.package_id},
                             {"packageRevision", item.package_revision},
                             {"installedVersion", item.installed_version}});
    }
    json payload{{"schemaVersion", constants::DIAG_PAYLOAD_SCHEMA_VERSION},
                 {"vehicleTaskId", request_data.vehicle_task_id},
                 {"taskRevision", request_data.task_revision},
                 {"executionId", request_data.execution_id},
                 {"attemptNo", request_data.attempt_no},
                 {"idempotencyKey", request_data.idempotency_key},
                 {"appliedAtMs", request_data.applied_at_ms},
                 {"items", std::move(items)}};

    SomeIpMessage message;
    message.header.service_id = constants::CCU_DIAG_SERVICE_ID;
    message.header.method_id = constants::METHOD_RECORD_INSTALLED_VERSIONS;
    message.header.message_type = MessageType::REQUEST;
    const std::string text = payload.dump();
    message.payload.assign(text.begin(), text.end());

    auto response = proxy_->sendRequest(message);
    if (response.header.return_code != ReturnCode::E_OK) {
        return result;
    }

    try {
        const std::string response_text(response.payload.begin(), response.payload.end());
        const json j = json::parse(response_text);
        const std::string disposition =
            normalizeEnum(getString(j, {"disposition"}, ""));
        if (disposition == "APPLIED") {
            result.disposition = RegistrationDisposition::APPLIED;
        } else if (disposition == "DUPLICATE") {
            result.disposition = RegistrationDisposition::DUPLICATE;
        } else if (disposition == "REJECTED_INVALID") {
            result.disposition = RegistrationDisposition::REJECTED_INVALID;
        } else {
            result.disposition = RegistrationDisposition::RETRYABLE_FAILED;
        }
        result.registration_id = getString(j, {"registrationId", "registration_id"}, "");
        result.effective_updated_at =
            getString(j, {"effectiveUpdatedAt", "effective_updated_at"}, "");
        if (const json* count = findField(j, {"appliedCount", "applied_count"})) {
            if (count->is_number_integer() || count->is_number_unsigned()) {
                result.applied_count = static_cast<uint32_t>(count->get<int64_t>());
            }
        }
        if (const json* rejected = findField(j, {"rejectedItems", "rejected_items"})) {
            if (rejected->is_array()) {
                for (const auto& item : *rejected) {
                    RejectedVersionItem out;
                    if (item.is_object()) {
                        out.ecu_id = getString(item, {"ecuId", "ecu_id"}, "");
                        out.reason_code =
                            getString(item, {"reasonCode", "reason_code", "reason"},
                                      "UNSPECIFIED_REJECTION");
                    } else if (item.is_string()) {
                        out.reason_code = item.get<std::string>();
                    }
                    result.rejected_items.push_back(std::move(out));
                }
            }
        }
    } catch (const std::exception&) {
        result.disposition = RegistrationDisposition::RETRYABLE_FAILED;
        result.registration_id.clear();
    }

    return result;
}

void FotaSomeIpProxy::setRequestTimeout(uint32_t timeout_ms) {
    proxy_->setRequestTimeout(timeout_ms);
}

} // namespace ccu_diag
