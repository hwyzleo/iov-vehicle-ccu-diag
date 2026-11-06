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
                      bool with_platform_values = true) {
        std::string network = with_platform_values ? R"(
    network:
      ip_address: "127.0.0.1"
      port: 51110)" : "";
        std::string sec_network = with_platform_values ? R"(
      network:
        ip_address: "127.0.0.1"
        port: 51140)" : "";
        std::string transport = with_platform_values ? R"(
  transport:
    can:
      interface: can1
      baudrate: 250000
    doip:
      ip: 10.0.0.1
      port: 13400)" : R"(
  transport: {})";

        writeFile(diagPath(), R"(
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
)" + transport + R"(
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
)" + network + R"(
  sec_client:
    someip:
      application_name: ccu-diag
      service:
        service_id: 0x1140
        instance_id: 0x0001
        major_version: 1
        minor_version: 0
)" + sec_network + R"(
      timing:
        request_timeout_ms: 2000
      sd:
        enabled: true
        mode: "static"
)" + extra_diag + R"(
)" + extra_top_level);
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

    EXPECT_EQ(config.registry.registry_version, "2026.07.1");
    EXPECT_FALSE(config.registry.ecus.empty());
    EXPECT_EQ(config.someip.service_id, constants::CCU_DIAG_SERVICE_ID);
    EXPECT_EQ(config.someip.instance_id, constants::CCU_DIAG_INSTANCE_ID);
    EXPECT_EQ(config.sec_someip.service_id, constants::CCU_SEC_SERVICE_ID);
    EXPECT_EQ(config.someip.application_name, "ccu-diag");
    EXPECT_EQ(config.sec_someip.application_name, "ccu-diag");
    EXPECT_EQ(config.transport.can_interface, constants::DEFAULT_CAN_INTERFACE);
    EXPECT_EQ(config.someip.ip_address, constants::DEFAULT_SOMEIP_ADDRESS);
}

} // namespace tests
} // namespace ccu_diag
