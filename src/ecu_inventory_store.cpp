// =============================================================================
// src/ecu_inventory_store.cpp
// Local ECU inventory facts (CCU-DIAG-DSN-CR-010 §4.1 / D1/D2/D6/D9/D10)
// =============================================================================
// Guarantees:
//   * single-key atomic commit: *.tmp -> write -> fsync(file) -> rename ->
//     fsync(dir); any failure leaves the previous file intact (a failure after
//     rename keeps the new file and reports a retryable error),
//   * record integrity: sha256 over the canonical payload JSON; unknown
//     schemaVersion / checksum mismatch / malformed record -> CCU-DIAG-1012,
//   * seed import: format/membership validation -> CCU-DIAG-1013, imported
//     once only (never overwrites an existing record),
//   * fail-closed: no silent rebuild, no "success with an empty inventory".
// Logs/errors carry paths, field names and error codes only (no content, no
// VIN). The store itself does not log (the caller owns the log adapter).
// =============================================================================

#include "ecu_inventory_store.h"
#include "constants.h"
#include "error_codes.h"

#include <ccu/fw/hash/sha256.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <set>
#include <sstream>

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace ccu_diag {

namespace {

using json = nlohmann::json;

constexpr size_t MAX_TEXT_FIELD = 256;

bool bounded(const std::string& value, size_t max) {
    return value.size() <= max;
}

bool getString(const json& node, const char* key, std::string& out, bool required,
               std::string& detail) {
    auto it = node.find(key);
    if (it == node.end() || it->is_null()) {
        if (!required) {
            out.clear();
            return true;
        }
        detail = std::string("missing field: ") + key;
        return false;
    }
    if (!it->is_string()) {
        detail = std::string("field type mismatch (expected string): ") + key;
        return false;
    }
    out = it->get<std::string>();
    if (!bounded(out, MAX_TEXT_FIELD)) {
        detail = std::string("field too long: ") + key;
        return false;
    }
    return true;
}

bool getUint(const json& node, const char* key, uint32_t& out, bool required,
             std::string& detail) {
    auto it = node.find(key);
    if (it == node.end() || it->is_null()) {
        if (!required) {
            out = 0;
            return true;
        }
        detail = std::string("missing field: ") + key;
        return false;
    }
    if (!it->is_number_unsigned() && !it->is_number_integer()) {
        detail = std::string("field type mismatch (expected integer): ") + key;
        return false;
    }
    const int64_t value = it->get<int64_t>();
    if (value < 0 || value > 0xFFFFFFFFLL) {
        detail = std::string("out of range: ") + key;
        return false;
    }
    out = static_cast<uint32_t>(value);
    return true;
}

std::string parentDirectory(const std::string& path) {
    std::filesystem::path p(path);
    const auto parent = p.parent_path();
    return parent.empty() ? std::string(".") : parent.string();
}

std::string formatUtc(std::time_t value) {
    struct tm tm_buf {};
    gmtime_r(&value, &tm_buf);
    std::stringstream ss;
    ss << std::put_time(&tm_buf, "%Y-%m-%dT%H:%M:%SZ");
    return ss.str();
}

} // namespace

// ---------------------------------------------------------------------------
// Record helpers
// ---------------------------------------------------------------------------

const InventoryEcuFact* EcuInventoryRecord::findEcu(const std::string& ecu_id) const {
    for (const auto& fact : ecus) {
        if (fact.ecu_id == ecu_id) return &fact;
    }
    return nullptr;
}

InventoryEcuFact* EcuInventoryRecord::findEcu(const std::string& ecu_id) {
    for (auto& fact : ecus) {
        if (fact.ecu_id == ecu_id) return &fact;
    }
    return nullptr;
}

std::string InventoryStoreResult::diagCode() const {
    switch (status) {
        case InventoryStoreStatus::OK: return ErrorCodeMapper::toDiagCode(ErrorCode::SUCCESS);
        case InventoryStoreStatus::NOT_FOUND: return "CCU-DIAG-1012";
        case InventoryStoreStatus::CORRUPT: return "CCU-DIAG-1012";
        case InventoryStoreStatus::SEED_INVALID: return "CCU-DIAG-1013";
        case InventoryStoreStatus::IO_ERROR: return "CCU-DIAG-1015";
    }
    return "CCU-DIAG-1012";
}

void EcuInventoryStore::trimApplied(EcuInventoryRecord& record, uint32_t window) {
    if (window == 0) window = 1;
    if (record.applied.size() <= window) return;
    const size_t drop = record.applied.size() - window;
    record.applied.erase(record.applied.begin(),
                         record.applied.begin() + static_cast<std::ptrdiff_t>(drop));
}

// ---------------------------------------------------------------------------
// Default POSIX file adapter
// ---------------------------------------------------------------------------

bool InventoryFileAdapter::exists(const std::string& path) const {
    std::error_code ec;
    return std::filesystem::exists(path, ec);
}

bool InventoryFileAdapter::readFile(const std::string& path, std::string& out,
                                    std::string& error) const {
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
        error = "open failed";
        return false;
    }
    std::ostringstream buffer;
    buffer << file.rdbuf();
    out = buffer.str();
    if (file.bad()) {
        error = "read failed";
        return false;
    }
    return true;
}

bool InventoryFileAdapter::removeFile(const std::string& path) const {
    std::error_code ec;
    std::filesystem::remove(path, ec);
    return !ec;
}

bool InventoryFileAdapter::ensureParentDirectory(const std::string& path,
                                                 std::string& error) const {
    const std::string parent = parentDirectory(path);
    std::error_code ec;
    std::filesystem::create_directories(parent, ec);
    if (ec && !std::filesystem::is_directory(parent)) {
        error = "create directory failed";
        return false;
    }
    return true;
}

bool InventoryFileAdapter::writeFileFsync(const std::string& path,
                                          const std::string& data,
                                          std::string& error) const {
    const int fd = ::open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
    if (fd < 0) {
        error = std::string("open failed: ") + std::strerror(errno);
        return false;
    }

    size_t written = 0;
    while (written < data.size()) {
        const ssize_t n = ::write(fd, data.data() + written, data.size() - written);
        if (n < 0) {
            if (errno == EINTR) continue;
            error = std::string("write failed: ") + std::strerror(errno);
            ::close(fd);
            return false;
        }
        written += static_cast<size_t>(n);
    }

    if (::fsync(fd) != 0) {
        error = std::string("fsync failed: ") + std::strerror(errno);
        ::close(fd);
        return false;
    }
    if (::close(fd) != 0) {
        error = std::string("close failed: ") + std::strerror(errno);
        return false;
    }
    return true;
}

bool InventoryFileAdapter::renameFile(const std::string& from, const std::string& to,
                                      std::string& error) const {
    if (::rename(from.c_str(), to.c_str()) != 0) {
        error = std::string("rename failed: ") + std::strerror(errno);
        return false;
    }
    return true;
}

bool InventoryFileAdapter::fsyncDirectory(const std::string& dir,
                                          std::string& error) const {
    const int fd = ::open(dir.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (fd < 0) {
        error = std::string("open directory failed: ") + std::strerror(errno);
        return false;
    }
    if (::fsync(fd) != 0) {
        const int saved = errno;
        ::close(fd);
        // Some filesystems/platforms (e.g. macOS dev hosts) do not support
        // directory fsync; the rename is already durable enough there.
        if (saved == EINVAL || saved == ENOTSUP || saved == EBADF) {
            return true;
        }
        error = std::string("fsync directory failed: ") + std::strerror(saved);
        return false;
    }
    ::close(fd);
    return true;
}

// ---------------------------------------------------------------------------
// Store
// ---------------------------------------------------------------------------

EcuInventoryStore::EcuInventoryStore(const InventoryConfig& config,
                                     std::vector<std::string> known_ecu_ids,
                                     std::shared_ptr<InventoryFileAdapter> adapter,
                                     bool enforce_state_path)
    : config_(config),
      known_ecu_ids_(std::move(known_ecu_ids)),
      adapter_(adapter ? std::move(adapter) : std::make_shared<InventoryFileAdapter>()),
      enforce_state_path_(enforce_state_path) {
    if (config_.state_path.empty()) {
        config_.state_path = constants::DEFAULT_INVENTORY_STATE_PATH;
    }
    if (config_.seed_path.empty()) {
        config_.seed_path = constants::DEFAULT_INVENTORY_SEED_PATH;
    }
    if (config_.idempotency_window < constants::MIN_INVENTORY_IDEMPOTENCY_WINDOW ||
        config_.idempotency_window > constants::MAX_INVENTORY_IDEMPOTENCY_WINDOW) {
        config_.idempotency_window = constants::DEFAULT_INVENTORY_IDEMPOTENCY_WINDOW;
    }
    std::sort(known_ecu_ids_.begin(), known_ecu_ids_.end());
    known_ecu_ids_.erase(std::unique(known_ecu_ids_.begin(), known_ecu_ids_.end()),
                         known_ecu_ids_.end());
}

std::string EcuInventoryStore::temporaryPath() const {
    return config_.state_path + ".tmp";
}

bool EcuInventoryStore::persistentRecordExists() const {
    return adapter_->exists(config_.state_path);
}

InventoryStoreResult EcuInventoryStore::initialize(const std::string& registry_version) {
    initialized_ = true;
    available_ = false;
    startup_result_ = InventoryStoreResult{};
    record_ = EcuInventoryRecord{};
    record_.schema_version = constants::ECU_INVENTORY_RECORD_SCHEMA_VERSION;
    record_.registry_version = registry_version;
    record_on_disk_ = adapter_->exists(config_.state_path);

    // Stale temp file from an interrupted write: remove it, keep the old file.
    adapter_->removeFile(temporaryPath());

    if (record_on_disk_) {
        auto result = loadExisting();
        available_ = result.ok();
        startup_result_ = result;
        return result;
    }

    if (config_.mode == InventoryMode::SEED) {
        EcuInventoryRecord seed_record;
        auto import = importSeed(registry_version, seed_record);
        if (!import.ok()) {
            startup_result_ = import;
            return import;
        }
        // First import is committed immediately so later restarts are no-ops.
        auto persisted = persist(seed_record);
        if (!persisted.ok()) {
            startup_result_ = InventoryStoreResult{
                InventoryStoreStatus::SEED_INVALID,
                "seed persist failed: " + persisted.detail};
            return startup_result_;
        }
        available_ = true;
        return InventoryStoreResult{};
    }

    auto skeleton = buildHardwareSkeleton(registry_version);
    if (!skeleton.ok()) {
        startup_result_ = skeleton;
        return skeleton;
    }
    available_ = true;
    return InventoryStoreResult{};
}

InventoryStoreResult EcuInventoryStore::startupResult() const {
    return startup_result_;
}

InventoryStoreResult EcuInventoryStore::loadExisting() {
    std::string text;
    std::string error;
    if (!adapter_->readFile(config_.state_path, text, error)) {
        return InventoryStoreResult{InventoryStoreStatus::CORRUPT,
                                    "inventory read failed: " + error};
    }

    EcuInventoryRecord record;
    std::string detail;
    if (!decodeRecordFile(text, record, detail)) {
        return InventoryStoreResult{InventoryStoreStatus::CORRUPT, detail};
    }
    trimApplied(record, config_.idempotency_window);
    record_ = std::move(record);
    return InventoryStoreResult{};
}

InventoryStoreResult EcuInventoryStore::buildHardwareSkeleton(
    const std::string& registry_version) {
    for (const auto& ecu_id : known_ecu_ids_) {
        InventoryEcuFact fact;
        fact.ecu_id = ecu_id;
        record_.ecus.push_back(std::move(fact));
    }
    record_.schema_version = constants::ECU_INVENTORY_RECORD_SCHEMA_VERSION;
    record_.registry_version = registry_version;
    record_.inventory_source = inventorySourceToString(InventorySource::HARDWARE);
    record_.baseline_code.clear();
    record_.seed_digest.clear();
    record_.updated_at = nowIso8601();
    record_.applied.clear();
    return InventoryStoreResult{};
}

InventoryStoreResult EcuInventoryStore::persist(EcuInventoryRecord record) {
    if (enforce_state_path_ && !isValidStatePath(config_.state_path)) {
        return InventoryStoreResult{InventoryStoreStatus::IO_ERROR,
                                    "state_path rejected by fail-closed check"};
    }

    record.schema_version = constants::ECU_INVENTORY_RECORD_SCHEMA_VERSION;
    trimApplied(record, config_.idempotency_window);

    const std::string data = encodeRecordFile(record);
    const std::string tmp = temporaryPath();
    std::string error;

    if (!adapter_->ensureParentDirectory(config_.state_path, error)) {
        return InventoryStoreResult{InventoryStoreStatus::IO_ERROR,
                                    "inventory directory unavailable: " + error};
    }
    if (!adapter_->writeFileFsync(tmp, data, error)) {
        adapter_->removeFile(tmp);
        return InventoryStoreResult{InventoryStoreStatus::IO_ERROR,
                                    "inventory write failed: " + error};
    }
    if (!adapter_->renameFile(tmp, config_.state_path, error)) {
        adapter_->removeFile(tmp);
        return InventoryStoreResult{InventoryStoreStatus::IO_ERROR,
                                    "inventory rename failed: " + error};
    }

    // Rename is committed: the new file is authoritative even if the directory
    // fsync cannot be confirmed. Keep memory in sync so a retry is detected as
    // DUPLICATE instead of applying twice.
    record_ = record;
    record_on_disk_ = true;

    if (!adapter_->fsyncDirectory(parentDirectory(config_.state_path), error)) {
        return InventoryStoreResult{InventoryStoreStatus::IO_ERROR,
                                    "inventory directory fsync failed: " + error};
    }
    return InventoryStoreResult{};
}

InventoryStoreResult EcuInventoryStore::importSeed(const std::string& registry_version,
                                                   EcuInventoryRecord& out) const {
    std::string text;
    std::string error;
    if (!adapter_->exists(config_.seed_path)) {
        return InventoryStoreResult{InventoryStoreStatus::SEED_INVALID,
                                    "seed file missing"};
    }
    if (!adapter_->readFile(config_.seed_path, text, error)) {
        return InventoryStoreResult{InventoryStoreStatus::SEED_INVALID,
                                    "seed read failed: " + error};
    }

    std::string detail;
    if (!parseSeedFile(text, registry_version, known_ecu_ids_, out, detail)) {
        return InventoryStoreResult{InventoryStoreStatus::SEED_INVALID, detail};
    }
    return InventoryStoreResult{};
}

bool EcuInventoryStore::isValidStatePath(const std::string& path) {
    if (path.empty() || path.front() != '/') {
        return false;
    }
    const std::filesystem::path normalized =
        std::filesystem::path(path).lexically_normal();
    const std::string text = normalized.string();
    const std::string allowed = constants::INVENTORY_STATE_DIRECTORY;
    if (text.size() <= allowed.size()) {
        return false;
    }
    if (text.compare(0, allowed.size(), allowed) != 0) {
        return false;
    }
    const std::string filename = normalized.filename().string();
    return !filename.empty() && filename != "." && filename != "..";
}

std::string EcuInventoryStore::sha256Prefixed(std::string_view data) {
    return "sha256:" + ccu::fw::hash::sha256_hex(data);
}

std::string EcuInventoryStore::nowIso8601() {
    return formatUtc(std::chrono::system_clock::to_time_t(
        std::chrono::system_clock::now()));
}

std::string EcuInventoryStore::isoFromEpochMs(int64_t epoch_ms) {
    if (epoch_ms <= 0) {
        return nowIso8601();
    }
    const std::time_t seconds = static_cast<std::time_t>(epoch_ms / 1000);
    return formatUtc(seconds);
}

std::string EcuInventoryStore::encodeRecordFile(const EcuInventoryRecord& record) {
    json payload;
    payload["registryVersion"] = record.registry_version;
    payload["baselineCode"] = record.baseline_code;
    payload["inventorySource"] = record.inventory_source;
    payload["seedDigest"] = record.seed_digest;
    payload["updatedAt"] = record.updated_at;

    json ecus = json::array();
    for (const auto& fact : record.ecus) {
        json entry;
        entry["ecuId"] = fact.ecu_id;
        entry["partNumber"] = fact.part_number;
        // CCU-DIAG-DSN-CR-012: software part number is part of the record
        // payload, so the record checksum covers it.
        entry["softwarePartNumber"] = fact.software_part_number;
        entry["swVersion"] = fact.sw_version;
        entry["hwVersion"] = fact.hw_version;
        entry["serialNumber"] = fact.serial_number;
        ecus.push_back(std::move(entry));
    }
    payload["ecus"] = std::move(ecus);

    json applied = json::array();
    for (const auto& entry : record.applied) {
        json item;
        item["executionId"] = entry.execution_id;
        item["attemptNo"] = entry.attempt_no;
        item["registrationId"] = entry.registration_id;
        item["appliedAt"] = entry.applied_at;
        item["itemsDigest"] = entry.items_digest;
        applied.push_back(std::move(item));
    }
    payload["applied"] = std::move(applied);

    json root;
    root["schemaVersion"] = record.schema_version;
    root["checksum"] = sha256Prefixed(payload.dump());
    root["payload"] = std::move(payload);
    return root.dump(2);
}

bool EcuInventoryStore::decodeRecordFile(const std::string& text,
                                         EcuInventoryRecord& out,
                                         std::string& detail) {
    json root;
    try {
        root = json::parse(text);
    } catch (const json::exception&) {
        detail = "record is not valid json";
        return false;
    }
    if (!root.is_object()) {
        detail = "record is not a json object";
        return false;
    }

    uint32_t schema_version = 0;
    if (!getUint(root, "schemaVersion", schema_version, true, detail)) {
        return false;
    }
    if (schema_version != constants::ECU_INVENTORY_RECORD_SCHEMA_VERSION) {
        // Unknown (newer or undefined older) version: fail-closed, never
        // overwrite or rebuild the record.
        detail = "unsupported record schemaVersion";
        return false;
    }

    std::string checksum;
    if (!getString(root, "checksum", checksum, true, detail)) {
        return false;
    }
    auto payload_it = root.find("payload");
    if (payload_it == root.end() || !payload_it->is_object()) {
        detail = "missing field: payload";
        return false;
    }
    const json& payload = *payload_it;
    if (sha256Prefixed(payload.dump()) != checksum) {
        detail = "checksum mismatch";
        return false;
    }

    EcuInventoryRecord record;
    record.schema_version = schema_version;
    if (!getString(payload, "registryVersion", record.registry_version, true, detail) ||
        !getString(payload, "baselineCode", record.baseline_code, false, detail) ||
        !getString(payload, "inventorySource", record.inventory_source, true, detail) ||
        !getString(payload, "seedDigest", record.seed_digest, false, detail) ||
        !getString(payload, "updatedAt", record.updated_at, true, detail)) {
        return false;
    }
    if (record.inventory_source != "hardware" && record.inventory_source != "seed") {
        detail = "invalid inventorySource";
        return false;
    }

    auto ecus_it = payload.find("ecus");
    if (ecus_it == payload.end() || !ecus_it->is_array()) {
        detail = "missing field: ecus";
        return false;
    }
    std::set<std::string> seen;
    for (const auto& node : *ecus_it) {
        if (!node.is_object()) {
            detail = "ecus entry is not an object";
            return false;
        }
        InventoryEcuFact fact;
        // softwarePartNumber is optional at decode time: a record written before
        // CCU-DIAG-DSN-CR-012 simply reads back as "unknown", never backfilled
        // from partNumber or serialNumber.
        if (!getString(node, "ecuId", fact.ecu_id, true, detail) ||
            !getString(node, "partNumber", fact.part_number, false, detail) ||
            !getString(node, "softwarePartNumber", fact.software_part_number,
                       false, detail) ||
            !getString(node, "swVersion", fact.sw_version, false, detail) ||
            !getString(node, "hwVersion", fact.hw_version, false, detail) ||
            !getString(node, "serialNumber", fact.serial_number, false, detail)) {
            return false;
        }
        if (fact.ecu_id.empty() ||
            !bounded(fact.ecu_id, constants::MAX_ECU_ID_LENGTH)) {
            detail = "invalid ecuId";
            return false;
        }
        if (!bounded(fact.part_number, constants::MAX_PART_NUMBER_LENGTH) ||
            !bounded(fact.software_part_number, constants::MAX_PART_NUMBER_LENGTH) ||
            !bounded(fact.sw_version, constants::MAX_VERSION_STRING_LENGTH) ||
            !bounded(fact.hw_version, constants::MAX_VERSION_STRING_LENGTH) ||
            !bounded(fact.serial_number, constants::MAX_SERIAL_NUMBER_LENGTH)) {
            detail = "ecu fact field too long";
            return false;
        }
        if (!seen.insert(fact.ecu_id).second) {
            detail = "duplicate ecuId";
            return false;
        }
        record.ecus.push_back(std::move(fact));
    }

    auto applied_it = payload.find("applied");
    if (applied_it == payload.end() || !applied_it->is_array()) {
        detail = "missing field: applied";
        return false;
    }
    for (const auto& node : *applied_it) {
        if (!node.is_object()) {
            detail = "applied entry is not an object";
            return false;
        }
        AppliedRegistration entry;
        if (!getString(node, "executionId", entry.execution_id, true, detail) ||
            !getUint(node, "attemptNo", entry.attempt_no, true, detail) ||
            !getString(node, "registrationId", entry.registration_id, true, detail) ||
            !getString(node, "appliedAt", entry.applied_at, true, detail) ||
            !getString(node, "itemsDigest", entry.items_digest, true, detail)) {
            return false;
        }
        record.applied.push_back(std::move(entry));
    }

    out = std::move(record);
    return true;
}

bool EcuInventoryStore::parseSeedFile(
    const std::string& text, const std::string& registry_version,
    const std::vector<std::string>& known_ecu_ids, EcuInventoryRecord& out,
    std::string& detail) {
    json root;
    try {
        root = json::parse(text);
    } catch (const json::exception&) {
        detail = "seed is not valid json";
        return false;
    }
    if (!root.is_object()) {
        detail = "seed is not a json object";
        return false;
    }

    uint32_t schema_version = 0;
    if (!getUint(root, "schemaVersion", schema_version, true, detail)) {
        return false;
    }
    if (schema_version != constants::ECU_INVENTORY_RECORD_SCHEMA_VERSION) {
        detail = "unsupported seed schemaVersion";
        return false;
    }

    EcuInventoryRecord record;
    record.schema_version = schema_version;
    if (!getString(root, "registryVersion", record.registry_version, true, detail)) {
        return false;
    }
    if (record.registry_version.empty()) {
        detail = "missing field: registryVersion";
        return false;
    }
    if (!getString(root, "baselineCode", record.baseline_code, false, detail)) {
        return false;
    }

    auto ecus_it = root.find("ecus");
    if (ecus_it == root.end() || !ecus_it->is_array()) {
        detail = "missing field: ecus";
        return false;
    }
    if (ecus_it->empty()) {
        detail = "seed ecus is empty";
        return false;
    }
    if (known_ecu_ids.empty()) {
        detail = "DidRegistry has no registered ECUs";
        return false;
    }

    std::set<std::string> registered(known_ecu_ids.begin(), known_ecu_ids.end());
    std::set<std::string> seen;
    for (const auto& node : *ecus_it) {
        if (!node.is_object()) {
            detail = "seed ecus entry is not an object";
            return false;
        }
        InventoryEcuFact fact;
        if (!getString(node, "ecuId", fact.ecu_id, true, detail) ||
            !getString(node, "partNumber", fact.part_number, false, detail) ||
            !getString(node, "softwarePartNumber", fact.software_part_number,
                       false, detail) ||
            !getString(node, "swVersion", fact.sw_version, false, detail) ||
            !getString(node, "hwVersion", fact.hw_version, false, detail) ||
            !getString(node, "serialNumber", fact.serial_number, false, detail)) {
            return false;
        }
        if (fact.ecu_id.empty() ||
            !bounded(fact.ecu_id, constants::MAX_ECU_ID_LENGTH)) {
            detail = "invalid seed ecuId";
            return false;
        }
        if (!bounded(fact.part_number, constants::MAX_PART_NUMBER_LENGTH) ||
            !bounded(fact.software_part_number, constants::MAX_PART_NUMBER_LENGTH) ||
            !bounded(fact.sw_version, constants::MAX_VERSION_STRING_LENGTH) ||
            !bounded(fact.hw_version, constants::MAX_VERSION_STRING_LENGTH) ||
            !bounded(fact.serial_number, constants::MAX_SERIAL_NUMBER_LENGTH)) {
            detail = "seed ecu field too long";
            return false;
        }
        // CCU-DIAG-DSN-CR-012 D4: a new seed must declare the approved software
        // part number and the software version for every ECU; otherwise the
        // record could never be reported as a complete SINGLE_IMAGE inventory.
        if (fact.software_part_number.empty()) {
            detail = "missing field: seed ecu softwarePartNumber";
            return false;
        }
        if (fact.sw_version.empty()) {
            detail = "missing field: seed ecu swVersion";
            return false;
        }
        if (registered.find(fact.ecu_id) == registered.end()) {
            detail = "seed ecuId not registered in DidRegistry";
            return false;
        }
        if (!seen.insert(fact.ecu_id).second) {
            detail = "duplicate seed ecuId";
            return false;
        }
        record.ecus.push_back(std::move(fact));
    }

    record.inventory_source = inventorySourceToString(InventorySource::SEED);
    record.seed_digest = sha256Prefixed(text);
    record.updated_at = nowIso8601();
    record.applied.clear();
    (void)registry_version;  // seed declares its own registryVersion for audit
    out = std::move(record);
    return true;
}

} // namespace ccu_diag
