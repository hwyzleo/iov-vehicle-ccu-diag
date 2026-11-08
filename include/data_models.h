#pragma once

#include <string>
#include <vector>
#include <optional>
#include <cstdint>

namespace ccu_diag {

enum class ReadMethod {
    UDS_0x22,
    SOMEIP_GET_VERSION
};

enum class TransportType {
    DOCAN,
    DOIP
};

enum class CollectionStatus {
    ALL_OK,
    PARTIAL,
    FAILED
};

enum class EcuStatus {
    OK,
    MISSING,
    NRC,
    TIMEOUT,
    UNREACHABLE,
    PARSE_ERROR
};

enum class BaselineSource {
    FACTORY,
    LAST_OTA,
    UNKNOWN
};

enum class VinSource {
    PROVISIONED,
    UNKNOWN
};

std::string vinSourceToString(VinSource source);
VinSource stringToVinSource(const std::string& str);

// Snapshot-level inventory fact source (CCU-DIAG-DSN-CR-010 D4/D5).
// HARDWARE: values were read from the target ECUs.
// SEED: values come from the deployment seed declaration table (transitional).
enum class InventorySource {
    HARDWARE,
    SEED
};

std::string inventorySourceToString(InventorySource source);
InventorySource stringToInventorySource(const std::string& str);

// Inventory fact source mode; hardware | seed are mutually exclusive
// (no auto fallback, no per-ECU mixing - CCU-DIAG-DSN-CR-010 D4).
enum class InventoryMode {
    HARDWARE,
    SEED
};

std::string inventoryModeToString(InventoryMode mode);
InventoryMode stringToInventoryMode(const std::string& str);

// diag.inventory.* configuration subtree (CCU-DIAG-DSN-CR-010 §配置增量).
struct InventoryConfig {
    InventoryMode mode = InventoryMode::HARDWARE;
    std::string seed_path;
    std::string state_path;
    uint32_t idempotency_window = 64;
};

struct DidEntry {
    uint16_t did = 0;
    std::string field;
    std::string encoding;
    uint16_t length = 0;
};

struct EcuConfig {
    std::string ecu_id;
    uint16_t logical_address = 0;
    TransportType transport = TransportType::DOCAN;
    ReadMethod read_method = ReadMethod::UDS_0x22;
    bool security_required = false;
    std::vector<DidEntry> dids;
    std::string someip_service;  // Only for SOME/IP method
    std::vector<std::string> fields;  // Only for SOME/IP method
};

struct DidRegistryConfig {
    std::string registry_version;
    std::vector<uint16_t> baseline_dids;
    std::vector<EcuConfig> ecus;
};

struct EcuVersionEntry {
    std::string ecu_id;
    // CCU-DIAG-DSN-CR-012 D1: five independent facts, never derived from each
    // other. part_number = DID 0xF187 (assembly/spare part number),
    // software_part_number = DID 0xF188 (ECU software part number),
    // sw_version = DID 0xF195, hw_version = DID 0xF193,
    // serial_number = DID 0xF18C.
    std::optional<std::string> part_number;
    std::optional<std::string> software_part_number;
    std::optional<std::string> sw_version;
    std::optional<std::string> hw_version;
    // CCU-DIAG-DSN-CR-011: ECU serial number (DID 0xF18C), optional.
    std::optional<std::string> serial_number;
    ReadMethod source = ReadMethod::UDS_0x22;
    EcuStatus status = EcuStatus::OK;
    std::optional<std::string> error_code;
};

struct VehicleSoftwareSnapshot {
    std::string vin;
    VinSource vin_source = VinSource::UNKNOWN;
    std::optional<std::string> baseline_id;
    BaselineSource baseline_source = BaselineSource::UNKNOWN;
    std::string registry_version;
    std::string collected_at;
    CollectionStatus overall_result = CollectionStatus::ALL_OK;
    // 0x0002 payload version; freeze at 1 for the v1 field set.
    uint32_t schema_version = 1;
    InventorySource inventory_source = InventorySource::HARDWARE;
    std::vector<EcuVersionEntry> ecu_list;
};

// ---------------------------------------------------------------------------
// 0x0005 METHOD_RECORD_INSTALLED_VERSIONS DTOs (CCU-DIAG-DSN-CR-010 §5.2)
// ---------------------------------------------------------------------------

enum class RegistrationDisposition {
    APPLIED,
    DUPLICATE,
    REJECTED_INVALID,
    RETRYABLE_FAILED
};

std::string registrationDispositionToString(RegistrationDisposition disposition);

struct InstalledVersionItem {
    std::string ecu_id;
    std::string package_id;
    std::string package_revision;
    std::string installed_version;
};

struct InstalledVersionsRequest {
    uint32_t schema_version = 0;
    std::string vehicle_task_id;
    uint32_t task_revision = 0;
    std::string execution_id;
    uint32_t attempt_no = 0;
    std::string idempotency_key;
    int64_t applied_at_ms = 0;
    std::vector<InstalledVersionItem> items;
};

struct RejectedVersionItem {
    std::string ecu_id;
    std::string reason_code;
};

struct InstalledVersionsResponse {
    uint32_t schema_version = 1;
    RegistrationDisposition disposition = RegistrationDisposition::REJECTED_INVALID;
    std::string registration_id;
    uint32_t applied_count = 0;
    std::vector<RejectedVersionItem> rejected_items;
    std::string effective_updated_at;
    // Internal only (logs); never serialized as a top-level v1 field.
    std::string error_code;
};

struct DiagConfig {
    uint32_t p2_server_max_ms = 50;
    uint32_t p2_star_server_max_ms = 5000;
    uint32_t s3_session_timeout_ms = 5000;
    uint32_t tester_present_interval_ms = 2000;
    uint32_t request_timeout_ms = 2000;
    uint32_t retry_interval_ms = 100;
    uint8_t max_retry_count = 2;
};

// CAN / DoIP non-secret transport parameters (CCU-DIAG-DSN-CR-009 §5.1).
// Platform interface/IP values are supplied by the BUILD platform overlay;
// empty/zero fields fall back to constants::DEFAULT_* in ConfigLoader.
struct TransportConfig {
    std::string can_interface;
    uint32_t can_baudrate = 0;
    std::string doip_ip;
    uint16_t doip_port = 0;
};

} // namespace ccu_diag
