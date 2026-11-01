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

// Configuration file paths
inline const std::string DEFAULT_DID_REGISTRY_PATH = "config/did_registry.yaml";
inline const std::string DEFAULT_DIAG_CONFIG_PATH = "config/diag_config.yaml";
inline const std::string DEFAULT_SOMEIP_CONFIG_PATH = "config/someip_config.yaml";

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

// SOME/IP Event IDs
constexpr EventId EVENT_INVENTORY_RESULT = 0x8001;

// SOME/IP Network Configuration
inline const std::string DEFAULT_SEC_CONFIG_PATH = "config/sec_config.yaml";
inline const std::string DEFAULT_SOMEIP_ADDRESS = "127.0.0.1";
constexpr uint16_t DEFAULT_SOMEIP_PORT = 51110;
constexpr uint32_t DEFAULT_SOMEIP_REQUEST_TIMEOUT_MS = 2000;

} // namespace constants
} // namespace ccu_diag
