#include <gtest/gtest.h>
#include "config_loader.h"
#include "constants.h"
#include <fstream>

namespace cgw_diag {
namespace tests {

class ConfigLoaderTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Create test YAML files
        createTestDidRegistry();
        createTestDiagConfig();
    }
    
    void TearDown() override {
        // Clean up test files
        std::remove("test_did_registry.yaml");
        std::remove("test_diag_config.yaml");
    }
    
    void createTestDidRegistry() {
        std::ofstream file("test_did_registry.yaml");
        file << R"(
registryVersion: "2026.07.0"
baselineDids: [0xF195, 0xF188, 0xF187, 0xF193]
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
      - did: 0xF187
        field: "partNumber"
        encoding: "ASCII"
        length: 11
  - ecuId: "IDCM"
    logicalAddress: 0x20
    transport: "DoIP"
    readMethod: "SOMEIP_getVersion"
    someipService: "0x1234"
    fields: ["swVersion", "hwVersion"]
)";
        file.close();
    }
    
    void createTestDiagConfig() {
        std::ofstream file("test_diag_config.yaml");
        file << R"(
timing:
  p2_server_max_ms: 50
  p2_star_server_max_ms: 5000
  s3_session_timeout_ms: 5000
  tester_present_interval_ms: 2000
  request_timeout_ms: 2000
  retry_interval_ms: 100
retry:
  max_count: 2
)";
        file.close();
    }
};

TEST_F(ConfigLoaderTest, LoadDidRegistrySuccess) {
    DidRegistryConfig config = ConfigLoader::loadDidRegistry("test_did_registry.yaml");
    
    EXPECT_EQ(config.registry_version, "2026.07.0");
    EXPECT_EQ(config.baseline_dids.size(), 4);
    EXPECT_EQ(config.baseline_dids[0], 0xF195);
    EXPECT_EQ(config.baseline_dids[1], 0xF188);
    EXPECT_EQ(config.baseline_dids[2], 0xF187);
    EXPECT_EQ(config.baseline_dids[3], 0xF193);
    
    ASSERT_EQ(config.ecus.size(), 2);
    
    // Test BMS ECU
    const auto& bms = config.ecus[0];
    EXPECT_EQ(bms.ecu_id, "BMS");
    EXPECT_EQ(bms.logical_address, 0x1A);
    EXPECT_EQ(bms.transport, TransportType::DOCAN);
    EXPECT_EQ(bms.read_method, ReadMethod::UDS_0x22);
    EXPECT_FALSE(bms.security_required);
    ASSERT_EQ(bms.dids.size(), 2);
    EXPECT_EQ(bms.dids[0].did, 0xF195);
    EXPECT_EQ(bms.dids[0].field, "swVersion");
    EXPECT_EQ(bms.dids[0].encoding, "ASCII");
    EXPECT_EQ(bms.dids[0].length, 16);
    
    // Test IDCM ECU
    const auto& idcm = config.ecus[1];
    EXPECT_EQ(idcm.ecu_id, "IDCM");
    EXPECT_EQ(idcm.logical_address, 0x20);
    EXPECT_EQ(idcm.transport, TransportType::DOIP);
    EXPECT_EQ(idcm.read_method, ReadMethod::SOMEIP_GET_VERSION);
    EXPECT_EQ(idcm.someip_service, "0x1234");
    ASSERT_EQ(idcm.fields.size(), 2);
    EXPECT_EQ(idcm.fields[0], "swVersion");
    EXPECT_EQ(idcm.fields[1], "hwVersion");
}

TEST_F(ConfigLoaderTest, LoadDiagConfigSuccess) {
    DiagConfig config = ConfigLoader::loadDiagConfig("test_diag_config.yaml");
    
    EXPECT_EQ(config.p2_server_max_ms, 50);
    EXPECT_EQ(config.p2_star_server_max_ms, 5000);
    EXPECT_EQ(config.s3_session_timeout_ms, 5000);
    EXPECT_EQ(config.tester_present_interval_ms, 2000);
    EXPECT_EQ(config.request_timeout_ms, 2000);
    EXPECT_EQ(config.retry_interval_ms, 100);
    EXPECT_EQ(config.max_retry_count, 2);
}

TEST_F(ConfigLoaderTest, LoadDidRegistryFileNotFound) {
    EXPECT_THROW(ConfigLoader::loadDidRegistry("nonexistent.yaml"), std::runtime_error);
}

TEST_F(ConfigLoaderTest, LoadDiagConfigFileNotFound) {
    EXPECT_THROW(ConfigLoader::loadDiagConfig("nonexistent.yaml"), std::runtime_error);
}

} // namespace tests
} // namespace cgw_diag
