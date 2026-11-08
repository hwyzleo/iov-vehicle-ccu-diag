// =============================================================================
// tests/test_ecu_inventory_store.cpp
// Inventory record / seed / atomic write / fail-closed
// (CCU-DIAG-DSN-CR-010 §4.1 / D6 / D9 / US-012)
// =============================================================================

#include <gtest/gtest.h>

#include "constants.h"
#include "ecu_inventory_store.h"

#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

#include <unistd.h>

namespace ccu_diag {
namespace tests {

namespace fs = std::filesystem;

namespace {

std::string seedJson(const std::string& bms_version = "1.0.0",
                     const std::string& vcu_version = "1.0.0") {
    return R"({
      "schemaVersion": 1,
      "registryVersion": "2026.07.1",
      "baselineCode": "BASE-001",
      "ecus": [
        {"ecuId":"BMS","partNumber":"PN-BMS","swVersion":")" + bms_version + R"(","hwVersion":"HW-BMS"},
        {"ecuId":"VCU","partNumber":"PN-VCU","swVersion":")" + vcu_version + R"(","hwVersion":"HW-VCU"}
      ]
    })";
}

void writeText(const fs::path& path, const std::string& text) {
    fs::create_directories(path.parent_path());
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    file << text;
}

std::string readText(const fs::path& path) {
    std::ifstream file(path, std::ios::binary);
    std::ostringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

class FailingAdapter : public InventoryFileAdapter {
public:
    enum class Step { NONE, WRITE, RENAME, DIR_FSYNC };
    mutable Step fail = Step::NONE;

    bool writeFileFsync(const std::string& path, const std::string& data,
                        std::string& error) const override {
        if (fail == Step::WRITE) {
            error = "injected write failure";
            return false;
        }
        return InventoryFileAdapter::writeFileFsync(path, data, error);
    }

    bool renameFile(const std::string& from, const std::string& to,
                    std::string& error) const override {
        if (fail == Step::RENAME) {
            error = "injected rename failure";
            return false;
        }
        return InventoryFileAdapter::renameFile(from, to, error);
    }

    bool fsyncDirectory(const std::string& dir, std::string& error) const override {
        if (fail == Step::DIR_FSYNC) {
            error = "injected dir fsync failure";
            return false;
        }
        return InventoryFileAdapter::fsyncDirectory(dir, error);
    }
};

} // namespace

class EcuInventoryStoreTest : public ::testing::Test {
protected:
    void SetUp() override {
        const auto* info = ::testing::UnitTest::GetInstance()->current_test_info();
        temp_dir_ = fs::temp_directory_path() /
                    ("ccu_diag_inv_" + std::to_string(::getpid()) + "_" +
                     info->name());
        fs::remove_all(temp_dir_);
        fs::create_directories(temp_dir_);
        seed_path_ = (temp_dir_ / "seed.json").string();
        state_path_ = (temp_dir_ / "state" / "ecu_inventory.json").string();
    }

    void TearDown() override {
        fs::remove_all(temp_dir_);
    }

    InventoryConfig seedConfig(uint32_t window = 64) const {
        InventoryConfig config;
        config.mode = InventoryMode::SEED;
        config.seed_path = seed_path_;
        config.state_path = state_path_;
        config.idempotency_window = window;
        return config;
    }

    InventoryConfig hardwareConfig(uint32_t window = 64) const {
        InventoryConfig config;
        config.mode = InventoryMode::HARDWARE;
        config.seed_path = seed_path_;
        config.state_path = state_path_;
        config.idempotency_window = window;
        return config;
    }

    std::shared_ptr<EcuInventoryStore> makeStore(
        const InventoryConfig& config,
        const std::vector<std::string>& ecus = {"BMS", "VCU"},
        std::shared_ptr<InventoryFileAdapter> adapter = nullptr) {
        return std::make_shared<EcuInventoryStore>(config, ecus,
                                                   std::move(adapter), false);
    }

    fs::path temp_dir_;
    std::string seed_path_;
    std::string state_path_;
};

TEST_F(EcuInventoryStoreTest, SeedImportedOnceAndNotReimportedOnRestart) {
    writeText(seed_path_, seedJson("1.0.0"));
    auto store = makeStore(seedConfig());
    const auto result = store->initialize("2026.07.1");
    ASSERT_TRUE(result.ok()) << result.detail;
    EXPECT_TRUE(store->available());
    EXPECT_EQ(store->record().inventory_source, "seed");
    ASSERT_EQ(store->record().ecus.size(), 2u);
    EXPECT_EQ(store->record().ecus[0].sw_version, "1.0.0");
    EXPECT_EQ(store->record().seed_digest.rfind("sha256:", 0), 0u);
    EXPECT_EQ(store->record().baseline_code, "BASE-001");
    EXPECT_TRUE(fs::exists(state_path_));

    // Changing the seed must NOT overwrite an existing record.
    writeText(seed_path_, seedJson("9.9.9"));
    auto restarted = makeStore(seedConfig());
    ASSERT_TRUE(restarted->initialize("2026.07.1").ok());
    EXPECT_EQ(restarted->record().ecus[0].sw_version, "1.0.0");
}

TEST_F(EcuInventoryStoreTest, SeedMissingFailsClosed) {
    auto store = makeStore(seedConfig());
    const auto result = store->initialize("2026.07.1");
    EXPECT_EQ(result.status, InventoryStoreStatus::SEED_INVALID);
    EXPECT_EQ(result.diagCode(), "CCU-DIAG-1013");
    EXPECT_FALSE(store->available());
    EXPECT_FALSE(fs::exists(state_path_));
}

TEST_F(EcuInventoryStoreTest, SeedValidationMatrix) {
    const std::vector<std::string> invalid_seeds = {
        "not json",
        R"({"schemaVersion":2,"registryVersion":"v","ecus":[{"ecuId":"BMS"}]})",
        R"({"schemaVersion":1,"registryVersion":"v","ecus":[]})",
        R"({"schemaVersion":1,"registryVersion":"v","ecus":[{"ecuId":"UNKNOWN"}]})",
        R"({"schemaVersion":1,"registryVersion":"v",
            "ecus":[{"ecuId":"BMS"},{"ecuId":"BMS"}]})",
        R"({"schemaVersion":1,"registryVersion":"v","ecus":[{"ecuId":""}]})",
        R"({"schemaVersion":1,"ecus":[{"ecuId":"BMS"}]})",
    };

    for (const auto& seed : invalid_seeds) {
        writeText(seed_path_, seed);
        fs::remove_all(fs::path(state_path_).parent_path());
        auto store = makeStore(seedConfig());
        const auto result = store->initialize("2026.07.1");
        EXPECT_EQ(result.status, InventoryStoreStatus::SEED_INVALID) << seed;
        EXPECT_FALSE(store->available()) << seed;
        EXPECT_FALSE(fs::exists(state_path_)) << seed;
    }
}

TEST_F(EcuInventoryStoreTest, HardwareModeWithoutRecordBuildsSkeleton) {
    auto store = makeStore(hardwareConfig(), {"BMS", "VCU"});
    const auto result = store->initialize("2026.07.1");
    ASSERT_TRUE(result.ok()) << result.detail;
    EXPECT_TRUE(store->available());
    EXPECT_EQ(store->record().inventory_source, "hardware");
    ASSERT_EQ(store->record().ecus.size(), 2u);
    EXPECT_TRUE(store->record().ecus[0].sw_version.empty());
    // Skeleton is in-memory only until a registration commits it.
    EXPECT_FALSE(fs::exists(state_path_));
}

TEST_F(EcuInventoryStoreTest, CorruptRecordFailsClosedWithoutRebuild) {
    writeText(seed_path_, seedJson());
    auto store = makeStore(seedConfig());
    ASSERT_TRUE(store->initialize("2026.07.1").ok());

    const std::string corrupted = "{this is not json";
    writeText(state_path_, corrupted);

    auto restarted = makeStore(seedConfig());
    const auto result = restarted->initialize("2026.07.1");
    EXPECT_EQ(result.status, InventoryStoreStatus::CORRUPT);
    EXPECT_EQ(result.diagCode(), "CCU-DIAG-1012");
    EXPECT_FALSE(restarted->available());
    // Never rebuilt / overwritten with a "looks fine" record.
    EXPECT_EQ(readText(state_path_), corrupted);
}

TEST_F(EcuInventoryStoreTest, ChecksumMismatchFailsClosed) {
    writeText(seed_path_, seedJson());
    auto store = makeStore(seedConfig());
    ASSERT_TRUE(store->initialize("2026.07.1").ok());

    auto file = nlohmann::json::parse(readText(state_path_));
    file["payload"]["ecus"][0]["swVersion"] = "6.6.6";
    writeText(state_path_, file.dump(2));

    auto restarted = makeStore(seedConfig());
    const auto result = restarted->initialize("2026.07.1");
    EXPECT_EQ(result.status, InventoryStoreStatus::CORRUPT);
    EXPECT_FALSE(restarted->available());
}

TEST_F(EcuInventoryStoreTest, UnknownRecordSchemaVersionFailsClosed) {
    writeText(seed_path_, seedJson());
    auto store = makeStore(seedConfig());
    ASSERT_TRUE(store->initialize("2026.07.1").ok());

    auto file = nlohmann::json::parse(readText(state_path_));
    file["schemaVersion"] = 2;
    // Keep a valid checksum so only the version gate can reject it.
    file["checksum"] = EcuInventoryStore::sha256Prefixed(
        file["payload"].dump());
    writeText(state_path_, file.dump(2));

    auto restarted = makeStore(seedConfig());
    const auto result = restarted->initialize("2026.07.1");
    EXPECT_EQ(result.status, InventoryStoreStatus::CORRUPT);
    EXPECT_NE(result.detail.find("schemaVersion"), std::string::npos);
}

TEST_F(EcuInventoryStoreTest, AtomicWriteFailureKeepsPreviousFile) {
    for (const auto step : {FailingAdapter::Step::WRITE,
                            FailingAdapter::Step::RENAME}) {
        fs::remove_all(temp_dir_);
        fs::create_directories(temp_dir_);
        writeText(seed_path_, seedJson());
        auto adapter = std::make_shared<FailingAdapter>();
        auto config = seedConfig();
        auto store = makeStore(config, {"BMS", "VCU"}, adapter);
        ASSERT_TRUE(store->initialize("2026.07.1").ok());

        const std::string before = readText(state_path_);
        EcuInventoryRecord changed = store->record();
        changed.ecus[0].sw_version = "8.8.8";
        adapter->fail = step;
        const auto result = store->persist(changed);
        EXPECT_EQ(result.status, InventoryStoreStatus::IO_ERROR);
        EXPECT_EQ(readText(state_path_), before);
        EXPECT_FALSE(fs::exists(state_path_ + ".tmp"));
        adapter->fail = FailingAdapter::Step::NONE;
    }
}

TEST_F(EcuInventoryStoreTest, DirectoryFsyncFailureReportsRetryableButKeepsNewContent) {
    writeText(seed_path_, seedJson());
    auto adapter = std::make_shared<FailingAdapter>();
    auto store = makeStore(seedConfig(), {"BMS", "VCU"}, adapter);
    ASSERT_TRUE(store->initialize("2026.07.1").ok());

    EcuInventoryRecord changed = store->record();
    changed.ecus[0].sw_version = "8.8.8";
    changed.updated_at = "2026-11-08T11:00:00Z";
    adapter->fail = FailingAdapter::Step::DIR_FSYNC;
    const auto result = store->persist(changed);
    EXPECT_EQ(result.status, InventoryStoreStatus::IO_ERROR);

    // rename already committed: disk and memory both hold the new record so a
    // retry is detected as DUPLICATE instead of applying twice.
    EXPECT_EQ(store->record().ecus[0].sw_version, "8.8.8");
    adapter->fail = FailingAdapter::Step::NONE;
    auto restarted = makeStore(seedConfig());
    ASSERT_TRUE(restarted->initialize("2026.07.1").ok());
    EXPECT_EQ(restarted->record().ecus[0].sw_version, "8.8.8");
}

TEST_F(EcuInventoryStoreTest, StaleTemporaryFileIsCleanedOnStartup) {
    writeText(seed_path_, seedJson());
    auto store = makeStore(seedConfig());
    ASSERT_TRUE(store->initialize("2026.07.1").ok());
    const std::string tmp = state_path_ + ".tmp";
    writeText(tmp, "partial garbage");

    auto restarted = makeStore(seedConfig());
    ASSERT_TRUE(restarted->initialize("2026.07.1").ok());
    EXPECT_FALSE(fs::exists(tmp));
    EXPECT_EQ(restarted->record().ecus[0].sw_version, "1.0.0");
}

TEST_F(EcuInventoryStoreTest, AppliedRingIsBoundedByWindow) {
    writeText(seed_path_, seedJson());
    auto store = makeStore(seedConfig(3), {"BMS", "VCU"});
    ASSERT_TRUE(store->initialize("2026.07.1").ok());

    EcuInventoryRecord record = store->record();
    for (int i = 1; i <= 5; ++i) {
        record.applied.push_back(AppliedRegistration{
            "EXE-" + std::to_string(i), 1, "REG-" + std::to_string(i),
            "2026-11-08T10:0" + std::to_string(i) + ":00Z", "sha256:x"});
    }
    ASSERT_TRUE(store->persist(record).ok());

    auto restarted = makeStore(seedConfig(3));
    ASSERT_TRUE(restarted->initialize("2026.07.1").ok());
    ASSERT_EQ(restarted->record().applied.size(), 3u);
    EXPECT_EQ(restarted->record().applied[0].execution_id, "EXE-3");
    EXPECT_EQ(restarted->record().applied[2].execution_id, "EXE-5");
}

TEST_F(EcuInventoryStoreTest, StatePathFailClosedRules) {
    EXPECT_TRUE(EcuInventoryStore::isValidStatePath(
        "/var/lib/ccu/diag/ecu_inventory.json"));
    EXPECT_TRUE(EcuInventoryStore::isValidStatePath(
        "/var/lib/ccu/diag/sub/ecu_inventory.json"));
    EXPECT_FALSE(EcuInventoryStore::isValidStatePath("relative.json"));
    EXPECT_FALSE(EcuInventoryStore::isValidStatePath("/etc/ecu_inventory.json"));
    EXPECT_FALSE(EcuInventoryStore::isValidStatePath("/var/lib/ccu/diag/"));
    EXPECT_FALSE(EcuInventoryStore::isValidStatePath(
        "/var/lib/ccu/diag/../etc/ecu_inventory.json"));
    EXPECT_FALSE(EcuInventoryStore::isValidStatePath("/var/lib/ccu/diagX/f.json"));
}

TEST_F(EcuInventoryStoreTest, HashAndTimestampHelpers) {
    const std::string digest = EcuInventoryStore::sha256Prefixed("abc");
    EXPECT_EQ(digest.rfind("sha256:", 0), 0u);
    EXPECT_EQ(digest.size(), std::string("sha256:").size() + 64u);
    EXPECT_EQ(EcuInventoryStore::isoFromEpochMs(1609459200000),
              "2021-01-01T00:00:00Z");
}

} // namespace tests
} // namespace ccu_diag
