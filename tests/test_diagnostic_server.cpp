#include <gtest/gtest.h>
#include "diagnostic_server.h"
#include "vin_store.h"
#include <filesystem>

namespace cgw_diag {
namespace tests {

class DiagnosticServerTest : public ::testing::Test {
protected:
    void SetUp() override {
        temp_dir_ = std::filesystem::temp_directory_path();
        storage_path_ = (temp_dir_ / "test_diag_server_vin.dat").string();
        vin_store_ = createVinStore(storage_path_);
        server_ = std::make_unique<DiagnosticServer>(vin_store_);
    }
    
    void TearDown() override {
        std::filesystem::remove(storage_path_);
    }
    
    std::filesystem::path temp_dir_;
    std::string storage_path_;
    std::shared_ptr<VinStore> vin_store_;
    std::unique_ptr<DiagnosticServer> server_;
};

TEST_F(DiagnosticServerTest, InitiallyLocked) {
    EXPECT_EQ(server_->getSecurityLevel(), SecurityLevel::LOCKED);
    EXPECT_FALSE(server_->isVinWriteSessionActive());
}

TEST_F(DiagnosticServerTest, SecurityAccessRequestSeed) {
    std::vector<uint8_t> request;
    std::vector<uint8_t> response;
    
    auto result = server_->handleSecurityAccess(0x01, request, response);
    EXPECT_EQ(result, ErrorCode::SUCCESS);
    EXPECT_EQ(response[0], 0x67);  // Positive response
    EXPECT_EQ(response[1], 0x01);  // Sub-function
}

TEST_F(DiagnosticServerTest, SecurityAccessSendKey) {
    std::vector<uint8_t> request = {0x12, 0x34, 0x56, 0x78};
    std::vector<uint8_t> response;
    
    auto result = server_->handleSecurityAccess(0x02, request, response);
    EXPECT_EQ(result, ErrorCode::SUCCESS);
    EXPECT_EQ(server_->getSecurityLevel(), SecurityLevel::LEVEL_1);
    EXPECT_TRUE(server_->isVinWriteSessionActive());
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
    // First, perform security access
    std::vector<uint8_t> seed_request;
    std::vector<uint8_t> seed_response;
    server_->handleSecurityAccess(0x01, seed_request, seed_response);
    
    std::vector<uint8_t> key_request = {0x12, 0x34, 0x56, 0x78};
    std::vector<uint8_t> key_response;
    server_->handleSecurityAccess(0x02, key_request, key_response);
    
    // Now write VIN
    std::vector<uint8_t> vin_data = {'1', '2', '3', '4', '5', '6', '7', '8', '9', '0', 
                                      'A', 'B', 'C', 'D', 'E', 'F', 'G'};
    std::vector<uint8_t> response;
    
    auto result = server_->handleWriteDataByIdentifier(DID_VIN, vin_data, response);
    EXPECT_EQ(result, ErrorCode::SUCCESS);
    EXPECT_TRUE(vin_store_->isVinProvisioned());
    EXPECT_EQ(vin_store_->readVin().value(), "1234567890ABCDEFG");
}

TEST_F(DiagnosticServerTest, WriteInvalidVinLength) {
    // Perform security access
    std::vector<uint8_t> seed_request;
    std::vector<uint8_t> seed_response;
    server_->handleSecurityAccess(0x01, seed_request, seed_response);
    
    std::vector<uint8_t> key_request = {0x12, 0x34, 0x56, 0x78};
    std::vector<uint8_t> key_response;
    server_->handleSecurityAccess(0x02, key_request, key_response);
    
    // Try to write invalid VIN (too short)
    std::vector<uint8_t> invalid_vin = {'1', '2', '3', '4', '5'};
    std::vector<uint8_t> response;
    
    auto result = server_->handleWriteDataByIdentifier(DID_VIN, invalid_vin, response);
    EXPECT_EQ(result, ErrorCode::VIN_WRITE_REJECTED);
    EXPECT_FALSE(vin_store_->isVinProvisioned());
}

TEST_F(DiagnosticServerTest, ResetSession) {
    // Perform security access
    std::vector<uint8_t> seed_request;
    std::vector<uint8_t> seed_response;
    server_->handleSecurityAccess(0x01, seed_request, seed_response);
    
    std::vector<uint8_t> key_request = {0x12, 0x34, 0x56, 0x78};
    std::vector<uint8_t> key_response;
    server_->handleSecurityAccess(0x02, key_request, key_response);
    
    EXPECT_TRUE(server_->isVinWriteSessionActive());
    
    // Reset session
    server_->resetSession();
    
    EXPECT_FALSE(server_->isVinWriteSessionActive());
    EXPECT_EQ(server_->getSecurityLevel(), SecurityLevel::LOCKED);
}

} // namespace tests
} // namespace cgw_diag
