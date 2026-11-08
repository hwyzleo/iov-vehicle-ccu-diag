#pragma once

// =============================================================================
// include/ecu_inventory_store.h
// Local ECU inventory facts (CCU-DIAG-DSN-CR-010 D1/D2/D6/D9/D10)
// =============================================================================
// The store owns the authoritative ECU version facts:
//   * instance state: /var/lib/ccu/diag/ecu_inventory.json (preserve on
//     deploy/rollback, never release-managed),
//   * deployment seed: read-only JSON imported once when mode=seed and no
//     record exists.
// The record and the bounded idempotency ring (recent executionId/attemptNo)
// live in ONE file and are committed by a single atomic replacement, so there
// is no "content updated but idempotency key missing" window.
// Corruption / checksum mismatch / unknown schemaVersion -> CCU-DIAG-1012 and
// fail-closed: never rebuild silently, never answer with an empty inventory.
// =============================================================================

#include "data_models.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace ccu_diag {

struct InventoryEcuFact {
    std::string ecu_id;
    std::string part_number;
    std::string sw_version;
    std::string hw_version;
    // CCU-DIAG-DSN-CR-011: ECU serial number (DID 0xF18C), optional. Additive
    // field: schemaVersion stays 1 while the contract is still in development.
    std::string serial_number;
};

struct AppliedRegistration {
    std::string execution_id;
    uint32_t attempt_no = 0;
    std::string registration_id;
    std::string applied_at;
    std::string items_digest;
};

struct EcuInventoryRecord {
    uint32_t schema_version = 1;
    std::string registry_version;
    std::string baseline_code;
    std::string inventory_source;  // "hardware" | "seed"
    std::string seed_digest;
    std::string updated_at;
    std::vector<InventoryEcuFact> ecus;
    std::vector<AppliedRegistration> applied;

    const InventoryEcuFact* findEcu(const std::string& ecu_id) const;
    InventoryEcuFact* findEcu(const std::string& ecu_id);
};

enum class InventoryStoreStatus {
    OK,
    NOT_FOUND,
    CORRUPT,       // CCU-DIAG-1012
    SEED_INVALID,  // CCU-DIAG-1013
    IO_ERROR       // CCU-DIAG-1015 (write path)
};

struct InventoryStoreResult {
    InventoryStoreStatus status = InventoryStoreStatus::OK;
    std::string detail;  // file path / field name / errno only, never content

    bool ok() const { return status == InventoryStoreStatus::OK; }
    std::string diagCode() const;
};

// File/fsync/rename seam (D2/D6). Tests inject failures by overriding steps;
// production uses POSIX open/write/fsync/rename/fsync(dir).
class InventoryFileAdapter {
public:
    virtual ~InventoryFileAdapter() = default;

    virtual bool exists(const std::string& path) const;
    virtual bool readFile(const std::string& path, std::string& out,
                          std::string& error) const;
    virtual bool removeFile(const std::string& path) const;
    virtual bool ensureParentDirectory(const std::string& path,
                                       std::string& error) const;
    // Write + fsync the temporary file (does not rename).
    virtual bool writeFileFsync(const std::string& path, const std::string& data,
                                std::string& error) const;
    virtual bool renameFile(const std::string& from, const std::string& to,
                            std::string& error) const;
    virtual bool fsyncDirectory(const std::string& dir, std::string& error) const;
};

class EcuInventoryStore {
public:
    EcuInventoryStore(const InventoryConfig& config,
                      std::vector<std::string> known_ecu_ids,
                      std::shared_ptr<InventoryFileAdapter> adapter = nullptr,
                      bool enforce_state_path = true);

    // Startup: clean stale *.tmp, load/validate the record, import the seed
    // once when mode=seed and no record exists (fail-closed on any error).
    InventoryStoreResult initialize(const std::string& registry_version);

    bool initialized() const { return initialized_; }
    bool available() const { return available_; }
    InventoryMode mode() const { return config_.mode; }
    const InventoryConfig& config() const { return config_; }
    const std::vector<std::string>& knownEcuIds() const { return known_ecu_ids_; }
    const EcuInventoryRecord& record() const { return record_; }
    bool persistentRecordExists() const;

    // Result of the last initialize(); lets the collector propagate 1012 vs
    // 1013 instead of collapsing every failure into "unavailable".
    InventoryStoreResult startupResult() const;

    // Single atomic commit of content + idempotency ring. On failure the
    // previous file and the in-memory record are unchanged.
    InventoryStoreResult persist(EcuInventoryRecord record);

    // Parse + validate the deployment seed and build a record (no write).
    InventoryStoreResult importSeed(const std::string& registry_version,
                                    EcuInventoryRecord& out) const;

    static bool isValidStatePath(const std::string& path);
    static std::string sha256Prefixed(std::string_view data);
    static std::string nowIso8601();
    static std::string isoFromEpochMs(int64_t epoch_ms);

    // Exposed for tests / diagnostics (deterministic JSON + checksum).
    static std::string encodeRecordFile(const EcuInventoryRecord& record);
    static bool decodeRecordFile(const std::string& text, EcuInventoryRecord& out,
                                 std::string& detail);
    static bool parseSeedFile(const std::string& text,
                              const std::string& registry_version,
                              const std::vector<std::string>& known_ecu_ids,
                              EcuInventoryRecord& out, std::string& detail);

    // Trim the idempotency ring to the configured window (oldest first).
    static void trimApplied(EcuInventoryRecord& record, uint32_t window);

private:
    InventoryStoreResult loadExisting();
    InventoryStoreResult buildHardwareSkeleton(const std::string& registry_version);
    std::string temporaryPath() const;

    InventoryConfig config_;
    std::vector<std::string> known_ecu_ids_;
    std::shared_ptr<InventoryFileAdapter> adapter_;
    EcuInventoryRecord record_;
    bool initialized_ = false;
    bool available_ = false;
    bool record_on_disk_ = false;
    bool enforce_state_path_ = true;
    InventoryStoreResult startup_result_;
};

} // namespace ccu_diag
