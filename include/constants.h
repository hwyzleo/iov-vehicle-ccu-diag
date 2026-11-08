#pragma once

#include <cstdint>
#include <string>
#include "someip_types.h"

namespace ccu_diag {
namespace constants {

// UDS Service IDs
constexpr uint8_t UDS_READ_DATA_BY_IDENTIFIER = 0x22;
constexpr uint8_t UDS_NEGATIVE_RESPONSE = 0x7F;
constexpr uint8_t UDS_RESPONSE_PENDING = 0x78;

// Common DID values
constexpr uint16_t DID_SOFTWARE_VERSION = 0xF195;
constexpr uint16_t DID_ECU_SOFTWARE_NUMBER = 0xF188;
constexpr uint16_t DID_PART_NUMBER = 0xF187;
constexpr uint16_t DID_HARDWARE_VERSION = 0xF193;

// Timing parameters (milliseconds)
constexpr uint32_t P2_SERVER_MAX_MS = 50;
constexpr uint32_t P2_STAR_SERVER_MAX_MS = 5000;
constexpr uint32_t S3_SESSION_TIMEOUT_MS = 5000;
constexpr uint32_t TESTER_PRESENT_INTERVAL_MS = 2000;
constexpr uint32_t REQUEST_TIMEOUT_MS = 2000;
constexpr uint32_t RETRY_INTERVAL_MS = 100;

// Retry parameters
constexpr uint8_t MAX_RETRY_COUNT = 2;

// Configuration file paths (CCU-DIAG-DSN-CR-009 §5.3)
// Production loads the unified diag.yaml plus the BUILD-owned common.yaml.
// Legacy per-domain files are development/test overrides only and are never
// relied on by the production unit.
inline const std::string DEFAULT_DIAG_CONFIG_PATH = "/etc/ccu/conf.d/diag.yaml";
inline const std::string DEFAULT_COMMON_CONFIG_PATH = "/etc/ccu/common.yaml";

// Device instance state (NOT release-managed configuration; preserve on
// deploy/rollback). CCU-DIAG-DSN-CR-009 §6.2 / CCU-DIAG-SPEC设计 §8.3.
inline const std::string DEFAULT_VIN_STORAGE_PATH = "/var/lib/ccu/diag/vin.dat";

// ECU inventory state (instance state, preserve) and deployment seed
// (release-managed, read-only). CCU-DIAG-DSN-CR-010 §数据模型 / §配置增量.
inline const std::string DEFAULT_INVENTORY_STATE_PATH =
    "/var/lib/ccu/diag/ecu_inventory.json";
inline const std::string DEFAULT_INVENTORY_SEED_PATH =
    "/usr/share/ccu-diag/ecu-inventory.seed.json";
inline const std::string INVENTORY_STATE_DIRECTORY = "/var/lib/ccu/diag/";
constexpr uint32_t DEFAULT_INVENTORY_IDEMPOTENCY_WINDOW = 64;
constexpr uint32_t MIN_INVENTORY_IDEMPOTENCY_WINDOW = 1;
constexpr uint32_t MAX_INVENTORY_IDEMPOTENCY_WINDOW = 256;

// 0x0002 payload v1 and inventory record schema version (frozen).
constexpr uint32_t DIAG_PAYLOAD_SCHEMA_VERSION = 1;
constexpr uint32_t ECU_INVENTORY_RECORD_SCHEMA_VERSION = 1;

// Bounded string limits for inventory facts / registration payloads.
constexpr size_t MAX_ECU_ID_LENGTH = 32;
constexpr size_t MAX_VERSION_STRING_LENGTH = 64;
constexpr size_t MAX_PART_NUMBER_LENGTH = 64;
// CCU-DIAG-DSN-CR-011: ECU serial number (DID 0xF18C). 20-char ASCII values
// are the current project format; 32 is the fail-closed upper bound.
constexpr size_t MAX_SERIAL_NUMBER_LENGTH = 32;
constexpr size_t MAX_EXECUTION_ID_LENGTH = 128;
constexpr size_t MAX_REGISTRATION_ITEMS = 256;

// Transport defaults
inline const std::string DEFAULT_CAN_INTERFACE = "can0";
constexpr uint32_t DEFAULT_CAN_BAUDRATE = 500000;
inline const std::string DEFAULT_DOIP_IP = "192.168.1.1";
constexpr uint16_t DEFAULT_DOIP_PORT = 13400;

// Application version
inline const std::string APP_VERSION = "1.0.0";

// SOME/IP Service IDs
// 来源：整车 SOME/IP Service Registry 统一分配（CCU-DIAG-DSN-CR-007）
constexpr ServiceId CCU_DIAG_SERVICE_ID = 0x1110;
constexpr uint16_t CCU_DIAG_INSTANCE_ID = 0x0001;
constexpr uint8_t CCU_DIAG_MAJOR_VERSION = 1;
constexpr uint32_t CCU_DIAG_MINOR_VERSION = 0;

// SOME/IP Method IDs (CCU-DIAG-DSN-CR-006)
constexpr MethodId METHOD_READ_VIN = 0x0001;
constexpr MethodId METHOD_COLLECT_VEHICLE_INVENTORY = 0x0002;
constexpr MethodId METHOD_GET_ECU_VERSION = 0x0003;
constexpr MethodId METHOD_GET_REGISTRY_VERSION = 0x0004;
// CCU-DIAG-DSN-CR-010 §5.1: installed version registration (suggested ID,
// pending VEH-SOMEIP Registry review). Once assigned it must not be renumbered.
constexpr MethodId METHOD_RECORD_INSTALLED_VERSIONS = 0x0005;

// SOME/IP Event IDs
constexpr EventId EVENT_INVENTORY_RESULT = 0x8001;

// SOME/IP Network Configuration
inline const std::string DEFAULT_SOMEIP_ADDRESS = "127.0.0.1";

// CCU-SEC SOME/IP endpoint defaults (Service Registry SSOT: CCU-SEC-DSN-CR-003)
constexpr ServiceId CCU_SEC_SERVICE_ID = 0x1140;
constexpr uint16_t CCU_SEC_INSTANCE_ID = 0x0001;
constexpr uint8_t CCU_SEC_MAJOR_VERSION = 1;
constexpr uint32_t CCU_SEC_MINOR_VERSION = 0;
inline const std::string DEFAULT_SEC_SOMEIP_ADDRESS = "127.0.0.1";
constexpr uint16_t DEFAULT_SEC_SOMEIP_PORT = 51140;

// 进程 vsomeip application 名（平台 vsomeip.json applications 登记名）。
// 一个进程只允许一个 application；service(0x1110) 与 client proxy(0x1140)
// 复用同一实例，名字必须与平台配置一致，否则无法加入 tbox-someip routing 域。
inline const std::string DEFAULT_SOMEIP_APPLICATION_NAME = "ccu-diag";
constexpr uint16_t DEFAULT_SOMEIP_PORT = 51110;
constexpr uint32_t DEFAULT_SOMEIP_REQUEST_TIMEOUT_MS = 2000;

} // namespace constants
} // namespace ccu_diag
