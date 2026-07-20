#include "config_loader.h"
#include "constants.h"
#include <yaml-cpp/yaml.h>
#include <stdexcept>

namespace cgw_diag {

DidRegistryConfig ConfigLoader::loadDidRegistry(const std::string& file_path) {
    DidRegistryConfig config;
    
    try {
        YAML::Node root = YAML::LoadFile(file_path);
        
        // Load registry version
        if (root["registryVersion"]) {
            config.registry_version = root["registryVersion"].as<std::string>();
        }
        
        // Load baseline DIDs
        if (root["baselineDids"]) {
            for (const auto& did : root["baselineDids"]) {
                config.baseline_dids.push_back(did.as<uint16_t>());
            }
        }
        
        // Load ECU configurations
        if (root["ecus"]) {
            for (const auto& ecu_node : root["ecus"]) {
                EcuConfig ecu = parseEcuConfig(&ecu_node);
                config.ecus.push_back(ecu);
            }
        }
        
    } catch (const YAML::Exception& e) {
        throw std::runtime_error("Failed to load DID registry: " + std::string(e.what()));
    }
    
    return config;
}

DiagConfig ConfigLoader::loadDiagConfig(const std::string& file_path) {
    DiagConfig config;
    
    try {
        YAML::Node root = YAML::LoadFile(file_path);
        
        if (root["timing"]) {
            auto timing = root["timing"];
            config.p2_server_max_ms = timing["p2_server_max_ms"].as<uint32_t>(constants::P2_SERVER_MAX_MS);
            config.p2_star_server_max_ms = timing["p2_star_server_max_ms"].as<uint32_t>(constants::P2_STAR_SERVER_MAX_MS);
            config.s3_session_timeout_ms = timing["s3_session_timeout_ms"].as<uint32_t>(constants::S3_SESSION_TIMEOUT_MS);
            config.tester_present_interval_ms = timing["tester_present_interval_ms"].as<uint32_t>(constants::TESTER_PRESENT_INTERVAL_MS);
            config.request_timeout_ms = timing["request_timeout_ms"].as<uint32_t>(constants::REQUEST_TIMEOUT_MS);
            config.retry_interval_ms = timing["retry_interval_ms"].as<uint32_t>(constants::RETRY_INTERVAL_MS);
        }
        
        if (root["retry"]) {
            config.max_retry_count = root["retry"]["max_count"].as<uint8_t>(constants::MAX_RETRY_COUNT);
        }
        
    } catch (const YAML::Exception& e) {
        throw std::runtime_error("Failed to load diag config: " + std::string(e.what()));
    }
    
    return config;
}

EcuConfig ConfigLoader::parseEcuConfig(const void* ecu_node) {
    const YAML::Node& node = *static_cast<const YAML::Node*>(ecu_node);
    EcuConfig ecu;
    
    ecu.ecu_id = node["ecuId"].as<std::string>();
    ecu.logical_address = node["logicalAddress"].as<uint16_t>();
    
    std::string transport = node["transport"].as<std::string>();
    if (transport == "DoCAN") {
        ecu.transport = TransportType::DOCAN;
    } else if (transport == "DoIP") {
        ecu.transport = TransportType::DOIP;
    } else {
        throw std::runtime_error("Unknown transport type: " + transport);
    }
    
    std::string read_method = node["readMethod"].as<std::string>();
    if (read_method == "UDS_0x22") {
        ecu.read_method = ReadMethod::UDS_0x22;
    } else if (read_method == "SOMEIP_getVersion") {
        ecu.read_method = ReadMethod::SOMEIP_GET_VERSION;
    } else {
        throw std::runtime_error("Unknown read method: " + read_method);
    }
    
    ecu.security_required = node["securityRequired"].as<bool>(false);
    
    // Load DID entries for UDS method
    if (ecu.read_method == ReadMethod::UDS_0x22 && node["dids"]) {
        for (const auto& did_node : node["dids"]) {
            DidEntry did = parseDidEntry(&did_node);
            ecu.dids.push_back(did);
        }
    }
    
    // Load SOME/IP specific fields
    if (ecu.read_method == ReadMethod::SOMEIP_GET_VERSION) {
        if (node["someipService"]) {
            ecu.someip_service = node["someipService"].as<std::string>();
        }
        if (node["fields"]) {
            for (const auto& field : node["fields"]) {
                ecu.fields.push_back(field.as<std::string>());
            }
        }
    }
    
    return ecu;
}

DidEntry ConfigLoader::parseDidEntry(const void* did_node) {
    const YAML::Node& node = *static_cast<const YAML::Node*>(did_node);
    DidEntry entry;
    
    entry.did = node["did"].as<uint16_t>();
    entry.field = node["field"].as<std::string>();
    entry.encoding = node["encoding"].as<std::string>();
    entry.length = node["length"].as<uint16_t>();
    
    return entry;
}

} // namespace cgw_diag
