#include "config_loader.h"
#include "constants.h"
#include <stdexcept>

namespace ccu_diag {

SomeIpServiceConfig ConfigLoader::loadSomeIpConfig(const std::string& file_path) {
    SomeIpServiceConfig config;
    
    try {
        YAML::Node yaml = YAML::LoadFile(file_path);
        
        if (yaml["someip"]["service"]) {
            auto service = yaml["someip"]["service"];
            config.service_id = service["service_id"].as<uint16_t>();
            config.instance_id = service["instance_id"].as<uint16_t>();
            config.major_version = service["major_version"].as<uint8_t>();
            config.minor_version = service["minor_version"].as<uint32_t>();
        }
        
        if (yaml["someip"]["network"]) {
            auto network = yaml["someip"]["network"];
            config.ip_address = network["ip_address"].as<std::string>();
            config.port = network["port"].as<uint16_t>();
        }
        
    } catch (const YAML::Exception& e) {
        throw std::runtime_error("Failed to load SOME/IP config: " + std::string(e.what()));
    }
    
    return config;
}

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
                EcuConfig ecu = parseEcuConfig(ecu_node);
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

EcuConfig ConfigLoader::parseEcuConfig(const YAML::Node& node) {
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
            DidEntry did = parseDidEntry(did_node);
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

DidEntry ConfigLoader::parseDidEntry(const YAML::Node& node) {
    DidEntry entry;
    
    entry.did = node["did"].as<uint16_t>();
    entry.field = node["field"].as<std::string>();
    entry.encoding = node["encoding"].as<std::string>();
    entry.length = node["length"].as<uint16_t>();
    
    return entry;
}

ccu::fw::log::LogConfig ConfigLoader::loadLogConfig(const std::string& file_path) {
    ccu::fw::log::LogConfig log_config;
    // 默认值
    log_config.schema_version = 1;
    log_config.level = ccu::fw::log::LogLevel::kInfo;
    log_config.strict = false;
    log_config.format = "standard";

    try {
        YAML::Node root = YAML::LoadFile(file_path);
        parseLogConfigNode(root, log_config);
    } catch (const YAML::Exception&) {
        // 配置加载失败时返回默认配置，由调用方决定是否严格模式终止
    }

    return log_config;
}

void ConfigLoader::parseLogConfigNode(const YAML::Node& root, ccu::fw::log::LogConfig& log_config) {
    // common.log.*
    if (root["common"] && root["common"]["log"]) {
        auto common_log = root["common"]["log"];

        if (common_log["schema_version"]) {
            log_config.schema_version = common_log["schema_version"].as<uint32_t>();
        }
        if (common_log["level"]) {
            log_config.level = ccu::fw::log::logLevelFromString(common_log["level"].as<std::string>());
        }
        if (common_log["strict"]) {
            log_config.strict = common_log["strict"].as<bool>();
        }
        if (common_log["format"]) {
            log_config.format = common_log["format"].as<std::string>();
        }

        // async
        if (common_log["async"]) {
            auto async = common_log["async"];
            if (async["enabled"]) {
                log_config.async_config.enabled = async["enabled"].as<bool>();
            }
            if (async["queue_size"]) {
                log_config.async_config.queue_size = async["queue_size"].as<uint32_t>();
            }
            if (async["flush_interval_ms"]) {
                log_config.async_config.flush_interval_ms = async["flush_interval_ms"].as<uint32_t>();
            }
        }

        // console
        if (common_log["console"]) {
            auto console = common_log["console"];
            if (console["enabled"]) {
                log_config.console_config.enabled = console["enabled"].as<bool>();
            }
        }

        // file
        if (common_log["file"]) {
            auto file = common_log["file"];
            if (file["enabled"]) {
                log_config.file_config.enabled = file["enabled"].as<bool>();
            }
            if (file["root"]) {
                log_config.file_config.root = file["root"].as<std::string>();
            }
            if (file["max_file_size_mb"]) {
                log_config.file_config.max_file_size_mb = file["max_file_size_mb"].as<uint32_t>();
            }
            if (file["max_files"]) {
                log_config.file_config.max_files = file["max_files"].as<uint32_t>();
            }
            if (file["total_budget_mb"]) {
                log_config.file_config.total_budget_mb = file["total_budget_mb"].as<uint32_t>();
            }
        }

        // redact
        if (common_log["redact"]) {
            auto redact = common_log["redact"];
            if (redact["identifiers"]) {
                log_config.redact_config.identifiers = redact["identifiers"].as<std::string>();
            }
            if (redact["payload_max_bytes"]) {
                log_config.redact_config.raw_payload_max_bytes = redact["payload_max_bytes"].as<uint32_t>();
            }
        }
    }

    // diag.log.* (service-level overrides)
    if (root["diag"] && root["diag"]["log"]) {
        auto diag_log = root["diag"]["log"];

        if (diag_log["level"]) {
            log_config.level = ccu::fw::log::logLevelFromString(diag_log["level"].as<std::string>());
        }

        // module-level overrides (CCU-DIAG-DSN-CR-008 §启动与模块 Logger)
        if (diag_log["modules"]) {
            auto modules = diag_log["modules"];
            for (auto it = modules.begin(); it != modules.end(); ++it) {
                std::string mod_name = it->first.as<std::string>();
                std::string mod_level = it->second.as<std::string>();
                log_config.module_levels[mod_name] =
                    ccu::fw::log::logLevelFromString(mod_level);
            }
        }
    }
}

} // namespace ccu_diag
