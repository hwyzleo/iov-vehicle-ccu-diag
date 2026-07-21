#include "config_loader.h"
#include "did_registry.h"
#include "uds_client.h"
#include "diagnostic_router.h"
#include "do_can_transport.h"
#include "doip_transport.h"
#include "version_collector.h"
#include "fota_facade.h"
#include "fota_someip_service.h"
#include "someip_service.h"
#include "vin_store.h"
#include "constants.h"
#include <iostream>
#include <memory>
#include <string>
#include <csignal>
#include <atomic>
#include <thread>
#include <chrono>

using namespace cgw_diag;

namespace {
std::atomic<bool> g_running{true};

void signalHandler(int signal) {
    if (signal == SIGINT || signal == SIGTERM) {
        g_running = false;
    }
}

void printUsage(const char* program_name) {
    std::cerr << "Usage: " << program_name << " [options]\n"
              << "Options:\n"
              << "  --did-registry <path>    Path to DID registry YAML\n"
              << "  --diag-config <path>     Path to diag config YAML\n"
              << "  --someip-config <path>   Path to SOME/IP config YAML\n"
              << "  --can-interface <name>   CAN interface name (default: " << constants::DEFAULT_CAN_INTERFACE << ")\n"
              << "  --can-baudrate <rate>    CAN baudrate (default: " << constants::DEFAULT_CAN_BAUDRATE << ")\n"
              << "  --doip-ip <ip>           DoIP server IP address (default: " << constants::DEFAULT_DOIP_IP << ")\n"
              << "  --doip-port <port>       DoIP server port (default: " << constants::DEFAULT_DOIP_PORT << ")\n"
              << "  --someip-ip <ip>         SOME/IP listen address (default: " << constants::DEFAULT_SOMEIP_ADDRESS << ")\n"
              << "  --someip-port <port>     SOME/IP listen port (default: " << constants::DEFAULT_SOMEIP_PORT << ")\n"
              << "  --version                Print application version\n"
              << "  --help                   Show this help message\n";
}

} // namespace

int main(int argc, char* argv[]) {
    std::string did_registry_path = constants::DEFAULT_DID_REGISTRY_PATH;
    std::string diag_config_path = constants::DEFAULT_DIAG_CONFIG_PATH;
    std::string someip_config_path = constants::DEFAULT_SOMEIP_CONFIG_PATH;
    std::string can_interface = constants::DEFAULT_CAN_INTERFACE;
    uint32_t can_baudrate = constants::DEFAULT_CAN_BAUDRATE;
    std::string doip_ip = constants::DEFAULT_DOIP_IP;
    uint16_t doip_port = constants::DEFAULT_DOIP_PORT;
    std::string someip_ip = constants::DEFAULT_SOMEIP_ADDRESS;
    uint16_t someip_port = constants::DEFAULT_SOMEIP_PORT;
    bool show_version = false;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--did-registry" && i + 1 < argc) {
            did_registry_path = argv[++i];
        } else if (arg == "--diag-config" && i + 1 < argc) {
            diag_config_path = argv[++i];
        } else if (arg == "--someip-config" && i + 1 < argc) {
            someip_config_path = argv[++i];
        } else if (arg == "--can-interface" && i + 1 < argc) {
            can_interface = argv[++i];
        } else if (arg == "--can-baudrate" && i + 1 < argc) {
            can_baudrate = static_cast<uint32_t>(std::stoul(argv[++i]));
        } else if (arg == "--doip-ip" && i + 1 < argc) {
            doip_ip = argv[++i];
        } else if (arg == "--doip-port" && i + 1 < argc) {
            doip_port = static_cast<uint16_t>(std::stoi(argv[++i]));
        } else if (arg == "--someip-ip" && i + 1 < argc) {
            someip_ip = argv[++i];
        } else if (arg == "--someip-port" && i + 1 < argc) {
            someip_port = static_cast<uint16_t>(std::stoi(argv[++i]));
        } else if (arg == "--version") {
            show_version = true;
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

        // Setup signal handlers
        std::signal(SIGINT, signalHandler);
        std::signal(SIGTERM, signalHandler);

        // Load configurations
        auto registry = std::make_shared<DidRegistry>(did_registry_path);
        auto diag_config = ConfigLoader::loadDiagConfig(diag_config_path);

        // Create transports
        auto docan_transport = std::make_shared<DoCanTransport>(can_interface, can_baudrate);
        auto doip_transport = std::make_shared<DoIpTransport>(doip_ip, doip_port);

        // Create UDS client and router
        auto uds_client = std::make_shared<UdsClient>(docan_transport, diag_config);
        auto router = std::make_shared<DiagnosticRouter>(registry, uds_client, docan_transport, doip_transport);

        // Create VIN store
        auto vin_store = std::shared_ptr<VinStore>(createVinStore().release());

        // Create version collector and FOTA facade
        auto collector = std::make_shared<VersionCollector>(registry, router, docan_transport, doip_transport, vin_store);
        auto facade = std::make_shared<FotaFacade>(collector, registry, vin_store);

        // Load SOME/IP configuration
        SomeIpServiceConfig someip_config;
        try {
            someip_config = ConfigLoader::loadSomeIpConfig(someip_config_path);
        } catch (const std::exception& e) {
            std::cerr << "Warning: Failed to load SOME/IP config, using defaults: " << e.what() << "\n";
            someip_config.service_id = constants::CGW_DIAG_SERVICE_ID;
            someip_config.instance_id = constants::CGW_DIAG_INSTANCE_ID;
            someip_config.major_version = constants::CGW_DIAG_MAJOR_VERSION;
            someip_config.minor_version = constants::CGW_DIAG_MINOR_VERSION;
        }

        // Override network config from command line
        someip_config.ip_address = someip_ip;
        someip_config.port = someip_port;

        // Create SOME/IP service
        auto someip_service = createSomeIpService();
        FotaSomeIpService fota_service(facade, std::move(someip_service));

        if (!fota_service.initialize(someip_config)) {
            std::cerr << "Error: Failed to initialize SOME/IP service\n";
            return 1;
        }

        if (!fota_service.start()) {
            std::cerr << "Error: Failed to start SOME/IP service\n";
            return 1;
        }

        if (!fota_service.offerService()) {
            std::cerr << "Error: Failed to offer SOME/IP service\n";
            return 1;
        }

        std::cout << "CGW-DIAG service started\n";
        std::cout << "SOME/IP Service ID: 0x" << std::hex << someip_config.service_id << "\n";
        std::cout << "Listening on: " << someip_config.ip_address << ":" << someip_config.port << "\n";

        // Main loop
        while (g_running) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }

        std::cout << "\nShutting down...\n";
        fota_service.stop();

        return 0;

    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }
}
