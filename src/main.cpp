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
#include "diagnostic_server.h"
#include "cgw_sec_client_proxy.h"
#include "constants.h"
#include "diag_log_adapter.h"
#include "diag_log_context.h"
#include <iostream>
#include <sstream>
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
    // 信号处理仍使用 stderr，因为 Logger 可能正在关闭
    std::cerr << "Interrupt signal (" << signal << ") received.\n";
    g_running = false;
}

void printUsage(const char* program_name) {
    std::cerr << "Usage: " << program_name << " [options]\n"
              << "Options:\n"
              << "  --did-registry <path>    Path to DID registry YAML\n"
              << "  --diag-config <path>     Path to diag config YAML\n"
              << "  --someip-config <path>   Path to SOME/IP config YAML\n"
              << "  --sec-config <path>      Path to CGW-SEC SOME/IP config YAML\n"
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
    std::string sec_config_path = constants::DEFAULT_SEC_CONFIG_PATH;
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
        } else if (arg == "--sec-config" && i + 1 < argc) {
            sec_config_path = argv[++i];
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

        // CGW-DIAG-DSN-CR-008: 启动顺序
        // 加载基础配置 -> 读取 common.log.* / diag.log.* -> Logger::init
        // -> 创建模块 Logger -> 加载 DidRegistry / VinStore -> 初始化各组件 -> 开放服务
        DiagLogAdapter::orchestrator().info(diag_events::SERVICE_STARTING,
            "CGW-DIAG Service Starting"
        );

        // 加载结构化日志配置并初始化 Logger（在创建业务模块之前）
        auto log_config = ConfigLoader::loadLogConfig(diag_config_path);
        auto log_result = DiagLogAdapter::init("cgw-diag", log_config);
        if (log_result.error != cgw::fw::log::LogError::kOk) {
            // 严格模式初始化失败时，在开放服务前终止启动
            std::cerr << "FATAL: Logger init failed: " << log_result.error_message << std::endl;
            return 1;
        }

        DiagLogAdapter::orchestrator().info(diag_events::SERVICE_CONFIG_LOADED,
            "Configuration loaded successfully",
            {dlog::f_str("diag_config_path", diag_config_path)}
        );

        DiagLogAdapter::orchestrator().info(diag_events::SERVICE_LOG_INITIALIZED,
            "Logger initialized successfully",
            {dlog::f_str("service", "cgw-diag"),
             dlog::f_str("log_level", cgw::fw::log::logLevelToString(log_config.level)),
             dlog::f_bool("strict_mode", log_config.strict)}
        );

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
        auto vin_store = std::shared_ptr<VinStore>(createVinStore("config/vin.dat").release());

        // Create CGW-SEC client proxy
        auto sec_client_proxy = createCgwSecClientProxy();

        // Create version collector and FOTA facade
        auto collector = std::make_shared<VersionCollector>(registry, router, docan_transport, doip_transport, vin_store);
        auto facade = std::make_shared<FotaFacade>(collector, registry, vin_store);

        // Load SOME/IP configuration
        SomeIpServiceConfig someip_config;
        try {
            someip_config = ConfigLoader::loadSomeIpConfig(someip_config_path);
        } catch (const std::exception& e) {
            DiagLogAdapter::facade().warn("diag.service.config_fallback",
                "Failed to load SOME/IP config, using defaults",
                {dlog::f_str("reason", e.what())}
            );
            someip_config.service_id = constants::CGW_DIAG_SERVICE_ID;
            someip_config.instance_id = constants::CGW_DIAG_INSTANCE_ID;
            someip_config.major_version = constants::CGW_DIAG_MAJOR_VERSION;
            someip_config.minor_version = constants::CGW_DIAG_MINOR_VERSION;
        }

        // Load CGW-SEC SOME/IP configuration
        SomeIpServiceConfig sec_someip_config;
        try {
            sec_someip_config = ConfigLoader::loadSomeIpConfig(sec_config_path);
        } catch (const std::exception& e) {
            DiagLogAdapter::sec_client().warn("diag.service.config_fallback",
                "Failed to load CGW-SEC SOME/IP config, using defaults",
                {dlog::f_str("reason", e.what())}
            );
            sec_someip_config.service_id = 0x1140;  // CGW-SEC-DSN-CR-003
            sec_someip_config.instance_id = 0x0001;
            sec_someip_config.major_version = 1;
            sec_someip_config.minor_version = 0;
        }

        // Override network config from command line
        someip_config.ip_address = someip_ip;
        someip_config.port = someip_port;

        DiagLogAdapter::facade().info("diag.service.network_configured",
            "Network configuration loaded",
            {dlog::f_str("someip_address", someip_ip),
             dlog::f_int("someip_port", someip_port)}
        );

        // Initialize CGW-SEC client proxy
        DiagLogAdapter::sec_client().info("diag.service.sec_client_initializing",
            "Initializing CGW-SEC client proxy"
        );
        if (!sec_client_proxy->initialize(sec_someip_config)) {
            DiagLogAdapter::sec_client().error("diag.service.sec_client_init_failed",
                "Failed to initialize CGW-SEC client proxy"
            );
            return 1;
        }

        // Connect to CGW-SEC service
        DiagLogAdapter::sec_client().info("diag.service.sec_client_connecting",
            "Connecting to CGW-SEC service"
        );
        if (!sec_client_proxy->connect()) {
            DiagLogAdapter::sec_client().warn("diag.service.sec_client_connect_failed",
                "Failed to connect to CGW-SEC service. Security features may be unavailable."
            );
            // Continue startup without SEC connection
        } else {
            DiagLogAdapter::sec_client().info("diag.service.sec_client_connected",
                "Successfully connected to CGW-SEC service"
            );
        }

        // Create diagnostic server with initialized SEC proxy
        auto diag_server = std::make_shared<DiagnosticServer>(vin_store, std::move(sec_client_proxy));

        // Create SOME/IP service
        auto someip_service = createSomeIpService();
        FotaSomeIpService fota_service(facade, std::move(someip_service));

        if (!fota_service.initialize(someip_config)) {
            DiagLogAdapter::facade().error("diag.service.someip_init_failed",
                "Failed to initialize SOME/IP service"
            );
            return 1;
        }

        if (!fota_service.start()) {
            DiagLogAdapter::facade().error("diag.service.someip_start_failed",
                "Failed to start SOME/IP service"
            );
            return 1;
        }

        if (!fota_service.offerService()) {
            DiagLogAdapter::facade().error("diag.service.someip_offer_failed",
                "Failed to offer SOME/IP service"
            );
            return 1;
        }

        DiagLogAdapter::facade().info("diag.service.someip_offered",
            "SOME/IP service offered",
            {dlog::f_str("someip_service_id", hex_id(someip_config.service_id)),
             dlog::f_str("someip_instance_id", hex_id(someip_config.instance_id)),
             dlog::f_str("transport", "TCP"),
             dlog::f_int("port", someip_config.port)}
        );

        DiagLogAdapter::vin_store().info("diag.service.vin_store_status",
            "VIN Store status",
            {dlog::f_str("provisioned", vin_store->isVinProvisioned() ? "true" : "false")}
        );

        DiagLogAdapter::orchestrator().info(diag_events::SERVICE_READY,
            "CGW-DIAG Service started successfully",
            {dlog::f_str("someip_config_path", someip_config_path),
             dlog::f_str("registry_version", registry->getRegistryVersion())}
        );

        // Main loop
        while (g_running) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }

        DiagLogAdapter::orchestrator().info(diag_events::SERVICE_SHUTTING_DOWN,
            "Shutting down..."
        );
        fota_service.stop();

        DiagLogAdapter::orchestrator().info("diag.service.stopped",
            "CGW-DIAG Service stopped"
        );

        return 0;

    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }
}
