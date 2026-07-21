#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include "vin_store.h"
#include "diagnostic_server.h"
#include "version_collector.h"
#include "fota_facade.h"
#include "did_registry.h"
#include "diagnostic_router.h"
#include "uds_client.h"
#include "mock_transport.h"
#include "cgw_sec_client_proxy.h"
#include <filesystem>
#include <fstream>
#include <memory>

namespace cgw_diag {
namespace tests {

using ::testing::Return;
using ::testing::_;

// Mock CGW-SEC client proxy for testing
class MockCgwSecClientProxy : public CgwSecClientProxy {
public:
    MOCK_METHOD(bool, initialize, (const SomeIpServiceConfig&), (override));
    MOCK_METHOD(bool, connect, (), (override));
    MOCK_METHOD(void, disconnect, (), (override));
    MOCK_METHOD(bool, isConnected, (), (const, override));
    MOCK_METHOD(std::optional<SecuritySeed>, getSeed, (SecLevel), (override));
    MOCK_METHOD(SecResult, verifyKey, (SecLevel, uint32_t, const std::vector<uint8_t>&), (override));
    MOCK_METHOD(void, setRequestTimeout, (uint32_t), (override));
    MOCK_METHOD(uint32_t, getRequestTimeout, (), (const, override));
};

class VinIntegrationTest : public ::testing::Test {
protected:
    void SetUp() override {
        temp_dir_ = std::filesystem::temp_directory_path();
        
        config_path_ = (temp_dir_ / "test_vin_integration_config.yaml").string();
        vin_store_path_ = (temp_dir_ / "test_vin_integration_vin.dat").string();
        
        createTestConfig();
        
        registry = std::make_shared<DidRegistry>(config_path_);
        vin_store = std::shared_ptr<VinStore>(createVinStore(vin_store_path_).release());
        
        mock_transport = std::make_shared<MockTransport>();
        mock_transport->setTransportType("DoCAN");
        
        mock_doip_transport = std::make_shared<MockTransport>();
        mock_doip_transport->setTransportType("DoIP");
        
        DiagConfig diag_config;
        diag_config.max_retry_count = 2;
        uds_client = std::make_shared<UdsClient>(mock_transport, diag_config);
        
        router = std::make_shared<DiagnosticRouter>(registry, uds_client,
                                                      mock_transport, mock_doip_transport);
        
        collector = std::make_shared<VersionCollector>(registry, router,
                                                        mock_transport, mock_doip_transport,
                                                        vin_store);
        
        facade = std::make_shared<FotaFacade>(collector, registry, vin_store);
        mock_sec_proxy = std::make_shared<MockCgwSecClientProxy>();
        diag_server = std::make_shared<DiagnosticServer>(vin_store, mock_sec_proxy);
    }
    
    void TearDown() override {
        std::filesystem::remove(config_path_);
        std::filesystem::remove(vin_store_path_);
    }
    
    void createTestConfig() {
        std::ofstream file(config_path_);
        file << R"(
registryVersion: "2026.07.1"
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
vinConfig:
  storagePath: "test_vin_integration_vin.dat"
  writeOnce: true
  securityLevel: 1
  allowedDids: [0xF190]
)";
        file.close();
    }
    
    std::filesystem::path temp_dir_;
    std::string config_path_;
    std::string vin_store_path_;
    std::shared_ptr<DidRegistry> registry;
    std::shared_ptr<VinStore> vin_store;
    std::shared_ptr<MockTransport> mock_transport;
    std::shared_ptr<MockTransport> mock_doip_transport;
    std::shared_ptr<UdsClient> uds_client;
    std::shared_ptr<DiagnosticRouter> router;
    std::shared_ptr<VersionCollector> collector;
    std::shared_ptr<FotaFacade> facade;
    std::shared_ptr<MockCgwSecClientProxy> mock_sec_proxy;
    std::shared_ptr<DiagnosticServer> diag_server;
};

TEST_F(VinIntegrationTest, FullVinProvisioningFlow) {
    // Initially no VIN
    EXPECT_FALSE(vin_store->isVinProvisioned());
    EXPECT_FALSE(facade->readVin().has_value());
    
    // Perform security access with mock
    SecuritySeed seed;
    seed.seed_id = 12345;
    seed.seed = {0x01, 0x02, 0x03, 0x04};
    
    EXPECT_CALL(*mock_sec_proxy, getSeed(SecLevel::LEVEL_1))
        .WillOnce(Return(seed));
    
    std::vector<uint8_t> seed_request;
    std::vector<uint8_t> seed_response;
    diag_server->handleSecurityAccess(0x01, seed_request, seed_response);
    
    std::vector<uint8_t> key = {0x05, 0x06, 0x07, 0x08};
    EXPECT_CALL(*mock_sec_proxy, verifyKey(SecLevel::LEVEL_1, 12345, key))
        .WillOnce(Return(SecResult::SUCCESS));
    
    std::vector<uint8_t> key_request = {0x02};
    key_request.insert(key_request.end(), key.begin(), key.end());
    std::vector<uint8_t> key_response;
    diag_server->handleSecurityAccess(0x02, key_request, key_response);
    
    // Write VIN
    std::vector<uint8_t> vin_data = {'1', '2', '3', '4', '5', '6', '7', '8', '9', '0', 
                                      'A', 'B', 'C', 'D', 'E', 'F', 'G'};
    std::vector<uint8_t> write_response;
    
    auto result = diag_server->handleWriteDataByIdentifier(DID_VIN, vin_data, write_response);
    EXPECT_EQ(result, ErrorCode::SUCCESS);
    
    // Verify VIN is stored
    EXPECT_TRUE(vin_store->isVinProvisioned());
    EXPECT_EQ(vin_store->readVin().value(), "1234567890ABCDEFG");
    
    // Verify VIN is available through facade
    auto vin = facade->readVin();
    EXPECT_TRUE(vin.has_value());
    EXPECT_EQ(vin.value(), "1234567890ABCDEFG");
    
    // Setup mock for version collection
    mock_transport->setRequestHandler([](uint16_t, const std::vector<uint8_t>& request) {
        TransportResponse resp;
        resp.success = true;
        resp.data = {0x62, request[1], request[2], '1', '.', '0', '.', '0', '\0'};
        return resp;
    });
    
    // Collect vehicle inventory
    auto snapshot = facade->collectVehicleInventory();
    
    // Verify VIN is included in snapshot
    EXPECT_EQ(snapshot.vin, "1234567890ABCDEFG");
    EXPECT_EQ(snapshot.vin_source, VinSource::PROVISIONED);
    EXPECT_EQ(snapshot.overall_result, CollectionStatus::ALL_OK);
}

TEST_F(VinIntegrationTest, VinNotProvisioned) {
    // Collect inventory without VIN
    mock_transport->setRequestHandler([](uint16_t, const std::vector<uint8_t>& request) {
        TransportResponse resp;
        resp.success = true;
        resp.data = {0x62, request[1], request[2], '1', '.', '0', '.', '0', '\0'};
        return resp;
    });
    
    auto snapshot = facade->collectVehicleInventory();
    
    // Verify VIN is empty and source is unknown
    EXPECT_TRUE(snapshot.vin.empty());
    EXPECT_EQ(snapshot.vin_source, VinSource::UNKNOWN);
}

TEST_F(VinIntegrationTest, EndToEndVinProvisioningWithCgwSec) {
    // This test verifies the complete VIN provisioning flow
    // with CGW-SEC integration
    
    // Setup mock CGW-SEC responses
    SecuritySeed seed;
    seed.seed_id = 12345;
    seed.seed = {0x12, 0x34, 0x56, 0x78};
    
    EXPECT_CALL(*mock_sec_proxy, getSeed(SecLevel::LEVEL_1))
        .WillOnce(Return(seed));
    
    std::vector<uint8_t> valid_key = {0x9A, 0xBC, 0xDE, 0xF0};
    EXPECT_CALL(*mock_sec_proxy, verifyKey(SecLevel::LEVEL_1, 12345, valid_key))
        .WillOnce(Return(SecResult::SUCCESS));
    
    // Step 1: Request seed
    std::vector<uint8_t> seed_request = {0x01};
    std::vector<uint8_t> seed_response;
    auto result = diag_server->handleSecurityAccess(0x01, seed_request, seed_response);
    EXPECT_EQ(result, ErrorCode::SUCCESS);
    
    // Step 2: Send key
    std::vector<uint8_t> key_request = {0x02};
    key_request.insert(key_request.end(), valid_key.begin(), valid_key.end());
    std::vector<uint8_t> key_response;
    result = diag_server->handleSecurityAccess(0x02, key_request, key_response);
    EXPECT_EQ(result, ErrorCode::SUCCESS);
    
    // Step 3: Write VIN
    std::string test_vin = "1HGBH41JXMN109186";
    std::vector<uint8_t> vin_data(test_vin.begin(), test_vin.end());
    std::vector<uint8_t> write_response;
    
    result = diag_server->handleWriteDataByIdentifier(0xF190, vin_data, write_response);
    EXPECT_EQ(result, ErrorCode::SUCCESS);
    
    // Verify VIN was written
    auto stored_vin = vin_store->readVin();
    EXPECT_TRUE(stored_vin.has_value());
    EXPECT_EQ(stored_vin.value(), test_vin);
}

} // namespace tests
} // namespace cgw_diag
