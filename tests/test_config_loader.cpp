#include <gtest/gtest.h>
#include "config_loader.h"
#include "constants.h"
#include <fstream>
#include <filesystem>

namespace cgw_diag {
namespace tests {

namespace fs = std::filesystem;

class ConfigLoaderTest : public ::testing::Test {
protected:
    fs::path temp_dir_;

    void SetUp() override {
        temp_dir_ = fs::temp_directory_path() / "cgw_diag_test";
        fs::create_directories(temp_dir_);
        createTestDidRegistry();
        createTestDiagConfig();
    }
    
    void TearDown() override {
        fs::remove_all(temp_dir_);
    }
    
    fs::path didRegistryPath() const { return temp_dir_ / "test_did_registry.yaml"; }
    fs::path diagConfigPath() const { return temp_dir_ / "test_diag_config.yaml"; }
    fs::path tempPath(const std::string& name) const { return temp_dir_ / name; }
    
    void writeFile(const fs::path& path, const std::string& content) {
        std::ofstream file(path);
        file << content;
        file.close();
    }

    void createTestDidRegistry() {
        writeFile(didRegistryPath(), R"(
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
)");
    }
    
    void createTestDiagConfig() {
        writeFile(diagConfigPath(), R"(
timing:
  p2_server_max_ms: 50
  p2_star_server_max_ms: 5000
  s3_session_timeout_ms: 5000
  tester_present_interval_ms: 2000
  request_timeout_ms: 2000
  retry_interval_ms: 100
retry:
  max_count: 2
)");
    }
};

TEST_F(ConfigLoaderTest, LoadDidRegistrySuccess) {
    DidRegistryConfig config = ConfigLoader::loadDidRegistry(didRegistryPath());
    
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
    DiagConfig config = ConfigLoader::loadDiagConfig(diagConfigPath());
    
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

TEST_F(ConfigLoaderTest, LoadDidRegistryMalformedYaml) {
    auto path = tempPath("malformed.yaml");
    writeFile(path, R"(
registryVersion: "1.0"
ecus:
  - ecuId: "BMS"
    logicalAddress: 0x1A
    transport: [invalid yaml
)");
    EXPECT_THROW(ConfigLoader::loadDidRegistry(path), std::runtime_error);
}

TEST_F(ConfigLoaderTest, LoadDidRegistryInvalidTransport) {
    auto path = tempPath("bad_transport.yaml");
    writeFile(path, R"(
ecus:
  - ecuId: "BMS"
    logicalAddress: 0x1A
    transport: "Unknown"
    readMethod: "UDS_0x22"
)");
    EXPECT_THROW(ConfigLoader::loadDidRegistry(path), std::runtime_error);
}

TEST_F(ConfigLoaderTest, LoadDidRegistryInvalidReadMethod) {
    auto path = tempPath("bad_read_method.yaml");
    writeFile(path, R"(
ecus:
  - ecuId: "BMS"
    logicalAddress: 0x1A
    transport: "DoCAN"
    readMethod: "Unknown"
)");
    EXPECT_THROW(ConfigLoader::loadDidRegistry(path), std::runtime_error);
}

TEST_F(ConfigLoaderTest, LoadDidRegistryMissingEcuId) {
    auto path = tempPath("missing_ecuid.yaml");
    writeFile(path, R"(
ecus:
  - logicalAddress: 0x1A
    transport: "DoCAN"
    readMethod: "UDS_0x22"
)");
    EXPECT_THROW(ConfigLoader::loadDidRegistry(path), std::runtime_error);
}

TEST_F(ConfigLoaderTest, LoadDidRegistryMissingLogicalAddress) {
    auto path = tempPath("missing_la.yaml");
    writeFile(path, R"(
ecus:
  - ecuId: "BMS"
    transport: "DoCAN"
    readMethod: "UDS_0x22"
)");
    EXPECT_THROW(ConfigLoader::loadDidRegistry(path), std::runtime_error);
}

TEST_F(ConfigLoaderTest, LoadDidRegistryWrongLogicalAddressType) {
    auto path = tempPath("wrong_la_type.yaml");
    writeFile(path, R"(
ecus:
  - ecuId: "BMS"
    logicalAddress: "abc"
    transport: "DoCAN"
    readMethod: "UDS_0x22"
)");
    EXPECT_THROW(ConfigLoader::loadDidRegistry(path), std::runtime_error);
}

TEST_F(ConfigLoaderTest, LoadDiagConfigMalformedYaml) {
    auto path = tempPath("malformed_diag.yaml");
    writeFile(path, R"(
timing:
  p2_server_max_ms: 50
  [bad yaml
)");
    EXPECT_THROW(ConfigLoader::loadDiagConfig(path), std::runtime_error);
}

TEST_F(ConfigLoaderTest, LoadDiagConfigMissingTimingUsesDefaults) {
    auto path = tempPath("no_timing.yaml");
    writeFile(path, R"(
retry:
  max_count: 3
)");
    DiagConfig config = ConfigLoader::loadDiagConfig(path);
    
    EXPECT_EQ(config.p2_server_max_ms, constants::P2_SERVER_MAX_MS);
    EXPECT_EQ(config.p2_star_server_max_ms, constants::P2_STAR_SERVER_MAX_MS);
    EXPECT_EQ(config.s3_session_timeout_ms, constants::S3_SESSION_TIMEOUT_MS);
    EXPECT_EQ(config.tester_present_interval_ms, constants::TESTER_PRESENT_INTERVAL_MS);
    EXPECT_EQ(config.request_timeout_ms, constants::REQUEST_TIMEOUT_MS);
    EXPECT_EQ(config.retry_interval_ms, constants::RETRY_INTERVAL_MS);
    EXPECT_EQ(config.max_retry_count, 3);
}

TEST_F(ConfigLoaderTest, LoadDiagConfigMissingRetryUsesDefaults) {
    auto path = tempPath("no_retry.yaml");
    writeFile(path, R"(
timing:
  p2_server_max_ms: 100
)");
    DiagConfig config = ConfigLoader::loadDiagConfig(path);
    
    EXPECT_EQ(config.p2_server_max_ms, 100);
    EXPECT_EQ(config.max_retry_count, constants::MAX_RETRY_COUNT);
}

TEST_F(ConfigLoaderTest, LoadDidRegistryEmptyEcuList) {
    auto path = tempPath("empty_ecus.yaml");
    writeFile(path, R"(
registryVersion: "1.0"
ecus: []
)");
    DidRegistryConfig config = ConfigLoader::loadDidRegistry(path);
    EXPECT_EQ(config.registry_version, "1.0");
    EXPECT_TRUE(config.ecus.empty());
}

} // namespace tests
} // namespace cgw_diag
