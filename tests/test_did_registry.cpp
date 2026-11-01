#include <gtest/gtest.h>
#include "did_registry.h"
#include <fstream>
#include <filesystem>

namespace ccu_diag {
namespace tests {

namespace fs = std::filesystem;

class DidRegistryTest : public ::testing::Test {
protected:
    fs::path temp_dir_;

    void SetUp() override {
        temp_dir_ = fs::temp_directory_path() / "ccu_diag_did_registry_test";
        fs::create_directories(temp_dir_);
        createTestConfig();
        registry = std::make_unique<DidRegistry>(configPath());
    }
    
    void TearDown() override {
        registry.reset();
        fs::remove_all(temp_dir_);
    }
    
    fs::path configPath() const { return temp_dir_ / "test_did_registry.yaml"; }
    
    void writeFile(const fs::path& path, const std::string& content) {
        std::ofstream file(path);
        file << content;
        file.close();
    }

    void createTestConfig() {
        writeFile(configPath(), R"(
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
    someipService: "0x1110"  # CCU-DIAG-DSN-CR-007
    fields: ["swVersion", "hwVersion"]
)");
    }
    
    std::unique_ptr<DidRegistry> registry;
};

TEST_F(DidRegistryTest, GetRegistryVersion) {
    EXPECT_EQ(registry->getRegistryVersion(), "2026.07.0");
}

TEST_F(DidRegistryTest, GetBaselineDids) {
    const auto& baseline_dids = registry->getBaselineDids();
    ASSERT_EQ(baseline_dids.size(), 4);
    EXPECT_EQ(baseline_dids[0], 0xF195);
    EXPECT_EQ(baseline_dids[1], 0xF188);
    EXPECT_EQ(baseline_dids[2], 0xF187);
    EXPECT_EQ(baseline_dids[3], 0xF193);
}

TEST_F(DidRegistryTest, FindEcuById) {
    auto bms = registry->findEcuById("BMS");
    ASSERT_TRUE(bms.has_value());
    EXPECT_EQ(bms->ecu_id, "BMS");
    EXPECT_EQ(bms->logical_address, 0x1A);
    EXPECT_EQ(bms->transport, TransportType::DOCAN);
    EXPECT_EQ(bms->read_method, ReadMethod::UDS_0x22);
    
    auto idcm = registry->findEcuById("IDCM");
    ASSERT_TRUE(idcm.has_value());
    EXPECT_EQ(idcm->ecu_id, "IDCM");
    EXPECT_EQ(idcm->logical_address, 0x20);
    EXPECT_EQ(idcm->transport, TransportType::DOIP);
    EXPECT_EQ(idcm->read_method, ReadMethod::SOMEIP_GET_VERSION);
    
    auto unknown = registry->findEcuById("UNKNOWN");
    EXPECT_FALSE(unknown.has_value());
}

TEST_F(DidRegistryTest, FindEcuByLogicalAddress) {
    auto bms = registry->findEcuByLogicalAddress(0x1A);
    ASSERT_TRUE(bms.has_value());
    EXPECT_EQ(bms->ecu_id, "BMS");
    
    auto idcm = registry->findEcuByLogicalAddress(0x20);
    ASSERT_TRUE(idcm.has_value());
    EXPECT_EQ(idcm->ecu_id, "IDCM");
    
    auto unknown = registry->findEcuByLogicalAddress(0xFF);
    EXPECT_FALSE(unknown.has_value());
}

TEST_F(DidRegistryTest, GetAllEcus) {
    const auto& ecus = registry->getAllEcus();
    EXPECT_EQ(ecus.size(), 2);
}

TEST_F(DidRegistryTest, IsEcuRegistered) {
    EXPECT_TRUE(registry->isEcuRegistered("BMS"));
    EXPECT_TRUE(registry->isEcuRegistered("IDCM"));
    EXPECT_FALSE(registry->isEcuRegistered("UNKNOWN"));
}

TEST_F(DidRegistryTest, GetDidsForEcu) {
    auto bms_dids = registry->getDidsForEcu("BMS");
    ASSERT_EQ(bms_dids.size(), 2);
    EXPECT_EQ(bms_dids[0].did, 0xF195);
    EXPECT_EQ(bms_dids[1].did, 0xF187);
    
    auto idcm_dids = registry->getDidsForEcu("IDCM");
    EXPECT_EQ(idcm_dids.size(), 0);  // SOME/IP method has no DID entries
    
    auto unknown_dids = registry->getDidsForEcu("UNKNOWN");
    EXPECT_EQ(unknown_dids.size(), 0);
}

TEST_F(DidRegistryTest, Reload) {
    // Modify the config file
    writeFile(configPath(), R"(
registryVersion: "2026.07.1"
baselineDids: [0xF195]
ecus:
  - ecuId: "NEW_ECU"
    logicalAddress: 0x30
    transport: "DoCAN"
    readMethod: "UDS_0x22"
    securityRequired: false
    dids:
      - did: 0xF195
        field: "swVersion"
        encoding: "ASCII"
        length: 16
)");

    registry->reload();

    EXPECT_EQ(registry->getRegistryVersion(), "2026.07.1");
    EXPECT_EQ(registry->getBaselineDids().size(), 1);
    EXPECT_TRUE(registry->isEcuRegistered("NEW_ECU"));
    EXPECT_FALSE(registry->isEcuRegistered("BMS"));
}

TEST_F(DidRegistryTest, ConstructorInvalidPath) {
    EXPECT_THROW(DidRegistry("nonexistent.yaml"), std::runtime_error);
}

TEST_F(DidRegistryTest, ReloadMalformedYaml) {
    writeFile(configPath(), "invalid: [yaml: {broken");
    EXPECT_THROW(registry->reload(), std::runtime_error);
}

TEST_F(DidRegistryTest, ReloadDuplicateEcuId) {
    writeFile(configPath(), R"(
registryVersion: "2026.07.0"
baselineDids: []
ecus:
  - ecuId: "BMS"
    logicalAddress: 0x1A
    transport: "DoCAN"
    readMethod: "UDS_0x22"
  - ecuId: "BMS"
    logicalAddress: 0x20
    transport: "DoCAN"
    readMethod: "UDS_0x22"
)");
    EXPECT_THROW(registry->reload(), std::runtime_error);
}

TEST_F(DidRegistryTest, ReloadDuplicateLogicalAddress) {
    writeFile(configPath(), R"(
registryVersion: "2026.07.0"
baselineDids: []
ecus:
  - ecuId: "BMS"
    logicalAddress: 0x1A
    transport: "DoCAN"
    readMethod: "UDS_0x22"
  - ecuId: "IDCM"
    logicalAddress: 0x1A
    transport: "DoCAN"
    readMethod: "UDS_0x22"
)");
    EXPECT_THROW(registry->reload(), std::runtime_error);
}

TEST_F(DidRegistryTest, ReloadPreservesStateOnFailure) {
    auto orig_version = registry->getRegistryVersion();
    auto orig_ecus = registry->getAllEcus().size();

    writeFile(configPath(), "invalid: [yaml: {broken");
    EXPECT_THROW(registry->reload(), std::runtime_error);

    EXPECT_EQ(registry->getRegistryVersion(), orig_version);
    EXPECT_EQ(registry->getAllEcus().size(), orig_ecus);
    EXPECT_TRUE(registry->isEcuRegistered("BMS"));
}

} // namespace tests
} // namespace ccu_diag
