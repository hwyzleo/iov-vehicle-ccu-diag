#include "config_loader.h"
#include "constants.h"
#include <stdexcept>
#include <string>

namespace ccu_diag {

namespace {

YAML::Node loadYamlFile(const std::string& file_path, const char* what) {
    try {
        return YAML::LoadFile(file_path);
    } catch (const YAML::Exception& e) {
        throw std::runtime_error(
            "Failed to load " + std::string(what) + " (" + file_path + "): " +
            std::string(e.what()));
    }
}

void requireMap(const YAML::Node& node, const std::string& context) {
    if (!node.IsMap()) {
        throw std::runtime_error(context + " must be a mapping");
    }
}

} // namespace

void ConfigLoader::rejectUnknownKeys(const YAML::Node& node,
                                     std::initializer_list<const char*> allowed,
                                     const std::string& context) {
    requireMap(node, context);
    for (auto it = node.begin(); it != node.end(); ++it) {
        const std::string key = it->first.as<std::string>();
        bool ok = false;
        for (const char* candidate : allowed) {
            if (key == candidate) {
                ok = true;
                break;
            }
        }
        if (!ok) {
            throw std::runtime_error("unknown config key: " + context + "." + key);
        }
    }
}

UnifiedDiagConfig ConfigLoader::loadUnifiedConfig(
    const std::string& diag_config_path,
    const std::string& common_config_path) {

    YAML::Node root = loadYamlFile(diag_config_path, "unified DIAG config");
    requireMap(root, "unified DIAG config");

    // CCU-DIAG-BUILD-REQ-003: the service file must never inline common:.
    if (root["common"]) {
        throw std::runtime_error(
            "unified DIAG config must not declare a top-level 'common:' key");
    }
    // Only diag: is allowed; old root nodes (timing/retry/someip/registry) are
    // rejected instead of being silently ignored after migration.
    rejectUnknownKeys(root, {"diag"}, "diag.yaml");

    const YAML::Node diag = root["diag"];
    if (!diag) {
        throw std::runtime_error("unified DIAG config is missing required 'diag:' root");
    }
    rejectUnknownKeys(diag,
                      {"log", "timing", "retry", "transport",
                       "registry", "someip", "sec_client"},
                      "diag");

    const YAML::Node sec_client = diag["sec_client"];
    if (!sec_client) {
        throw std::runtime_error("unified DIAG config is missing 'diag.sec_client'");
    }
    rejectUnknownKeys(sec_client, {"someip"}, "diag.sec_client");

    // common.log.* comes from the BUILD-owned common.yaml; diag.log.* overrides
    // it from the unified service file. No second deep-merge algorithm.
    YAML::Node common_root = loadYamlFile(common_config_path, "common config");
    requireMap(common_root, "common config");

    UnifiedDiagConfig config;
    config.log.schema_version = 1;
    config.log.level = ccu::fw::log::LogLevel::kInfo;
    config.log.strict = false;
    config.log.format = "standard";
    parseLogConfigNode(common_root, config.log);
    parseLogConfigNode(root, config.log);

    config.registry = parseDidRegistryNode(diag["registry"], true);
    config.diag = parseDiagConfigNode(diag);
    config.transport = parseTransportNode(diag["transport"]);

    SomeIpServiceConfig provider_defaults;
    provider_defaults.service_id = constants::CCU_DIAG_SERVICE_ID;
    provider_defaults.instance_id = constants::CCU_DIAG_INSTANCE_ID;
    provider_defaults.major_version = constants::CCU_DIAG_MAJOR_VERSION;
    provider_defaults.minor_version = constants::CCU_DIAG_MINOR_VERSION;
    provider_defaults.ip_address = constants::DEFAULT_SOMEIP_ADDRESS;
    provider_defaults.port = constants::DEFAULT_SOMEIP_PORT;
    config.someip = parseSomeIpConfigNode(diag["someip"], provider_defaults, true);

    SomeIpServiceConfig sec_defaults;
    sec_defaults.service_id = constants::CCU_SEC_SERVICE_ID;
    sec_defaults.instance_id = constants::CCU_SEC_INSTANCE_ID;
    sec_defaults.major_version = constants::CCU_SEC_MAJOR_VERSION;
    sec_defaults.minor_version = constants::CCU_SEC_MINOR_VERSION;
    sec_defaults.ip_address = constants::DEFAULT_SEC_SOMEIP_ADDRESS;
    sec_defaults.port = constants::DEFAULT_SEC_SOMEIP_PORT;
    config.sec_someip = parseSomeIpConfigNode(sec_client["someip"], sec_defaults, false);

    return config;
}

SomeIpServiceConfig ConfigLoader::loadSomeIpConfig(const std::string& file_path) {
    YAML::Node yaml = loadYamlFile(file_path, "SOME/IP config");
    requireMap(yaml, "SOME/IP config");
    const YAML::Node someip = yaml["someip"];
    if (!someip) {
        throw std::runtime_error("SOME/IP config is missing 'someip:' root");
    }
    return parseSomeIpConfigNode(someip, SomeIpServiceConfig{}, true);
}

DidRegistryConfig ConfigLoader::loadDidRegistry(const std::string& file_path) {
    YAML::Node root = loadYamlFile(file_path, "DID registry");
    requireMap(root, "DID registry");
    // Legacy development fixtures may still carry unused vinConfig entries.
    return parseDidRegistryNode(root, false);
}

DidRegistryConfig ConfigLoader::parseDidRegistryNode(const YAML::Node& node, bool strict) {
    if (!node) {
        throw std::runtime_error("unified DIAG config is missing 'diag.registry'");
    }
    requireMap(node, "diag.registry");
    if (strict) {
        rejectUnknownKeys(node, {"registryVersion", "baselineDids", "ecus"},
                          "diag.registry");
    }

    DidRegistryConfig config;
    if (node["registryVersion"]) {
        config.registry_version = node["registryVersion"].as<std::string>();
    }
    if (node["baselineDids"]) {
        for (const auto& did : node["baselineDids"]) {
            config.baseline_dids.push_back(did.as<uint16_t>());
        }
    }
    if (node["ecus"]) {
        for (const auto& ecu_node : node["ecus"]) {
            config.ecus.push_back(parseEcuConfig(ecu_node));
        }
    }
    return config;
}

DiagConfig ConfigLoader::loadDiagConfig(const std::string& file_path) {
    YAML::Node root = loadYamlFile(file_path, "diag config");
    requireMap(root, "diag config");
    return parseDiagConfigNode(root);
}

DiagConfig ConfigLoader::parseDiagConfigNode(const YAML::Node& node) {
    DiagConfig config;

    if (node["timing"]) {
        const YAML::Node timing = node["timing"];
        rejectUnknownKeys(timing,
                          {"p2_server_max_ms", "p2_star_server_max_ms",
                           "s3_session_timeout_ms", "tester_present_interval_ms",
                           "request_timeout_ms", "retry_interval_ms"},
                          "diag.timing");
        config.p2_server_max_ms =
            timing["p2_server_max_ms"].as<uint32_t>(constants::P2_SERVER_MAX_MS);
        config.p2_star_server_max_ms =
            timing["p2_star_server_max_ms"].as<uint32_t>(constants::P2_STAR_SERVER_MAX_MS);
        config.s3_session_timeout_ms =
            timing["s3_session_timeout_ms"].as<uint32_t>(constants::S3_SESSION_TIMEOUT_MS);
        config.tester_present_interval_ms =
            timing["tester_present_interval_ms"].as<uint32_t>(constants::TESTER_PRESENT_INTERVAL_MS);
        config.request_timeout_ms =
            timing["request_timeout_ms"].as<uint32_t>(constants::REQUEST_TIMEOUT_MS);
        config.retry_interval_ms =
            timing["retry_interval_ms"].as<uint32_t>(constants::RETRY_INTERVAL_MS);
    }

    if (node["retry"]) {
        const YAML::Node retry = node["retry"];
        rejectUnknownKeys(retry, {"max_count"}, "diag.retry");
        config.max_retry_count =
            retry["max_count"].as<uint8_t>(constants::MAX_RETRY_COUNT);
    }

    return config;
}

TransportConfig ConfigLoader::parseTransportNode(const YAML::Node& node) {
    TransportConfig config;
    config.can_interface = constants::DEFAULT_CAN_INTERFACE;
    config.can_baudrate = constants::DEFAULT_CAN_BAUDRATE;
    config.doip_ip = constants::DEFAULT_DOIP_IP;
    config.doip_port = constants::DEFAULT_DOIP_PORT;

    if (!node || node.IsNull()) {
        return config;
    }
    rejectUnknownKeys(node, {"can", "doip"}, "diag.transport");

    if (node["can"]) {
        const YAML::Node can = node["can"];
        rejectUnknownKeys(can, {"interface", "baudrate"}, "diag.transport.can");
        config.can_interface = can["interface"].as<std::string>(config.can_interface);
        config.can_baudrate = can["baudrate"].as<uint32_t>(config.can_baudrate);
    }

    if (node["doip"]) {
        const YAML::Node doip = node["doip"];
        rejectUnknownKeys(doip, {"ip", "port"}, "diag.transport.doip");
        config.doip_ip = doip["ip"].as<std::string>(config.doip_ip);
        config.doip_port = doip["port"].as<uint16_t>(config.doip_port);
    }

    return config;
}

SomeIpServiceConfig ConfigLoader::parseSomeIpConfigNode(
    const YAML::Node& node,
    const SomeIpServiceConfig& defaults,
    bool allow_methods) {

    if (!node) {
        throw std::runtime_error("unified DIAG config is missing a 'someip:' subtree");
    }
    requireMap(node, "someip");

    if (allow_methods) {
        rejectUnknownKeys(node,
                          {"application_name", "service", "network",
                           "methods", "events", "timing", "sd"},
                          "diag.someip");
    } else {
        rejectUnknownKeys(node,
                          {"application_name", "service", "network", "timing", "sd"},
                          "diag.sec_client.someip");
    }

    SomeIpServiceConfig config = defaults;

    if (node["application_name"]) {
        config.application_name = node["application_name"].as<std::string>();
    }

    if (node["service"]) {
        const YAML::Node service = node["service"];
        const std::string context = allow_methods ? "diag.someip.service"
                                                  : "diag.sec_client.someip.service";
        rejectUnknownKeys(service,
                          {"service_id", "instance_id", "major_version", "minor_version"},
                          context);
        config.service_id = service["service_id"].as<uint16_t>(config.service_id);
        config.instance_id = service["instance_id"].as<uint16_t>(config.instance_id);
        config.major_version = service["major_version"].as<uint8_t>(config.major_version);
        config.minor_version = service["minor_version"].as<uint32_t>(config.minor_version);
    }

    if (node["network"]) {
        const YAML::Node network = node["network"];
        const std::string context = allow_methods ? "diag.someip.network"
                                                  : "diag.sec_client.someip.network";
        rejectUnknownKeys(network, {"ip_address", "port"}, context);
        config.ip_address = network["ip_address"].as<std::string>(config.ip_address);
        config.port = network["port"].as<uint16_t>(config.port);
    }

    // methods / events / timing / sd are validated by the executable schema;
    // the runtime only consumes service identity and endpoint here.
    if (node["methods"] && !node["methods"].IsMap() && !node["methods"].IsNull()) {
        throw std::runtime_error("diag.someip.methods must be a mapping");
    }
    if (node["events"] && !node["events"].IsMap() && !node["events"].IsNull()) {
        throw std::runtime_error("diag.someip.events must be a mapping");
    }
    if (node["timing"] && !node["timing"].IsMap() && !node["timing"].IsNull()) {
        throw std::runtime_error("someip.timing must be a mapping");
    }
    if (node["sd"] && !node["sd"].IsMap() && !node["sd"].IsNull()) {
        throw std::runtime_error("someip.sd must be a mapping");
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

    if (ecu.read_method == ReadMethod::UDS_0x22 && node["dids"]) {
        for (const auto& did_node : node["dids"]) {
            ecu.dids.push_back(parseDidEntry(did_node));
        }
    }

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

void ConfigLoader::parseLogConfigNode(const YAML::Node& root,
                                      ccu::fw::log::LogConfig& log_config) {
    if (!root.IsMap()) {
        return;
    }

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

        if (common_log["console"]) {
            auto console = common_log["console"];
            if (console["enabled"]) {
                log_config.console_config.enabled = console["enabled"].as<bool>();
            }
        }

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
