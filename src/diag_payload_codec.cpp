// =============================================================================
// src/diag_payload_codec.cpp
// CCU-DIAG SOME/IP payload v1 codec (CCU-DIAG-DSN-CR-010 §5.2)
// =============================================================================
// Field set is frozen for schemaVersion=1; new fields/semantic changes require
// a version bump and a documented migration. Error details carry field names
// only - never VIN, version values or registration payload contents.
// =============================================================================

#include "diag_payload_codec.h"
#include "constants.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <limits>

namespace ccu_diag {

namespace {

using json = nlohmann::json;

const char* baselineSourceToWire(BaselineSource source) {
    switch (source) {
        case BaselineSource::FACTORY: return "factory";
        case BaselineSource::LAST_OTA: return "lastOta";
        case BaselineSource::UNKNOWN: return "unknown";
    }
    return "unknown";
}

const char* overallResultToWire(CollectionStatus status) {
    switch (status) {
        case CollectionStatus::ALL_OK: return "all_ok";
        case CollectionStatus::PARTIAL: return "partial";
        case CollectionStatus::FAILED: return "failed";
    }
    return "failed";
}

const char* readMethodToWire(ReadMethod method) {
    switch (method) {
        case ReadMethod::UDS_0x22: return "UDS_0x22";
        case ReadMethod::SOMEIP_GET_VERSION: return "SOMEIP_getVersion";
    }
    return "UDS_0x22";
}

const char* ecuStatusToWire(EcuStatus status) {
    switch (status) {
        case EcuStatus::OK: return "ok";
        case EcuStatus::MISSING: return "missing";
        case EcuStatus::NRC: return "nrc";
        case EcuStatus::TIMEOUT: return "timeout";
        case EcuStatus::UNREACHABLE: return "unreachable";
        case EcuStatus::PARSE_ERROR: return "parse_error";
    }
    return "parse_error";
}

std::string trimForDetail(const std::string& value) {
    // Error details must stay bounded and free of payload contents.
    return value.size() > 64 ? value.substr(0, 64) : value;
}

const json* findField(const json& j, std::initializer_list<const char*> keys) {
    for (const char* key : keys) {
        auto it = j.find(key);
        if (it != j.end() && !it->is_null()) {
            return &(*it);
        }
    }
    return nullptr;
}

bool getOptionalString(const json& j, std::initializer_list<const char*> keys,
                       std::string& out, const char* what, std::string& err) {
    const json* field = findField(j, keys);
    if (field == nullptr) {
        out.clear();
        return true;
    }
    if (!field->is_string()) {
        err = std::string("field type mismatch (expected string): ") + what;
        return false;
    }
    out = field->get<std::string>();
    return true;
}

bool getRequiredString(const json& j, std::initializer_list<const char*> keys,
                       std::string& out, const char* what, std::string& err) {
    if (!getOptionalString(j, keys, out, what, err)) {
        return false;
    }
    if (out.empty()) {
        err = std::string("missing field: ") + what;
        return false;
    }
    return true;
}

bool getOptionalUint(const json& j, std::initializer_list<const char*> keys,
                     uint64_t& out, const char* what, std::string& err) {
    const json* field = findField(j, keys);
    if (field == nullptr) {
        out = 0;
        return true;
    }
    if (!field->is_number_unsigned() && !field->is_number_integer()) {
        err = std::string("field type mismatch (expected integer): ") + what;
        return false;
    }
    const int64_t value = field->get<int64_t>();
    if (value < 0) {
        err = std::string("negative integer not allowed: ") + what;
        return false;
    }
    out = static_cast<uint64_t>(value);
    return true;
}

} // namespace

std::string DiagPayloadCodec::encodeInventoryV1(
    const VehicleSoftwareSnapshot& snapshot) {
    json j;
    j["schemaVersion"] = static_cast<uint32_t>(snapshot.schema_version);
    j["inventorySource"] = inventorySourceToString(snapshot.inventory_source);
    j["vin"] = snapshot.vin;
    j["vinSource"] = vinSourceToString(snapshot.vin_source);
    if (snapshot.baseline_id.has_value() && !snapshot.baseline_id->empty()) {
        j["baselineId"] = snapshot.baseline_id.value();
    } else {
        j["baselineId"] = nullptr;
    }
    j["baselineSource"] = baselineSourceToWire(snapshot.baseline_source);
    j["registryVersion"] = snapshot.registry_version;
    j["collectedAt"] = snapshot.collected_at;
    j["overallResult"] = overallResultToWire(snapshot.overall_result);

    json ecus = json::array();
    for (const auto& entry : snapshot.ecu_list) {
        json item;
        item["ecuId"] = entry.ecu_id;
        item["partNumber"] = entry.part_number.value_or("");
        item["swVersion"] = entry.sw_version.value_or("");
        item["hwVersion"] = entry.hw_version.value_or("");
        item["serialNumber"] = entry.serial_number.value_or("");
        item["source"] = readMethodToWire(entry.source);
        item["status"] = ecuStatusToWire(entry.status);
        item["errorCode"] = entry.error_code.value_or("");
        ecus.push_back(std::move(item));
    }
    j["ecuList"] = std::move(ecus);

    return j.dump();
}

std::string DiagPayloadCodec::encodeInventoryError(
    const std::string& diag_error_code) {
    json j;
    j["schemaVersion"] = static_cast<uint32_t>(constants::DIAG_PAYLOAD_SCHEMA_VERSION);
    j["errorCode"] = diag_error_code;
    return j.dump();
}

bool DiagPayloadCodec::decodeRegistrationRequest(
    std::string_view payload, InstalledVersionsRequest& request,
    std::string& error_detail) {
    request = InstalledVersionsRequest{};

    if (payload.empty()) {
        error_detail = "empty payload";
        return false;
    }

    json j;
    try {
        j = json::parse(payload.begin(), payload.end());
    } catch (const json::exception& e) {
        error_detail = std::string("json parse failed: ") + trimForDetail(e.what());
        return false;
    }
    if (!j.is_object()) {
        error_detail = "payload is not a json object";
        return false;
    }

    // schemaVersion: required, integer or numeric string, must be 1.
    const json* schema = findField(j, {"schemaVersion", "schema_version"});
    if (schema == nullptr) {
        error_detail = "missing field: schemaVersion";
        return false;
    }
    std::string schema_text;
    if (schema->is_string()) {
        schema_text = schema->get<std::string>();
    } else if (schema->is_number_integer() || schema->is_number_unsigned()) {
        schema_text = std::to_string(schema->get<long long>());
    } else {
        error_detail = "field type mismatch (expected integer/string): schemaVersion";
        return false;
    }
    if (schema_text != std::to_string(constants::DIAG_PAYLOAD_SCHEMA_VERSION)) {
        error_detail = "unsupported schemaVersion";
        return false;
    }
    request.schema_version = constants::DIAG_PAYLOAD_SCHEMA_VERSION;

    if (!getRequiredString(j, {"executionId", "execution_id"}, request.execution_id,
                           "executionId", error_detail)) {
        return false;
    }
    if (request.execution_id.size() > constants::MAX_EXECUTION_ID_LENGTH) {
        error_detail = "field too long: executionId";
        return false;
    }

    uint64_t attempt_no = 0;
    if (!getOptionalUint(j, {"attemptNo", "attempt_no"}, attempt_no, "attemptNo",
                         error_detail)) {
        return false;
    }
    if (attempt_no == 0 ||
        attempt_no > std::numeric_limits<uint32_t>::max()) {
        error_detail = "missing field: attemptNo";
        return false;
    }
    request.attempt_no = static_cast<uint32_t>(attempt_no);

    if (!getOptionalString(j, {"vehicleTaskId", "vehicle_task_id"},
                           request.vehicle_task_id, "vehicleTaskId",
                           error_detail)) {
        return false;
    }
    if (!getOptionalString(j, {"idempotencyKey", "idempotency_key"},
                           request.idempotency_key, "idempotencyKey",
                           error_detail)) {
        return false;
    }

    uint64_t task_revision = 0;
    if (!getOptionalUint(j, {"taskRevision", "task_revision"}, task_revision,
                         "taskRevision", error_detail)) {
        return false;
    }
    request.task_revision = static_cast<uint32_t>(task_revision);

    uint64_t applied_at_ms = 0;
    if (!getOptionalUint(j, {"appliedAtMs", "applied_at_ms"}, applied_at_ms,
                         "appliedAtMs", error_detail)) {
        return false;
    }
    if (applied_at_ms >
        static_cast<uint64_t>(std::numeric_limits<int64_t>::max())) {
        error_detail = "out of range: appliedAtMs";
        return false;
    }
    request.applied_at_ms = static_cast<int64_t>(applied_at_ms);

    const json* items = findField(j, {"items"});
    if (items == nullptr) {
        error_detail = "missing field: items";
        return false;
    }
    if (!items->is_array()) {
        error_detail = "field type mismatch (expected array): items";
        return false;
    }
    if (items->empty()) {
        error_detail = "missing field: items (empty)";
        return false;
    }
    if (items->size() > constants::MAX_REGISTRATION_ITEMS) {
        error_detail = "too many items";
        return false;
    }

    request.items.reserve(items->size());
    for (const auto& node : *items) {
        if (!node.is_object()) {
            error_detail = "items entry is not an object";
            return false;
        }
        InstalledVersionItem item;
        // Per-item required values are validated by the registrar so the
        // response can report MISSING_FIELD / UNKNOWN_ECU / INVALID_VERSION.
        if (!getOptionalString(node, {"ecuId", "ecu_id"}, item.ecu_id, "items.ecuId",
                               error_detail) ||
            !getOptionalString(node, {"packageId", "package_id"}, item.package_id,
                               "items.packageId", error_detail) ||
            !getOptionalString(node, {"packageRevision", "package_revision"},
                               item.package_revision, "items.packageRevision",
                               error_detail) ||
            !getOptionalString(node, {"installedVersion", "installed_version"},
                               item.installed_version, "items.installedVersion",
                               error_detail)) {
            return false;
        }
        request.items.push_back(std::move(item));
    }

    return true;
}

std::string DiagPayloadCodec::encodeRegistrationResponse(
    const InstalledVersionsResponse& response) {
    json j;
    j["schemaVersion"] = static_cast<uint32_t>(response.schema_version);
    j["disposition"] = registrationDispositionToString(response.disposition);
    if (!response.registration_id.empty()) {
        j["registrationId"] = response.registration_id;
    }
    j["appliedCount"] = static_cast<uint32_t>(response.applied_count);

    json rejected = json::array();
    for (const auto& item : response.rejected_items) {
        json entry;
        entry["ecuId"] = item.ecu_id;
        entry["reasonCode"] = item.reason_code;
        rejected.push_back(std::move(entry));
    }
    j["rejectedItems"] = std::move(rejected);

    if (!response.effective_updated_at.empty()) {
        j["effectiveUpdatedAt"] = response.effective_updated_at;
    }
    return j.dump();
}

} // namespace ccu_diag
