#pragma once

#include <string>
#include <vector>
#include <optional>
#include <cstdint>

namespace cgw_diag {

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
    std::optional<std::string> part_number;
    std::optional<std::string> sw_version;
    std::optional<std::string> hw_version;
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
    std::vector<EcuVersionEntry> ecu_list;
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

} // namespace cgw_diag
