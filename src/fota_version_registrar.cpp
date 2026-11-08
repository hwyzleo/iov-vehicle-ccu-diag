// =============================================================================
// src/fota_version_registrar.cpp
// 0x0005 METHOD_RECORD_INSTALLED_VERSIONS handler logic
// (CCU-DIAG-DSN-CR-010 D6/D7/D8 / US-013)
// =============================================================================

#include "fota_version_registrar.h"
#include "constants.h"
#include "error_codes.h"

#include <ccu/fw/hash/sha256.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <tuple>

namespace ccu_diag {

namespace {

using json = nlohmann::json;

constexpr const char* REASON_MISSING_FIELD = "MISSING_FIELD";
constexpr const char* REASON_UNKNOWN_ECU = "UNKNOWN_ECU";
constexpr const char* REASON_INVALID_VERSION = "INVALID_VERSION";
constexpr const char* REASON_FIELD_TOO_LONG = "FIELD_TOO_LONG";
constexpr const char* REASON_IDEMPOTENCY_CONFLICT = "IDEMPOTENCY_CONFLICT";

InstalledVersionsResponse makeRejected(const std::string& error_code) {
    InstalledVersionsResponse response;
    response.schema_version = constants::DIAG_PAYLOAD_SCHEMA_VERSION;
    response.disposition = RegistrationDisposition::REJECTED_INVALID;
    response.error_code = error_code;
    return response;
}

} // namespace

FotaVersionRegistrar::FotaVersionRegistrar(
    std::shared_ptr<EcuInventoryStore> store, std::vector<std::string> known_ecu_ids,
    uint32_t idempotency_window)
    : store_(std::move(store)),
      known_ecu_ids_(std::move(known_ecu_ids)),
      idempotency_window_(idempotency_window) {
    if (idempotency_window_ < constants::MIN_INVENTORY_IDEMPOTENCY_WINDOW ||
        idempotency_window_ > constants::MAX_INVENTORY_IDEMPOTENCY_WINDOW) {
        idempotency_window_ = constants::DEFAULT_INVENTORY_IDEMPOTENCY_WINDOW;
    }
    std::sort(known_ecu_ids_.begin(), known_ecu_ids_.end());
    known_ecu_ids_.erase(std::unique(known_ecu_ids_.begin(), known_ecu_ids_.end()),
                         known_ecu_ids_.end());
}

bool FotaVersionRegistrar::isValidVersionString(const std::string& version) {
    if (version.empty() ||
        version.size() > constants::MAX_VERSION_STRING_LENGTH) {
        return false;
    }
    const auto is_alnum = [](unsigned char c) {
        return std::isalnum(c) != 0;
    };
    if (!is_alnum(static_cast<unsigned char>(version.front()))) {
        return false;
    }
    for (char raw : version) {
        const auto c = static_cast<unsigned char>(raw);
        if (is_alnum(c)) continue;
        switch (raw) {
            case '.': case '_': case '+': case '~': case '-':
                continue;
            default:
                return false;
        }
    }
    return true;
}

bool FotaVersionRegistrar::isKnownEcu(const std::string& ecu_id,
                                      const EcuInventoryRecord& record) const {
    if (std::binary_search(known_ecu_ids_.begin(), known_ecu_ids_.end(), ecu_id)) {
        return true;
    }
    return record.findEcu(ecu_id) != nullptr;
}

std::string FotaVersionRegistrar::canonicalItemsDigest(
    const std::vector<InstalledVersionItem>& items) {
    std::vector<const InstalledVersionItem*> ordered;
    ordered.reserve(items.size());
    for (const auto& item : items) ordered.push_back(&item);
    std::sort(ordered.begin(), ordered.end(),
              [](const InstalledVersionItem* a, const InstalledVersionItem* b) {
                  return std::tie(a->ecu_id, a->package_id, a->package_revision,
                                  a->installed_version) <
                         std::tie(b->ecu_id, b->package_id, b->package_revision,
                                  b->installed_version);
              });

    json array = json::array();
    for (const auto* item : ordered) {
        json entry;
        entry["ecuId"] = item->ecu_id;
        entry["packageId"] = item->package_id;
        entry["packageRevision"] = item->package_revision;
        entry["installedVersion"] = item->installed_version;
        array.push_back(std::move(entry));
    }
    return EcuInventoryStore::sha256Prefixed(array.dump());
}

std::string FotaVersionRegistrar::makeRegistrationId(const std::string& key,
                                                     const std::string& digest) {
    const std::string hex = ccu::fw::hash::sha256_hex(key + "|" + digest);
    std::string id = "REG-";
    for (size_t i = 0; i < 12 && i < hex.size(); ++i) {
        id.push_back(static_cast<char>(std::toupper(
            static_cast<unsigned char>(hex[i]))));
    }
    return id;
}

InstalledVersionsResponse FotaVersionRegistrar::recordInstalledVersions(
    const InstalledVersionsRequest& request) {
    if (!store_ || !store_->available()) {
        // Inventory fact store is not usable: retryable, not a permanent reject.
        InstalledVersionsResponse response;
        response.schema_version = constants::DIAG_PAYLOAD_SCHEMA_VERSION;
        response.disposition = RegistrationDisposition::RETRYABLE_FAILED;
        response.error_code = ErrorCodeMapper::toDiagCode(
            ErrorCode::REGISTRATION_PERSIST_FAILED);
        return response;
    }

    if (request.schema_version != constants::DIAG_PAYLOAD_SCHEMA_VERSION ||
        request.execution_id.empty() || request.attempt_no == 0 ||
        request.items.empty()) {
        auto response = makeRejected(
            ErrorCodeMapper::toDiagCode(ErrorCode::REGISTRATION_INVALID));
        response.rejected_items.push_back(
            RejectedVersionItem{"", REASON_MISSING_FIELD});
        return response;
    }

    const EcuInventoryRecord& current = store_->record();

    // ---- per-item validation: reject the whole request, never partial write ----
    std::vector<RejectedVersionItem> rejected;
    for (const auto& item : request.items) {
        if (item.ecu_id.empty() || item.installed_version.empty()) {
            rejected.push_back(RejectedVersionItem{item.ecu_id, REASON_MISSING_FIELD});
            continue;
        }
        if (item.ecu_id.size() > constants::MAX_ECU_ID_LENGTH ||
            item.package_id.size() > constants::MAX_PART_NUMBER_LENGTH ||
            item.package_revision.size() > constants::MAX_VERSION_STRING_LENGTH) {
            rejected.push_back(RejectedVersionItem{item.ecu_id, REASON_FIELD_TOO_LONG});
            continue;
        }
        if (!isKnownEcu(item.ecu_id, current)) {
            rejected.push_back(RejectedVersionItem{item.ecu_id, REASON_UNKNOWN_ECU});
            continue;
        }
        if (!isValidVersionString(item.installed_version)) {
            rejected.push_back(RejectedVersionItem{item.ecu_id, REASON_INVALID_VERSION});
            continue;
        }
    }
    if (!rejected.empty()) {
        auto response = makeRejected(
            ErrorCodeMapper::toDiagCode(ErrorCode::REGISTRATION_INVALID));
        response.rejected_items = std::move(rejected);
        return response;
    }

    const std::string digest = canonicalItemsDigest(request.items);
    const std::string key =
        request.execution_id + ":" + std::to_string(request.attempt_no);

    // ---- idempotency: executionId + attemptNo, content digest must match ----
    for (const auto& applied : current.applied) {
        if (applied.execution_id != request.execution_id ||
            applied.attempt_no != request.attempt_no) {
            continue;
        }
        if (applied.items_digest == digest) {
            InstalledVersionsResponse response;
            response.schema_version = constants::DIAG_PAYLOAD_SCHEMA_VERSION;
            response.disposition = RegistrationDisposition::DUPLICATE;
            response.registration_id = applied.registration_id;
            return response;
        }
        auto response = makeRejected(ErrorCodeMapper::toDiagCode(
            ErrorCode::REGISTRATION_IDEMPOTENCY_CONFLICT));
        for (const auto& item : request.items) {
            response.rejected_items.push_back(
                RejectedVersionItem{item.ecu_id, REASON_IDEMPOTENCY_CONFLICT});
        }
        return response;
    }

    const std::string registration_id = makeRegistrationId(key, digest);

    // ---- apply: software version facts + updatedAt only (D7) ----
    EcuInventoryRecord updated = current;
    for (const auto& item : request.items) {
        auto* fact = updated.findEcu(item.ecu_id);
        if (fact == nullptr) {
            InventoryEcuFact created;
            created.ecu_id = item.ecu_id;
            updated.ecus.push_back(std::move(created));
            fact = &updated.ecus.back();
        }
        fact->sw_version = item.installed_version;
    }
    updated.updated_at = EcuInventoryStore::nowIso8601();
    updated.applied.push_back(AppliedRegistration{
        request.execution_id, request.attempt_no, registration_id,
        EcuInventoryStore::isoFromEpochMs(request.applied_at_ms), digest});
    EcuInventoryStore::trimApplied(updated, idempotency_window_);

    // Content + idempotency ring in one atomic commit.
    const auto persisted = store_->persist(std::move(updated));
    if (!persisted.ok()) {
        InstalledVersionsResponse response;
        response.schema_version = constants::DIAG_PAYLOAD_SCHEMA_VERSION;
        response.disposition = RegistrationDisposition::RETRYABLE_FAILED;
        response.error_code = ErrorCodeMapper::toDiagCode(
            ErrorCode::REGISTRATION_PERSIST_FAILED);
        return response;
    }

    InstalledVersionsResponse response;
    response.schema_version = constants::DIAG_PAYLOAD_SCHEMA_VERSION;
    response.disposition = RegistrationDisposition::APPLIED;
    response.registration_id = registration_id;
    response.applied_count = static_cast<uint32_t>(request.items.size());
    response.effective_updated_at = store_->record().updated_at;
    return response;
}

} // namespace ccu_diag
