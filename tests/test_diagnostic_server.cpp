#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include "diagnostic_server.h"
#include "vin_store.h"
#include "ccu_sec_client_proxy.h"
#include <filesystem>

namespace ccu_diag {
namespace tests {

using ::testing::Return;
using ::testing::_;

// Mock CCU-SEC client proxy for testing
class MockCcuSecClientProxy : public CcuSecClientProxy {
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

class DiagnosticServerTest : public ::testing::Test {
protected:
    void SetUp() override {
        temp_dir_ = std::filesystem::temp_directory_path();
        storage_path_ = (temp_dir_ / "test_diag_server_vin.dat").string();
        vin_store_ = createVinStore(storage_path_);
        mock_sec_proxy_ = std::make_shared<MockCcuSecClientProxy>();
        server_ = std::make_unique<DiagnosticServer>(vin_store_, mock_sec_proxy_);
    }
    
    void TearDown() override {
        std::filesystem::remove(storage_path_);
    }
    
    void performSecurityAccess() {
        SecuritySeed seed;
        seed.seed_id = 12345;
        seed.seed = {0x01, 0x02, 0x03, 0x04};
        
        EXPECT_CALL(*mock_sec_proxy_, getSeed(SecLevel::LEVEL_1))
            .WillOnce(Return(seed));
        
        std::vector<uint8_t> seed_request;
        std::vector<uint8_t> seed_response;
        server_->handleSecurityAccess(0x01, seed_request, seed_response);
        
        std::vector<uint8_t> key = {0x05, 0x06, 0x07, 0x08};
        EXPECT_CALL(*mock_sec_proxy_, verifyKey(SecLevel::LEVEL_1, 12345, key))
            .WillOnce(Return(SecResult::SUCCESS));
        
        std::vector<uint8_t> key_request = {0x02};
        key_request.insert(key_request.end(), key.begin(), key.end());
        std::vector<uint8_t> key_response;
        server_->handleSecurityAccess(0x02, key_request, key_response);
    }
    
    std::filesystem::path temp_dir_;
    std::string storage_path_;
    std::shared_ptr<VinStore> vin_store_;
    std::shared_ptr<MockCcuSecClientProxy> mock_sec_proxy_;
    std::unique_ptr<DiagnosticServer> server_;
};

TEST_F(DiagnosticServerTest, InitiallyLocked) {
    EXPECT_EQ(server_->getSecurityLevel(), SecurityLevel::LOCKED);
    EXPECT_FALSE(server_->isVinWriteSessionActive());
}

TEST_F(DiagnosticServerTest, SecurityAccessRequestSeedSuccess) {
    SecuritySeed expected_seed;
    expected_seed.seed_id = 12345;
    expected_seed.seed = {0x01, 0x02, 0x03, 0x04};
    
    EXPECT_CALL(*mock_sec_proxy_, getSeed(SecLevel::LEVEL_1))
        .WillOnce(Return(expected_seed));
    
    std::vector<uint8_t> request = {0x01};
    std::vector<uint8_t> response;
    
    auto result = server_->handleSecurityAccess(0x01, request, response);
    
    EXPECT_EQ(result, ErrorCode::SUCCESS);
    EXPECT_EQ(response[0], 0x67);  // Positive response
    EXPECT_EQ(response[1], 0x01);  // Sub-function
    EXPECT_EQ(response.size(), 6);  // 2 header + 4 seed bytes
}

TEST_F(DiagnosticServerTest, SecurityAccessRequestSeedFailure) {
    EXPECT_CALL(*mock_sec_proxy_, getSeed(SecLevel::LEVEL_1))
        .WillOnce(Return(std::nullopt));
    
    std::vector<uint8_t> request = {0x01};
    std::vector<uint8_t> response;
    
    auto result = server_->handleSecurityAccess(0x01, request, response);
    
    EXPECT_EQ(result, ErrorCode::VIN_WRITE_REJECTED);
}

TEST_F(DiagnosticServerTest, SecurityAccessVerifyKeySuccess) {
    // First request seed
    SecuritySeed seed;
    seed.seed_id = 12345;
    seed.seed = {0x01, 0x02, 0x03, 0x04};
    
    EXPECT_CALL(*mock_sec_proxy_, getSeed(SecLevel::LEVEL_1))
        .WillOnce(Return(seed));
    
    std::vector<uint8_t> seed_request = {0x01};
    std::vector<uint8_t> seed_response;
    server_->handleSecurityAccess(0x01, seed_request, seed_response);
    
    // Then verify key
    std::vector<uint8_t> key = {0x05, 0x06, 0x07, 0x08};
    EXPECT_CALL(*mock_sec_proxy_, verifyKey(SecLevel::LEVEL_1, 12345, key))
        .WillOnce(Return(SecResult::SUCCESS));
    
    std::vector<uint8_t> key_request = {0x02};
    key_request.insert(key_request.end(), key.begin(), key.end());
    std::vector<uint8_t> key_response;
    
    auto result = server_->handleSecurityAccess(0x02, key_request, key_response);
    
    EXPECT_EQ(result, ErrorCode::SUCCESS);
    EXPECT_EQ(key_response[0], 0x67);  // Positive response
    EXPECT_EQ(key_response[1], 0x02);  // Sub-function
    EXPECT_TRUE(server_->isVinWriteSessionActive());
}

TEST_F(DiagnosticServerTest, SecurityAccessVerifyKeyInvalid) {
    // First request seed
    SecuritySeed seed;
    seed.seed_id = 12345;
    seed.seed = {0x01, 0x02, 0x03, 0x04};
    
    EXPECT_CALL(*mock_sec_proxy_, getSeed(SecLevel::LEVEL_1))
        .WillOnce(Return(seed));
    
    std::vector<uint8_t> seed_request = {0x01};
    std::vector<uint8_t> seed_response;
    server_->handleSecurityAccess(0x01, seed_request, seed_response);
    
    // Then verify with invalid key
    std::vector<uint8_t> invalid_key = {0xFF, 0xFF, 0xFF, 0xFF};
    EXPECT_CALL(*mock_sec_proxy_, verifyKey(SecLevel::LEVEL_1, 12345, invalid_key))
        .WillOnce(Return(SecResult::INVALID_KEY));
    
    std::vector<uint8_t> key_request = {0x02};
    key_request.insert(key_request.end(), invalid_key.begin(), invalid_key.end());
    std::vector<uint8_t> key_response;
    
    auto result = server_->handleSecurityAccess(0x02, key_request, key_response);
    
    EXPECT_EQ(result, ErrorCode::INVALID_KEY);
    EXPECT_FALSE(server_->isVinWriteSessionActive());
}

TEST_F(DiagnosticServerTest, SecurityAccessLockoutAfterMaxAttempts) {
    SecuritySeed seed;
    seed.seed_id = 12345;
    seed.seed = {0x01, 0x02, 0x03, 0x04};
    
    // Simulate 3 failed attempts
    for (int i = 0; i < 3; i++) {
        EXPECT_CALL(*mock_sec_proxy_, getSeed(SecLevel::LEVEL_1))
            .WillOnce(Return(seed));
        
        std::vector<uint8_t> seed_request = {0x01};
        std::vector<uint8_t> seed_response;
        server_->handleSecurityAccess(0x01, seed_request, seed_response);
        
        std::vector<uint8_t> invalid_key = {0xFF, 0xFF, 0xFF, 0xFF};
        EXPECT_CALL(*mock_sec_proxy_, verifyKey(SecLevel::LEVEL_1, 12345, invalid_key))
            .WillOnce(Return(SecResult::INVALID_KEY));
        
        std::vector<uint8_t> key_request = {0x02};
        key_request.insert(key_request.end(), invalid_key.begin(), invalid_key.end());
        std::vector<uint8_t> key_response;
        
        server_->handleSecurityAccess(0x02, key_request, key_response);
    }
    
    // Should be locked out now
    EXPECT_TRUE(server_->isLockedOut());
    
    // Next attempt should fail immediately
    std::vector<uint8_t> request = {0x01};
    std::vector<uint8_t> response;
    auto result = server_->handleSecurityAccess(0x01, request, response);
    
    EXPECT_EQ(result, ErrorCode::VIN_WRITE_REJECTED);
}

TEST_F(DiagnosticServerTest, WriteVinWithoutSecurity) {
    std::vector<uint8_t> vin_data = {'1', '2', '3', '4', '5', '6', '7', '8', '9', '0', 
                                      'A', 'B', 'C', 'D', 'E', 'F', 'G'};
    std::vector<uint8_t> response;
    
    auto result = server_->handleWriteDataByIdentifier(DID_VIN, vin_data, response);
    EXPECT_EQ(result, ErrorCode::VIN_WRITE_REJECTED);
    EXPECT_FALSE(vin_store_->isVinProvisioned());
}

TEST_F(DiagnosticServerTest, WriteVinWithSecurity) {
    performSecurityAccess();
    
    std::vector<uint8_t> vin_data = {'1', '2', '3', '4', '5', '6', '7', '8', '9', '0', 
                                      'A', 'B', 'C', 'D', 'E', 'F', 'G'};
    std::vector<uint8_t> response;
    
    auto result = server_->handleWriteDataByIdentifier(DID_VIN, vin_data, response);
    EXPECT_EQ(result, ErrorCode::SUCCESS);
    EXPECT_TRUE(vin_store_->isVinProvisioned());
    EXPECT_EQ(vin_store_->readVin().value(), "1234567890ABCDEFG");
}

TEST_F(DiagnosticServerTest, WriteInvalidVinLength) {
    performSecurityAccess();
    
    std::vector<uint8_t> invalid_vin = {'1', '2', '3', '4', '5'};
    std::vector<uint8_t> response;
    
    auto result = server_->handleWriteDataByIdentifier(DID_VIN, invalid_vin, response);
    EXPECT_EQ(result, ErrorCode::VIN_WRITE_REJECTED);
    EXPECT_FALSE(vin_store_->isVinProvisioned());
}

TEST_F(DiagnosticServerTest, WriteNonVinDid) {
    performSecurityAccess();
    
    std::vector<uint8_t> data = {0x01, 0x02, 0x03};
    std::vector<uint8_t> response;
    
    auto result = server_->handleWriteDataByIdentifier(0x0000, data, response);
    EXPECT_EQ(result, ErrorCode::VIN_WRITE_REJECTED);
    
    result = server_->handleWriteDataByIdentifier(0xF191, data, response);
    EXPECT_EQ(result, ErrorCode::VIN_WRITE_REJECTED);
}

TEST_F(DiagnosticServerTest, InvalidSubFunction) {
    std::vector<uint8_t> request;
    std::vector<uint8_t> response;
    
    auto result = server_->handleSecurityAccess(0x00, request, response);
    EXPECT_EQ(result, ErrorCode::VIN_WRITE_REJECTED);
    EXPECT_EQ(server_->getSecurityLevel(), SecurityLevel::LOCKED);
    
    result = server_->handleSecurityAccess(0x03, request, response);
    EXPECT_EQ(result, ErrorCode::VIN_WRITE_REJECTED);
    EXPECT_EQ(server_->getSecurityLevel(), SecurityLevel::LOCKED);
    
    result = server_->handleSecurityAccess(0xFF, request, response);
    EXPECT_EQ(result, ErrorCode::VIN_WRITE_REJECTED);
    EXPECT_EQ(server_->getSecurityLevel(), SecurityLevel::LOCKED);
}

TEST_F(DiagnosticServerTest, ResetSession) {
    performSecurityAccess();
    
    EXPECT_TRUE(server_->isVinWriteSessionActive());
    
    server_->resetSession();
    
    EXPECT_FALSE(server_->isVinWriteSessionActive());
    EXPECT_EQ(server_->getSecurityLevel(), SecurityLevel::LOCKED);
}

} // namespace tests
} // namespace ccu_diag
