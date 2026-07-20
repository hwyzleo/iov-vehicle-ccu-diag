#pragma once

#include <cstdint>
#include <string>

namespace cgw_diag {
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

} // namespace constants
} // namespace cgw_diag
