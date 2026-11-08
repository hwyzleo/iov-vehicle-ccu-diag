#include <gtest/gtest.h>
#include <config_loader.h>
#include <constants.h>
#include <log_types.h>

#include <filesystem>
#include <fstream>
#include <string>

namespace ccu_diag {
namespace tests {

namespace fs = std::filesystem;

class UnifiedConfigTest : public ::testing::Test {
protected:
    fs::path temp_dir_;

    void SetUp() override {
        temp_dir_ = fs::temp_directory_path() / "ccu_diag_unified_test";
        fs::remove_all(temp_dir_);
        fs::create_directories(temp_dir_);
    }

    void TearDown() override {
        fs::remove_all(temp_dir_);
    }

    fs::path diagPath() const { return temp_dir_ / "diag.yaml"; }
    fs::path commonPath() const { return temp_dir_ / "common.yaml"; }

    void writeFile(const fs::path& path, const std::string& content) {
        std::ofstream file(path);
        file << content;
        file.close();
    }

    void writeCommon(const std::string& level = "WARN") {
        writeFile(commonPath(), R"(
common:
  log:
    schema_version: 1
    level: )" + level + R"(
    strict: true
    console:
      enabled: false
    format: json
)");
    }

    void writeUnified(const std::string& extra_top_level = "",
                      const std::string& extra_diag = "",
                      bool with_platform_values = true,
                      const std::string& inventory_override = "") {
        std::string network = with_platform_values ? R"(    network:
      ip_address: "127.0.0.1"
      port: 51110)" : "";
        std::string sec_network = with_platform_values ? R"(      network:
        ip_address: "127.0.0.1"
        port: 51140)" : "";
        std::string transport = with_platform_values ? R"(  transport:
    can:
      interface: can1
      baudrate: 250000
    doip:
      ip: 10.0.0.1
      port: 13400)" : "  transport: {}";
        std::string inventory = inventory_override;
        if (inventory.empty()) {
            inventory = R"(  inventory:
    mode: hardware
    seed_path: /usr/share/ccu-diag/ecu-inventory.seed.json
    state_path: /var/lib/ccu/diag/ecu_inventory.json
    idempotency_window: 64)";
        } else if (inventory == "@omit") {
            inventory.clear();
        }

        std::string text = R"(
diag:
  log:
    level: INFO
    modules:
      router: ERROR
  timing:
    p2_server_max_ms: 100
    p2_star_server_max_ms: 5000
    s3_session_timeout_ms: 5000
    tester_present_interval_ms: 2000
    request_timeout_ms: 2000
    retry_interval_ms: 100
  retry:
    max_count: 3
@INVENTORY@
  registry:
    registryVersion: "2026.07.1"
    baselineDids: [0xF195, 0xF188]
    ecus:
      - ecuId: "BMS"
        logicalAddress: 0x1A
        transport: "DoCAN"
        readMethod: "UDS_0x22"
        securityRequired: false
        dids:
          - did: 0xF195
            field: "swVersion"
            encoding: "ASCII"
            length: 16
      - ecuId: "IDCM"
        logicalAddress: 0x20
        transport: "DoIP"
        readMethod: "SOMEIP_getVersion"
        someipService: "0x1110"
        fields: ["swVersion", "hwVersion"]
@TRANSPORT@
  someip:
    application_name: ccu-diag
    service:
      service_id: 0x1110
      instance_id: 0x0001
      major_version: 1
      minor_version: 0
    methods:
      read_vin:
        method_id: 0x0001
    events:
      inventory_result:
        event_id: 0x8001
    timing:
      request_timeout_ms: 2000
    sd:
      enabled: true
      mode: "static"
@NETWORK@
  sec_client:
    someip:
      application_name: ccu-diag
      service:
        service_id: 0x1140
        instance_id: 0x0001
        major_version: 1
        minor_version: 0
@SEC_NETWORK@
      timing:
        request_timeout_ms: 2000
      sd:
        enabled: true
        mode: "static"
@EXTRA_DIAG@
@EXTRA_TOP@)";

        const auto replaceToken = [&text](const std::string& token,
                                          const std::string& value) {
            const auto pos = text.find(token);
            if (pos != std::string::npos) {
                text.replace(pos, token.size(), value);
            }
        };
        replaceToken("@INVENTORY@", inventory);
        replaceToken("@TRANSPORT@", transport);
        replaceToken("@NETWORK@", network);
        replaceToken("@SEC_NETWORK@", sec_network);
        replaceToken("@EXTRA_DIAG@", extra_diag);
        replaceToken("@EXTRA_TOP@", extra_top_level);

        writeFile(diagPath(), text);
    }
};

TEST_F(UnifiedConfigTest, MapsAllLegacyFieldsIntoDiagSubtrees) {
    writeCommon();
    writeUnified();

    UnifiedDiagConfig config = ConfigLoader::loadUnifiedConfig(diagPath(), commonPath());

    // did_registry.yaml -> diag.registry
    EXPECT_EQ(config.registry.registry_version, "2026.07.1");
    ASSERT_EQ(config.registry.baseline_dids.size(), 2);
    EXPECT_EQ(config.registry.baseline_dids[0], 0xF195);
    ASSERT_EQ(config.registry.ecus.size(), 2);
    EXPECT_EQ(config.registry.ecus[0].ecu_id, "BMS");
    EXPECT_EQ(config.registry.ecus[0].logical_address, 0x1A);
    EXPECT_EQ(config.registry.ecus[0].transport, TransportType::DOCAN);
    ASSERT_EQ(config.registry.ecus[0].dids.size(), 1);
    EXPECT_EQ(config.registry.ecus[1].read_method, ReadMethod::SOMEIP_GET_VERSION);
    EXPECT_EQ(config.registry.ecus[1].someip_service, "0x1110");

    // diag_config.yaml timing/retry -> diag.timing / diag.retry
    EXPECT_EQ(config.diag.p2_server_max_ms, 100u);
    EXPECT_EQ(config.diag.max_retry_count, 3u);

    // transport platform values
    EXPECT_EQ(config.transport.can_interface, "can1");
    EXPECT_EQ(config.transport.can_baudrate, 250000u);
    EXPECT_EQ(config.transport.doip_ip, "10.0.0.1");
    EXPECT_EQ(config.transport.doip_port, 13400u);

    // diag.inventory (CCU-DIAG-DSN-CR-010)
    EXPECT_TRUE(config.inventory.mode == InventoryMode::HARDWARE ||
                config.inventory.mode == InventoryMode::SEED);
    EXPECT_EQ(config.inventory.seed_path,
              constants::DEFAULT_INVENTORY_SEED_PATH);
    EXPECT_EQ(config.inventory.state_path,
              constants::DEFAULT_INVENTORY_STATE_PATH);
    EXPECT_EQ(config.inventory.idempotency_window, 64u);

    // someip_config.yaml -> diag.someip
    EXPECT_EQ(config.someip.service_id, constants::CCU_DIAG_SERVICE_ID);
    EXPECT_EQ(config.someip.instance_id, constants::CCU_DIAG_INSTANCE_ID);
    EXPECT_EQ(config.someip.application_name, "ccu-diag");
    EXPECT_EQ(config.someip.ip_address, "127.0.0.1");
    EXPECT_EQ(config.someip.port, 51110u);

    // sec_config.yaml -> diag.sec_client.someip (same process application)
    EXPECT_EQ(config.sec_someip.service_id, constants::CCU_SEC_SERVICE_ID);
    EXPECT_EQ(config.sec_someip.instance_id, constants::CCU_SEC_INSTANCE_ID);
    EXPECT_EQ(config.sec_someip.application_name, "ccu-diag");
    EXPECT_EQ(config.sec_someip.port, 51140u);

    // common.log.* from common.yaml, overridden by diag.log.*
    EXPECT_EQ(config.log.level, ccu::fw::log::LogLevel::kInfo);
    EXPECT_TRUE(config.log.strict);
    EXPECT_FALSE(config.log.console_config.enabled);
    EXPECT_EQ(config.log.format, "json");
    ASSERT_EQ(config.log.module_levels.count("router"), 1u);
    EXPECT_EQ(config.log.module_levels.at("router"), ccu::fw::log::LogLevel::kError);
}

TEST_F(UnifiedConfigTest, OptionalPlatformValuesFallBackToSafeDefaults) {
    writeCommon();
    writeUnified("", "", false);

    UnifiedDiagConfig config = ConfigLoader::loadUnifiedConfig(diagPath(), commonPath());

    EXPECT_EQ(config.transport.can_interface, constants::DEFAULT_CAN_INTERFACE);
    EXPECT_EQ(config.transport.can_baudrate, constants::DEFAULT_CAN_BAUDRATE);
    EXPECT_EQ(config.transport.doip_ip, constants::DEFAULT_DOIP_IP);
    EXPECT_EQ(config.transport.doip_port, constants::DEFAULT_DOIP_PORT);
    EXPECT_EQ(config.someip.ip_address, constants::DEFAULT_SOMEIP_ADDRESS);
    EXPECT_EQ(config.someip.port, constants::DEFAULT_SOMEIP_PORT);
    EXPECT_EQ(config.sec_someip.ip_address, constants::DEFAULT_SEC_SOMEIP_ADDRESS);
    EXPECT_EQ(config.sec_someip.port, constants::DEFAULT_SEC_SOMEIP_PORT);
}

TEST_F(UnifiedConfigTest, RejectsTopLevelCommonInServiceFile) {
    writeCommon();
    writeUnified(R"(
common:
  log:
    level: INFO
)");

    EXPECT_THROW(ConfigLoader::loadUnifiedConfig(diagPath(), commonPath()),
                 std::runtime_error);
}

TEST_F(UnifiedConfigTest, RejectsDeprecatedRootNodes) {
    writeCommon();
    writeUnified(R"(
timing:
  p2_server_max_ms: 50
)");

    EXPECT_THROW(ConfigLoader::loadUnifiedConfig(diagPath(), commonPath()),
                 std::runtime_error);
}

TEST_F(UnifiedConfigTest, RejectsUnknownDiagSubKey) {
    writeCommon();
    writeUnified("", R"(
  unknown_platform_knob: 1)");

    EXPECT_THROW(ConfigLoader::loadUnifiedConfig(diagPath(), commonPath()),
                 std::runtime_error);
}

TEST_F(UnifiedConfigTest, RejectsLegacyVinConfigUnderRegistry) {
    writeCommon();
    writeFile(diagPath(), R"(
diag:
  log: { level: INFO }
  timing:
    p2_server_max_ms: 50
    p2_star_server_max_ms: 5000
    s3_session_timeout_ms: 5000
    tester_present_interval_ms: 2000
    request_timeout_ms: 2000
    retry_interval_ms: 100
  retry: { max_count: 2 }
  transport: {}
  registry:
    registryVersion: "2026.07.1"
    baselineDids: [0xF195]
    ecus: []
    vinConfig:
      storagePath: "config/vin.dat"
  someip:
    application_name: ccu-diag
    service:
      service_id: 0x1110
      instance_id: 0x0001
      major_version: 1
      minor_version: 0
  sec_client:
    someip:
      application_name: ccu-diag
      service:
        service_id: 0x1140
        instance_id: 0x0001
        major_version: 1
        minor_version: 0
)");

    EXPECT_THROW(ConfigLoader::loadUnifiedConfig(diagPath(), commonPath()),
                 std::runtime_error);
}

TEST_F(UnifiedConfigTest, MissingCommonFileFailsClosed) {
    writeUnified();

    EXPECT_THROW(ConfigLoader::loadUnifiedConfig(diagPath(),
                                                 temp_dir_ / "missing.yaml"),
                 std::runtime_error);
}

// The shipped default template must stay loadable and keep the stable
// Service/Instance/Method IDs.
TEST_F(UnifiedConfigTest, ShippedDefaultTemplateLoads) {
    const fs::path repo = CCU_DIAG_SOURCE_DIR;
    UnifiedDiagConfig config = ConfigLoader::loadUnifiedConfig(
        repo / "config" / "diag.default.yaml",
        repo / "config" / "dev" / "common.yaml");

    // CCU-DIAG-DSN-CR-011: the shipped default is the project configuration, so
    // the registry data version is asserted as "present", not as a literal.
    EXPECT_FALSE(config.registry.registry_version.empty());
    EXPECT_FALSE(config.registry.ecus.empty());
    EXPECT_EQ(config.someip.service_id, constants::CCU_DIAG_SERVICE_ID);
    EXPECT_EQ(config.someip.instance_id, constants::CCU_DIAG_INSTANCE_ID);
    EXPECT_EQ(config.sec_someip.service_id, constants::CCU_SEC_SERVICE_ID);
    EXPECT_EQ(config.someip.application_name, "ccu-diag");
    EXPECT_EQ(config.sec_someip.application_name, "ccu-diag");
    EXPECT_EQ(config.transport.can_interface, constants::DEFAULT_CAN_INTERFACE);
    EXPECT_EQ(config.someip.ip_address, constants::DEFAULT_SOMEIP_ADDRESS);
    EXPECT_TRUE(config.inventory.mode == InventoryMode::HARDWARE ||
                config.inventory.mode == InventoryMode::SEED);
    EXPECT_EQ(config.inventory.state_path,
              constants::DEFAULT_INVENTORY_STATE_PATH);
    EXPECT_EQ(config.inventory.seed_path,
              constants::DEFAULT_INVENTORY_SEED_PATH);
}

TEST_F(UnifiedConfigTest, InventorySeedModeParsed) {
    writeCommon();
    writeUnified("", "", true, R"(
  inventory:
    mode: seed
    seed_path: /usr/share/ccu-diag/ecu-inventory.seed.json
    state_path: /var/lib/ccu/diag/ecu_inventory.json
    idempotency_window: 16)");

    UnifiedDiagConfig config = ConfigLoader::loadUnifiedConfig(diagPath(),
                                                               commonPath());
    EXPECT_EQ(config.inventory.mode, InventoryMode::SEED);
    EXPECT_EQ(config.inventory.idempotency_window, 16u);
}

TEST_F(UnifiedConfigTest, MissingInventorySubtreeFailsClosed) {
    writeCommon();
    writeUnified("", "", true, "@omit");

    EXPECT_THROW(ConfigLoader::loadUnifiedConfig(diagPath(), commonPath()),
                 std::runtime_error);
}

TEST_F(UnifiedConfigTest, UnknownInventoryKeyRejected) {
    writeCommon();
    writeUnified("", "", true, R"(
  inventory:
    mode: hardware
    state_path: /var/lib/ccu/diag/ecu_inventory.json
    unknown_knob: 1)");

    EXPECT_THROW(ConfigLoader::loadUnifiedConfig(diagPath(), commonPath()),
                 std::runtime_error);
}

TEST_F(UnifiedConfigTest, AutoInventoryModeRejected) {
    writeCommon();
    writeUnified("", "", true, R"(
  inventory:
    mode: auto
    state_path: /var/lib/ccu/diag/ecu_inventory.json)");

    EXPECT_THROW(ConfigLoader::loadUnifiedConfig(diagPath(), commonPath()),
                 std::runtime_error);
}

TEST_F(UnifiedConfigTest, StatePathOutsideAllowedDirectoryRejected) {
    writeCommon();
    writeUnified("", "", true, R"(
  inventory:
    mode: hardware
    state_path: /etc/ccu/ecu_inventory.json)");

    EXPECT_THROW(ConfigLoader::loadUnifiedConfig(diagPath(), commonPath()),
                 std::runtime_error);
}

TEST_F(UnifiedConfigTest, StatePathTraversalRejected) {
    writeCommon();
    writeUnified("", "", true, R"(
  inventory:
    mode: hardware
    state_path: /var/lib/ccu/diag/../../etc/ecu_inventory.json)");

    EXPECT_THROW(ConfigLoader::loadUnifiedConfig(diagPath(), commonPath()),
                 std::runtime_error);
}

TEST_F(UnifiedConfigTest, SeedModeWithoutSeedPathRejected) {
    writeCommon();
    writeUnified("", "", true, R"(
  inventory:
    mode: seed
    seed_path: ""
    state_path: /var/lib/ccu/diag/ecu_inventory.json)");

    EXPECT_THROW(ConfigLoader::loadUnifiedConfig(diagPath(), commonPath()),
                 std::runtime_error);
}

TEST_F(UnifiedConfigTest, IdempotencyWindowOutOfRangeRejected) {
    writeCommon();
    writeUnified("", "", true, R"(
  inventory:
    mode: hardware
    state_path: /var/lib/ccu/diag/ecu_inventory.json
    idempotency_window: 257)");

    EXPECT_THROW(ConfigLoader::loadUnifiedConfig(diagPath(), commonPath()),
                 std::runtime_error);
}

} // namespace tests
} // namespace ccu_diag
