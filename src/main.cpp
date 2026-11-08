#include "config_loader.h"
#include "did_registry.h"
#include "uds_client.h"
#include "diagnostic_router.h"
#include "do_can_transport.h"
#include "doip_transport.h"
#include "version_collector.h"
#include "fota_facade.h"
#include "fota_someip_service.h"
#include "fota_version_registrar.h"
#include "someip_service.h"
#include "vin_store.h"
#include "ecu_inventory_store.h"
#include "diagnostic_server.h"
#include "ccu_sec_client_proxy.h"
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

using namespace ccu_diag;

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
              << "  --diag-config <path>     Path to unified DIAG config (default: "
              << constants::DEFAULT_DIAG_CONFIG_PATH << ")\n"
              << "  --common-config <path>   Path to common config (dev/test override; default: "
              << constants::DEFAULT_COMMON_CONFIG_PATH << ")\n"
              << "  --did-registry <path>    Dev/test override: legacy DID registry YAML\n"
              << "  --someip-config <path>   Dev/test override: legacy SOME/IP config YAML\n"
              << "  --sec-config <path>      Dev/test override: legacy CCU-SEC SOME/IP config YAML\n"
              << "  --can-interface <name>   CAN interface name\n"
              << "  --can-baudrate <rate>    CAN baudrate\n"
              << "  --doip-ip <ip>           DoIP server IP address\n"
              << "  --doip-port <port>       DoIP server port\n"
              << "  --someip-ip <ip>         SOME/IP listen address\n"
              << "  --someip-port <port>     SOME/IP listen port\n"
              << "  --version                Print application version\n"
              << "  --help                   Show this help message\n";
}

} // namespace

int main(int argc, char* argv[]) {
    // Production defaults (CCU-DIAG-DSN-CR-009 §5.3). The production unit
    // relies only on these; per-domain paths are explicit dev/test overrides.
    std::string diag_config_path = constants::DEFAULT_DIAG_CONFIG_PATH;
    std::string common_config_path = constants::DEFAULT_COMMON_CONFIG_PATH;
    std::string did_registry_path;
    std::string someip_config_path;
    std::string sec_config_path;

    // Command-line transport overrides (applied on top of diag.transport).
    bool has_can_interface = false;
    bool has_can_baudrate = false;
    bool has_doip_ip = false;
    bool has_doip_port = false;
    bool has_someip_ip = false;
    bool has_someip_port = false;
    std::string can_interface;
    uint32_t can_baudrate = 0;
    std::string doip_ip;
    uint16_t doip_port = 0;
    std::string someip_ip;
    uint16_t someip_port = 0;

    bool show_version = false;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--diag-config" && i + 1 < argc) {
            diag_config_path = argv[++i];
        } else if (arg == "--common-config" && i + 1 < argc) {
            common_config_path = argv[++i];
        } else if (arg == "--did-registry" && i + 1 < argc) {
            did_registry_path = argv[++i];
        } else if (arg == "--someip-config" && i + 1 < argc) {
            someip_config_path = argv[++i];
        } else if (arg == "--sec-config" && i + 1 < argc) {
            sec_config_path = argv[++i];
        } else if (arg == "--can-interface" && i + 1 < argc) {
            can_interface = argv[++i];
            has_can_interface = true;
        } else if (arg == "--can-baudrate" && i + 1 < argc) {
            can_baudrate = static_cast<uint32_t>(std::stoul(argv[++i]));
            has_can_baudrate = true;
        } else if (arg == "--doip-ip" && i + 1 < argc) {
            doip_ip = argv[++i];
            has_doip_ip = true;
        } else if (arg == "--doip-port" && i + 1 < argc) {
            doip_port = static_cast<uint16_t>(std::stoi(argv[++i]));
            has_doip_port = true;
        } else if (arg == "--someip-ip" && i + 1 < argc) {
            someip_ip = argv[++i];
            has_someip_ip = true;
        } else if (arg == "--someip-port" && i + 1 < argc) {
            someip_port = static_cast<uint16_t>(std::stoi(argv[++i]));
            has_someip_port = true;
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

        // CCU-DIAG-DSN-CR-008: 启动顺序
        // 加载基础配置 -> 读取 common.log.* / diag.log.* -> Logger::init
        // -> 创建模块 Logger -> 加载 DidRegistry / VinStore -> 初始化各组件 -> 开放服务
        DiagLogAdapter::orchestrator().info(diag_events::SERVICE_STARTING,
            "CCU-DIAG Service Starting"
        );

        // CCU-DIAG-DSN-CR-009: unified diag.yaml + BUILD-owned common.yaml.
        // Fail closed on missing/migrated/unknown configuration.
        UnifiedDiagConfig unified;
        try {
            unified = ConfigLoader::loadUnifiedConfig(diag_config_path, common_config_path);
        } catch (const std::exception& e) {
            std::cerr << "FATAL: config error: " << e.what() << std::endl;
            return 1;
        }

        // 加载结构化日志配置并初始化 Logger（在创建业务模块之前）
        auto log_config = unified.log;
        auto log_result = DiagLogAdapter::init("ccu-diag", log_config);
        if (log_result.error != ccu::fw::log::LogError::kOk) {
            // 严格模式初始化失败时，在开放服务前终止启动
            std::cerr << "FATAL: Logger init failed: " << log_result.error_message << std::endl;
            return 1;
        }

        DiagLogAdapter::orchestrator().info(diag_events::SERVICE_CONFIG_LOADED,
            "Configuration loaded successfully",
            {dlog::f_str("diag_config_path", diag_config_path),
             dlog::f_str("common_config_path", common_config_path)}
        );

        DiagLogAdapter::orchestrator().info(diag_events::SERVICE_LOG_INITIALIZED,
            "Logger initialized successfully",
            {dlog::f_str("service", "ccu-diag"),
             dlog::f_str("log_level", ccu::fw::log::logLevelToString(log_config.level)),
             dlog::f_bool("strict_mode", log_config.strict)}
        );

        // Load DID registry: unified subtree unless a dev/test override is given.
        std::shared_ptr<DidRegistry> registry;
        if (!did_registry_path.empty()) {
            registry = std::make_shared<DidRegistry>(did_registry_path);
        } else {
            registry = std::make_shared<DidRegistry>(unified.registry);
        }

        // Apply command-line transport overrides on top of diag.transport.
        TransportConfig transport = unified.transport;
        if (has_can_interface) {
            transport.can_interface = can_interface;
        }
        if (has_can_baudrate) {
            transport.can_baudrate = can_baudrate;
        }
        if (has_doip_ip) {
            transport.doip_ip = doip_ip;
        }
        if (has_doip_port) {
            transport.doip_port = doip_port;
        }
        if (has_someip_ip) {
            unified.someip.ip_address = someip_ip;
        }
        if (has_someip_port) {
            unified.someip.port = someip_port;
        }

        // Create transports
        auto docan_transport = std::make_shared<DoCanTransport>(
            transport.can_interface, transport.can_baudrate);
        auto doip_transport = std::make_shared<DoIpTransport>(
            transport.doip_ip, transport.doip_port);

        // Create UDS client and router
        auto uds_client = std::make_shared<UdsClient>(docan_transport, unified.diag);
        auto router = std::make_shared<DiagnosticRouter>(registry, uds_client, docan_transport, doip_transport);

        // Create VIN store (device instance state; never release-managed config)
        auto vin_store = std::shared_ptr<VinStore>(
            createVinStore(constants::DEFAULT_VIN_STORAGE_PATH).release());

        // Create ECU inventory store (instance state + optional seed import).
        // CCU-DIAG-DSN-CR-010 §3/§12.3: opened after VIN, before Tester/
        // Router/Transport/SEC/Provider. Fail-closed: on corruption or an
        // invalid seed the process keeps running but never serves an empty
        // "successful" inventory (0x0002 -> 1012/1013, 0x0005 -> 1015).
        std::vector<std::string> known_ecu_ids;
        for (const auto& ecu : registry->getAllEcus()) {
            known_ecu_ids.push_back(ecu.ecu_id);
        }
        auto inventory_store = std::make_shared<EcuInventoryStore>(
            unified.inventory, known_ecu_ids);
        const bool had_inventory_record = inventory_store->persistentRecordExists();
        const auto inventory_result =
            inventory_store->initialize(registry->getRegistryVersion());
        if (!inventory_result.ok()) {
            const std::string error_code = inventory_result.diagCode();
            const bool seed_mode =
                inventory_store->mode() == InventoryMode::SEED;
            DiagLogAdapter::inventory_store().error(
                seed_mode ? diag_events::INVENTORY_SEED_IMPORT_FAILED
                          : diag_events::INVENTORY_LOAD_FAILED,
                "ECU inventory unavailable, serving fail-closed",
                {dlog::f_str("state_path", unified.inventory.state_path),
                 dlog::f_str("seed_path", unified.inventory.seed_path),
                 dlog::f_str("mode", inventoryModeToString(inventory_store->mode())),
                 dlog::f_str("error_code", error_code),
                 dlog::f_str("reason", inventory_result.detail)}
            );
        } else {
            DiagLogAdapter::inventory_store().info(diag_events::INVENTORY_LOADED,
                "ECU inventory loaded",
                {dlog::f_str("state_path", unified.inventory.state_path),
                 dlog::f_str("mode", inventoryModeToString(inventory_store->mode())),
                 dlog::f_str("inventory_source",
                     inventory_store->record().inventory_source),
                 dlog::f_int("ecu_count", static_cast<int64_t>(
                     inventory_store->record().ecus.size()))}
            );
            if (inventory_store->mode() == InventoryMode::SEED) {
                DiagLogAdapter::inventory_store().warn(
                    diag_events::INVENTORY_SEED_MODE_ENABLED,
                    "Inventory seed mode enabled: values are declarations, not hardware reads",
                    {dlog::f_str("seed_path", unified.inventory.seed_path),
                     dlog::f_int("ecu_count", static_cast<int64_t>(
                         inventory_store->record().ecus.size()))}
                );
                if (!had_inventory_record) {
                    DiagLogAdapter::inventory_store().info(
                        diag_events::INVENTORY_SEED_IMPORTED,
                        "Inventory seed imported",
                        {dlog::f_str("seed_path", unified.inventory.seed_path),
                         dlog::f_str("seed_digest",
                             inventory_store->record().seed_digest),
                         dlog::f_int("ecu_count", static_cast<int64_t>(
                             inventory_store->record().ecus.size()))}
                    );
                }
            }
        }

        // Create CCU-SEC client proxy
        auto sec_client_proxy = createCcuSecClientProxy();

        // Create version collector and FOTA facade
        auto collector = std::make_shared<VersionCollector>(
            registry, router, docan_transport, doip_transport, vin_store,
            inventory_store);
        auto registrar = std::make_shared<FotaVersionRegistrar>(
            inventory_store, known_ecu_ids, unified.inventory.idempotency_window);
        auto facade = std::make_shared<FotaFacade>(collector, registry, vin_store,
                                                  registrar);

        // SOME/IP provider config: unified subtree by default, explicit
        // dev/test override supported.
        SomeIpServiceConfig someip_config = unified.someip;
        if (!someip_config_path.empty()) {
            try {
                someip_config = ConfigLoader::loadSomeIpConfig(someip_config_path);
            } catch (const std::exception& e) {
                DiagLogAdapter::facade().warn("diag.service.config_fallback",
                    "Failed to load SOME/IP override, using unified config",
                    {dlog::f_str("reason", e.what())}
                );
            }
        }

        // CCU-SEC SOME/IP config: unified subtree by default.
        SomeIpServiceConfig sec_someip_config = unified.sec_someip;
        if (!sec_config_path.empty()) {
            try {
                sec_someip_config = ConfigLoader::loadSomeIpConfig(sec_config_path);
            } catch (const std::exception& e) {
                DiagLogAdapter::sec_client().warn("diag.service.config_fallback",
                    "Failed to load CCU-SEC SOME/IP override, using unified config",
                    {dlog::f_str("reason", e.what())}
                );
            }
        }

        DiagLogAdapter::facade().info("diag.service.network_configured",
            "Network configuration loaded",
            {dlog::f_str("someip_address", someip_config.ip_address),
             dlog::f_int("someip_port", someip_config.port)}
        );

        // Initialize CCU-SEC client proxy
        DiagLogAdapter::sec_client().info("diag.service.sec_client_initializing",
            "Initializing CCU-SEC client proxy"
        );
        if (!sec_client_proxy->initialize(sec_someip_config)) {
            DiagLogAdapter::sec_client().error("diag.service.sec_client_init_failed",
                "Failed to initialize CCU-SEC client proxy"
            );
            return 1;
        }

        // Connect to CCU-SEC service
        DiagLogAdapter::sec_client().info("diag.service.sec_client_connecting",
            "Connecting to CCU-SEC service"
        );
        if (!sec_client_proxy->connect()) {
            DiagLogAdapter::sec_client().warn("diag.service.sec_client_connect_failed",
                "Failed to connect to CCU-SEC service. Security features may be unavailable."
            );
            // Continue startup without SEC connection
        } else {
            DiagLogAdapter::sec_client().info("diag.service.sec_client_connected",
                "Successfully connected to CCU-SEC service"
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
            {dlog::f_str("provisioned", vin_store->isVinProvisioned() ? "true" : "false"),
             dlog::f_str("storage_path", constants::DEFAULT_VIN_STORAGE_PATH)}
        );

        DiagLogAdapter::orchestrator().info(diag_events::SERVICE_READY,
            "CCU-DIAG Service started successfully",
            {dlog::f_str("diag_config_path", diag_config_path),
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
            "CCU-DIAG Service stopped"
        );

        return 0;

    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }
}
