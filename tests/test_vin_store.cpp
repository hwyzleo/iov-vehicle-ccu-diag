#include <gtest/gtest.h>
#include "vin_store.h"
#include <filesystem>
#include <fstream>

namespace ccu_diag {
namespace tests {

class VinStoreTest : public ::testing::Test {
protected:
    void SetUp() override {
        temp_dir_ = std::filesystem::temp_directory_path();
        storage_path_ = (temp_dir_ / "test_vin_store.dat").string();
        store_ = createVinStore(storage_path_);
    }

    void TearDown() override {
        std::filesystem::remove(storage_path_);
    }

    std::filesystem::path temp_dir_;
    std::string storage_path_;
    std::unique_ptr<VinStore> store_;
};

TEST_F(VinStoreTest, InitiallyEmpty) {
    EXPECT_FALSE(store_->isVinProvisioned());
    EXPECT_FALSE(store_->readVin().has_value());
}

TEST_F(VinStoreTest, WriteValidVin) {
    std::string valid_vin = "1234567890ABCDEFG";
    EXPECT_TRUE(store_->writeVin(valid_vin));
    EXPECT_TRUE(store_->isVinProvisioned());
    EXPECT_EQ(store_->readVin().value(), valid_vin);
}

TEST_F(VinStoreTest, WriteInvalidVinLength) {
    std::string invalid_vin = "1234567890";  // Too short
    EXPECT_FALSE(store_->writeVin(invalid_vin));
    EXPECT_FALSE(store_->isVinProvisioned());
}

TEST_F(VinStoreTest, WriteInvalidVinCharacters) {
    std::string invalid_vin = "1234567890ABCDEFI";  // Contains 'I' which is invalid
    EXPECT_FALSE(store_->writeVin(invalid_vin));
    EXPECT_FALSE(store_->isVinProvisioned());
}

TEST_F(VinStoreTest, WriteOnceSemantics) {
    std::string first_vin = "1234567890ABCDEFG";
    std::string second_vin = "9876543210EDCBAFG";

    EXPECT_TRUE(store_->writeVin(first_vin));
    EXPECT_FALSE(store_->writeVin(second_vin));  // Should fail due to write-once
    EXPECT_EQ(store_->readVin().value(), first_vin);
}

TEST_F(VinStoreTest, Persistence) {
    std::string vin = "1234567890ABCDEFG";
    store_->writeVin(vin);

    // Create new store instance to test persistence
    auto new_store = createVinStore(storage_path_);
    EXPECT_TRUE(new_store->isVinProvisioned());
    EXPECT_EQ(new_store->readVin().value(), vin);
}

TEST_F(VinStoreTest, VinValidation) {
    EXPECT_TRUE(VinStore::isValidVin("1234567890ABCDEFG"));
    EXPECT_FALSE(VinStore::isValidVin("1234567890"));  // Too short
    EXPECT_FALSE(VinStore::isValidVin("1234567890ABCDEFI"));  // Invalid char 'I'
}

TEST_F(VinStoreTest, ResetVin) {
    std::string vin = "1234567890ABCDEFG";
    EXPECT_TRUE(store_->writeVin(vin));
    EXPECT_TRUE(store_->isVinProvisioned());
    EXPECT_EQ(store_->readVin().value(), vin);

    // Reset should fail without authorization
    EXPECT_FALSE(store_->resetVin());
    EXPECT_TRUE(store_->isVinProvisioned());

    // Enable rewrite authorization
    store_->setAllowRewrite(true);
    EXPECT_TRUE(store_->resetVin());
    EXPECT_FALSE(store_->isVinProvisioned());
    EXPECT_FALSE(store_->readVin().has_value());
}

TEST_F(VinStoreTest, WriteAfterReset) {
    std::string first_vin = "1234567890ABCDEFG";
    std::string second_vin = "9876543210EDCBAFG";

    EXPECT_TRUE(store_->writeVin(first_vin));
    EXPECT_TRUE(store_->isVinProvisioned());

    // Enable rewrite authorization and reset
    store_->setAllowRewrite(true);
    EXPECT_TRUE(store_->resetVin());
    EXPECT_FALSE(store_->isVinProvisioned());

    // Write new VIN after reset should succeed
    EXPECT_TRUE(store_->writeVin(second_vin));
    EXPECT_TRUE(store_->isVinProvisioned());
    EXPECT_EQ(store_->readVin().value(), second_vin);
}

TEST_F(VinStoreTest, ResetVinUnauthorized) {
    std::string vin = "1234567890ABCDEFG";
    EXPECT_TRUE(store_->writeVin(vin));

    // Reset should fail without setting allow_rewrite
    EXPECT_FALSE(store_->resetVin());
    EXPECT_TRUE(store_->isVinProvisioned());
    EXPECT_EQ(store_->readVin().value(), vin);
}

} // namespace tests
} // namespace ccu_diag
