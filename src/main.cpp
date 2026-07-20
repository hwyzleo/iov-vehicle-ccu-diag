#include "config_loader.h"
#include "did_registry.h"
#include "uds_client.h"
#include "diagnostic_router.h"
#include "do_can_transport.h"
#include "doip_transport.h"
#include "version_collector.h"
#include "fota_facade.h"
#include "constants.h"
#include <iostream>
#include <memory>
#include <string>
#include <nlohmann/json.hpp>

using namespace cgw_diag;

namespace {

void printUsage(const char* program_name) {
    std::cerr << "Usage: " << program_name << " [options]\n"
              << "Options:\n"
              << "  --vin <VIN>              Collect vehicle software inventory\n"
              << "  --ecu <ECU_ID>           Get version for specific ECU\n"
              << "  --registry-version       Print registry version\n"
              << "  --version                Print application version\n"
              << "  --did-registry <path>    Path to DID registry YAML\n"
              << "  --diag-config <path>     Path to diag config YAML\n"
              << "  --can-interface <name>   CAN interface name (default: " << constants::DEFAULT_CAN_INTERFACE << ")\n"
              << "  --can-baudrate <rate>    CAN baudrate (default: " << constants::DEFAULT_CAN_BAUDRATE << ")\n"
              << "  --doip-ip <ip>           DoIP server IP address (default: " << constants::DEFAULT_DOIP_IP << ")\n"
              << "  --doip-port <port>       DoIP server port (default: " << constants::DEFAULT_DOIP_PORT << ")\n"
              << "  --help                   Show this help message\n";
}

nlohmann::json ecuVersionToJson(const EcuVersionEntry& entry) {
    nlohmann::json j;
    j["ecu_id"] = entry.ecu_id;

    if (entry.part_number.has_value()) {
        j["part_number"] = entry.part_number.value();
    }
    if (entry.sw_version.has_value()) {
        j["sw_version"] = entry.sw_version.value();
    }
    if (entry.hw_version.has_value()) {
        j["hw_version"] = entry.hw_version.value();
    }

    std::string source_str;
    switch (entry.source) {
        case ReadMethod::UDS_0x22: source_str = "UDS_0x22"; break;
        case ReadMethod::SOMEIP_GET_VERSION: source_str = "SOMEIP_GET_VERSION"; break;
    }
    j["source"] = source_str;

    std::string status_str;
    switch (entry.status) {
        case EcuStatus::OK: status_str = "OK"; break;
        case EcuStatus::MISSING: status_str = "MISSING"; break;
        case EcuStatus::NRC: status_str = "NRC"; break;
        case EcuStatus::TIMEOUT: status_str = "TIMEOUT"; break;
        case EcuStatus::UNREACHABLE: status_str = "UNREACHABLE"; break;
        case EcuStatus::PARSE_ERROR: status_str = "PARSE_ERROR"; break;
    }
    j["status"] = status_str;

    if (entry.error_code.has_value()) {
        j["error_code"] = entry.error_code.value();
    }

    return j;
}

nlohmann::json snapshotToJson(const VehicleSoftwareSnapshot& snapshot) {
    nlohmann::json j;
    j["vin"] = snapshot.vin;

    if (snapshot.baseline_id.has_value()) {
        j["baseline_id"] = snapshot.baseline_id.value();
    }

    std::string baseline_source_str;
    switch (snapshot.baseline_source) {
        case BaselineSource::FACTORY: baseline_source_str = "FACTORY"; break;
        case BaselineSource::LAST_OTA: baseline_source_str = "LAST_OTA"; break;
        case BaselineSource::UNKNOWN: baseline_source_str = "UNKNOWN"; break;
    }
    j["baseline_source"] = baseline_source_str;

    j["registry_version"] = snapshot.registry_version;
    j["collected_at"] = snapshot.collected_at;

    std::string overall_str;
    switch (snapshot.overall_result) {
        case CollectionStatus::ALL_OK: overall_str = "ALL_OK"; break;
        case CollectionStatus::PARTIAL: overall_str = "PARTIAL"; break;
        case CollectionStatus::FAILED: overall_str = "FAILED"; break;
    }
    j["overall_result"] = overall_str;

    j["ecu_list"] = nlohmann::json::array();
    for (const auto& entry : snapshot.ecu_list) {
        j["ecu_list"].push_back(ecuVersionToJson(entry));
    }

    return j;
}

} // namespace

int main(int argc, char* argv[]) {
    std::string vin;
    std::string ecu_id;
    bool show_registry_version = false;
    bool show_version = false;
    std::string did_registry_path = constants::DEFAULT_DID_REGISTRY_PATH;
    std::string diag_config_path = constants::DEFAULT_DIAG_CONFIG_PATH;
    std::string can_interface = constants::DEFAULT_CAN_INTERFACE;
    uint32_t can_baudrate = constants::DEFAULT_CAN_BAUDRATE;
    std::string doip_ip = constants::DEFAULT_DOIP_IP;
    uint16_t doip_port = constants::DEFAULT_DOIP_PORT;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--vin" && i + 1 < argc) {
            vin = argv[++i];
        } else if (arg == "--ecu" && i + 1 < argc) {
            ecu_id = argv[++i];
        } else if (arg == "--registry-version") {
            show_registry_version = true;
        } else if (arg == "--version") {
            show_version = true;
        } else if (arg == "--did-registry" && i + 1 < argc) {
            did_registry_path = argv[++i];
        } else if (arg == "--diag-config" && i + 1 < argc) {
            diag_config_path = argv[++i];
        } else if (arg == "--can-interface" && i + 1 < argc) {
            can_interface = argv[++i];
        } else if (arg == "--can-baudrate" && i + 1 < argc) {
            can_baudrate = static_cast<uint32_t>(std::stoul(argv[++i]));
        } else if (arg == "--doip-ip" && i + 1 < argc) {
            doip_ip = argv[++i];
        } else if (arg == "--doip-port" && i + 1 < argc) {
            doip_port = static_cast<uint16_t>(std::stoi(argv[++i]));
        } else if (arg == "--help") {
            printUsage(argv[0]);
            return 0;
        } else {
            std::cerr << "Unknown option: " << arg << "\n";
            printUsage(argv[0]);
            return 1;
        }
    }

    try {
        if (show_version) {
            std::cout << constants::APP_VERSION << "\n";
            return 0;
        }

        auto registry = std::make_shared<DidRegistry>(did_registry_path);
        auto diag_config = ConfigLoader::loadDiagConfig(diag_config_path);

        auto docan_transport = std::make_shared<DoCanTransport>(can_interface, can_baudrate);
        auto doip_transport = std::make_shared<DoIpTransport>(doip_ip, doip_port);

        auto uds_client = std::make_shared<UdsClient>(docan_transport, diag_config);

        auto router = std::make_shared<DiagnosticRouter>(
            registry, uds_client, docan_transport, doip_transport);

        auto collector = std::make_shared<VersionCollector>(
            registry, router, docan_transport, doip_transport);

        FotaFacade facade(collector, registry);

        if (show_registry_version) {
            std::cout << facade.getRegistryVersion() << "\n";
            return 0;
        }

        if (!vin.empty()) {
            auto snapshot = facade.collectVehicleInventory(vin);
            std::cout << snapshotToJson(snapshot).dump(2) << "\n";
            return (snapshot.overall_result == CollectionStatus::ALL_OK) ? 0 : 1;
        }

        if (!ecu_id.empty()) {
            auto entry = facade.getEcuVersion(ecu_id);
            std::cout << ecuVersionToJson(entry).dump(2) << "\n";
            return (entry.status == EcuStatus::OK) ? 0 : 1;
        }

        printUsage(argv[0]);
        return 1;

    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }
}
