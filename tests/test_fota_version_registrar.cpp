// =============================================================================
// tests/test_fota_version_registrar.cpp
// 0x0005 registration validation / idempotency / atomicity / failure mapping
// (CCU-DIAG-DSN-CR-010 §5.2 / US-013)
// =============================================================================

#include <gtest/gtest.h>

#include "constants.h"
#include "ecu_inventory_store.h"
#include "fota_version_registrar.h"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

#include <unistd.h>

namespace ccu_diag {
namespace tests {

namespace fs = std::filesystem;

namespace {

std::string seedJson(const std::string& bms_version = "1.0.0") {
    return R"({
      "schemaVersion": 1,
      "registryVersion": "2026.07.1",
      "baselineCode": "BASE-001",
      "ecus": [
        {"ecuId":"BMS","partNumber":"PN-BMS","swVersion":")" + bms_version + R"(","hwVersion":"HW-BMS"},
        {"ecuId":"VCU","partNumber":"PN-VCU","swVersion":"1.0.0","hwVersion":"HW-VCU"}
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
    mutable bool fail_write = false;

    bool writeFileFsync(const std::string& path, const std::string& data,
                        std::string& error) const override {
        if (fail_write) {
            error = "injected write failure";
            return false;
        }
        return InventoryFileAdapter::writeFileFsync(path, data, error);
    }
};

InstalledVersionsRequest makeRequest(
    const std::string& execution_id, uint32_t attempt_no,
    std::vector<InstalledVersionItem> items,
    int64_t applied_at_ms = 1794000000000) {
    InstalledVersionsRequest request;
    request.schema_version = 1;
    request.vehicle_task_id = "VT-001";
    request.task_revision = 3;
    request.execution_id = execution_id;
    request.attempt_no = attempt_no;
    request.idempotency_key = execution_id + ":" + std::to_string(attempt_no);
    request.applied_at_ms = applied_at_ms;
    request.items = std::move(items);
    return request;
}

InstalledVersionItem item(const std::string& ecu,
                          const std::string& version) {
    InstalledVersionItem it;
    it.ecu_id = ecu;
    it.package_id = "PKG-" + ecu;
    it.package_revision = "r1";
    it.installed_version = version;
    return it;
}

} // namespace

class FotaVersionRegistrarTest : public ::testing::Test {
protected:
    void SetUp() override {
        const auto* info = ::testing::UnitTest::GetInstance()->current_test_info();
        temp_dir_ = fs::temp_directory_path() /
                    ("ccu_diag_reg_" + std::to_string(::getpid()) + "_" +
                     info->name());
        fs::remove_all(temp_dir_);
        fs::create_directories(temp_dir_);
        seed_path_ = (temp_dir_ / "seed.json").string();
        state_path_ = (temp_dir_ / "state" / "ecu_inventory.json").string();
        adapter_ = std::make_shared<FailingAdapter>();
        writeText(seed_path_, seedJson());
        initializeStore(window_);
    }

    void TearDown() override {
        fs::remove_all(temp_dir_);
    }

    void initializeStore(uint32_t window) {
        InventoryConfig config;
        config.mode = InventoryMode::SEED;
        config.seed_path = seed_path_;
        config.state_path = state_path_;
        config.idempotency_window = window;
        store_ = std::make_shared<EcuInventoryStore>(
            config, std::vector<std::string>{"BMS", "VCU"}, adapter_, false);
        ASSERT_TRUE(store_->initialize("2026.07.1").ok());
        registrar_ = std::make_shared<FotaVersionRegistrar>(
            store_, std::vector<std::string>{"BMS", "VCU"}, window);
    }

    fs::path temp_dir_;
    std::string seed_path_;
    std::string state_path_;
    uint32_t window_ = 64;
    std::shared_ptr<FailingAdapter> adapter_;
    std::shared_ptr<EcuInventoryStore> store_;
    std::shared_ptr<FotaVersionRegistrar> registrar_;
};

TEST_F(FotaVersionRegistrarTest, AppliedUpdatesOnlyVersionFacts) {
    const auto before_file = readText(state_path_);

    const auto response = registrar_->recordInstalledVersions(
        makeRequest("EXE-1", 1, {item("BMS", "2.0.0"), item("VCU", "3.0.0")}));

    EXPECT_EQ(response.disposition, RegistrationDisposition::APPLIED);
    EXPECT_FALSE(response.registration_id.empty());
    EXPECT_EQ(response.applied_count, 2u);
    EXPECT_FALSE(response.effective_updated_at.empty());

    const auto& record = store_->record();
    ASSERT_NE(record.findEcu("BMS"), nullptr);
    EXPECT_EQ(record.findEcu("BMS")->sw_version, "2.0.0");
    EXPECT_EQ(record.findEcu("VCU")->sw_version, "3.0.0");
    // Identity / audit fields untouched (D7).
    EXPECT_EQ(record.registry_version, "2026.07.1");
    EXPECT_EQ(record.baseline_code, "BASE-001");
    EXPECT_EQ(record.seed_digest.rfind("sha256:", 0), 0u);
    EXPECT_EQ(record.inventory_source, "seed");
    EXPECT_EQ(record.updated_at, response.effective_updated_at);
    ASSERT_EQ(record.applied.size(), 1u);
    EXPECT_EQ(record.applied[0].execution_id, "EXE-1");
    EXPECT_EQ(record.applied[0].attempt_no, 1u);
    EXPECT_EQ(record.applied[0].applied_at, "2026-11-06T21:20:00Z");

    // Persisted atomically (content + ring in one file).
    EXPECT_NE(readText(state_path_), before_file);
}

TEST_F(FotaVersionRegistrarTest, DuplicateSameKeySameContentChangesNothing) {
    const auto first = registrar_->recordInstalledVersions(
        makeRequest("EXE-1", 1, {item("BMS", "2.0.0")}));
    ASSERT_EQ(first.disposition, RegistrationDisposition::APPLIED);

    const std::string after_first = readText(state_path_);
    const auto second = registrar_->recordInstalledVersions(
        makeRequest("EXE-1", 1, {item("BMS", "2.0.0")}));

    EXPECT_EQ(second.disposition, RegistrationDisposition::DUPLICATE);
    EXPECT_EQ(second.registration_id, first.registration_id);
    EXPECT_EQ(second.applied_count, 0u);
    EXPECT_EQ(readText(state_path_), after_first);
    EXPECT_EQ(store_->record().applied.size(), 1u);
}

TEST_F(FotaVersionRegistrarTest, SameKeyDifferentContentIsIdempotencyConflict) {
    ASSERT_EQ(registrar_->recordInstalledVersions(
                  makeRequest("EXE-1", 1, {item("BMS", "2.0.0")}))
                  .disposition,
              RegistrationDisposition::APPLIED);
    const std::string after_first = readText(state_path_);

    const auto conflict = registrar_->recordInstalledVersions(
        makeRequest("EXE-1", 1, {item("BMS", "2.1.0")}));

    EXPECT_EQ(conflict.disposition, RegistrationDisposition::REJECTED_INVALID);
    EXPECT_EQ(conflict.error_code, "CCU-DIAG-1016");
    ASSERT_FALSE(conflict.rejected_items.empty());
    EXPECT_EQ(conflict.rejected_items[0].reason_code, "IDEMPOTENCY_CONFLICT");
    EXPECT_EQ(readText(state_path_), after_first);
    EXPECT_EQ(store_->record().findEcu("BMS")->sw_version, "2.0.0");
}

TEST_F(FotaVersionRegistrarTest, NewAttemptForSameExecutionIsApplied) {
    ASSERT_EQ(registrar_->recordInstalledVersions(
                  makeRequest("EXE-1", 1, {item("BMS", "2.0.0")}))
                  .disposition,
              RegistrationDisposition::APPLIED);
    const auto second = registrar_->recordInstalledVersions(
        makeRequest("EXE-1", 2, {item("BMS", "2.1.0")}));
    EXPECT_EQ(second.disposition, RegistrationDisposition::APPLIED);
    EXPECT_EQ(store_->record().findEcu("BMS")->sw_version, "2.1.0");
    EXPECT_EQ(store_->record().applied.size(), 2u);
}

TEST_F(FotaVersionRegistrarTest, ValidationMatrixRejectsWholeRequest) {
    const std::string after_import = readText(state_path_);

    struct Case {
        std::vector<InstalledVersionItem> items;
        std::string reason;
    };
    const std::vector<Case> cases = {
        {{item("UNKNOWN", "1.0.0")}, "UNKNOWN_ECU"},
        {{item("BMS", "bad version!")}, "INVALID_VERSION"},
        {{item("BMS", std::string(65, '9'))}, "INVALID_VERSION"},
        {{item("", "1.0.0")}, "MISSING_FIELD"},
    };

    for (const auto& c : cases) {
        const auto response = registrar_->recordInstalledVersions(
            makeRequest("EXE-1", 1, c.items));
        EXPECT_EQ(response.disposition, RegistrationDisposition::REJECTED_INVALID)
            << c.reason;
        EXPECT_EQ(response.error_code, "CCU-DIAG-1014");
        ASSERT_FALSE(response.rejected_items.empty());
        EXPECT_EQ(response.rejected_items[0].reason_code, c.reason);
        EXPECT_EQ(response.applied_count, 0u);
        EXPECT_EQ(readText(state_path_), after_import);
    }
}

TEST_F(FotaVersionRegistrarTest, MixedValidityRejectsEverything) {
    const std::string after_import = readText(state_path_);
    const auto response = registrar_->recordInstalledVersions(
        makeRequest("EXE-1", 1, {item("BMS", "2.0.0"), item("NOPE", "1.0.0")}));

    EXPECT_EQ(response.disposition, RegistrationDisposition::REJECTED_INVALID);
    ASSERT_EQ(response.rejected_items.size(), 1u);
    EXPECT_EQ(response.rejected_items[0].ecu_id, "NOPE");
    EXPECT_EQ(response.rejected_items[0].reason_code, "UNKNOWN_ECU");
    EXPECT_EQ(readText(state_path_), after_import);
    EXPECT_EQ(store_->record().findEcu("BMS")->sw_version, "1.0.0");
}

TEST_F(FotaVersionRegistrarTest, PersistFailureIsRetryableAndKeepsRecord) {
    const auto before = readText(state_path_);
    const std::string version_before = store_->record().findEcu("BMS")->sw_version;

    adapter_->fail_write = true;
    const auto failed = registrar_->recordInstalledVersions(
        makeRequest("EXE-1", 1, {item("BMS", "2.0.0")}));
    EXPECT_EQ(failed.disposition, RegistrationDisposition::RETRYABLE_FAILED);
    EXPECT_EQ(failed.error_code, "CCU-DIAG-1015");
    EXPECT_EQ(readText(state_path_), before);
    EXPECT_EQ(store_->record().findEcu("BMS")->sw_version, version_before);

    // Same identity can be retried safely.
    adapter_->fail_write = false;
    const auto retried = registrar_->recordInstalledVersions(
        makeRequest("EXE-1", 1, {item("BMS", "2.0.0")}));
    EXPECT_EQ(retried.disposition, RegistrationDisposition::APPLIED);
    EXPECT_EQ(store_->record().findEcu("BMS")->sw_version, "2.0.0");
}

TEST_F(FotaVersionRegistrarTest, StructuralRequestProblemsAreRejected) {
    auto request = makeRequest("", 1, {item("BMS", "2.0.0")});
    EXPECT_EQ(registrar_->recordInstalledVersions(request).disposition,
              RegistrationDisposition::REJECTED_INVALID);

    request = makeRequest("EXE-1", 0, {item("BMS", "2.0.0")});
    EXPECT_EQ(registrar_->recordInstalledVersions(request).disposition,
              RegistrationDisposition::REJECTED_INVALID);

    request = makeRequest("EXE-1", 1, {});
    EXPECT_EQ(registrar_->recordInstalledVersions(request).disposition,
              RegistrationDisposition::REJECTED_INVALID);

    request = makeRequest("EXE-1", 1, {item("BMS", "2.0.0")});
    request.schema_version = 2;
    EXPECT_EQ(registrar_->recordInstalledVersions(request).disposition,
              RegistrationDisposition::REJECTED_INVALID);
}

TEST_F(FotaVersionRegistrarTest, RingEvictionAllowsReapplication) {
    window_ = 2;
    initializeStore(window_);

    for (int i = 1; i <= 3; ++i) {
        ASSERT_EQ(registrar_->recordInstalledVersions(makeRequest(
                      "EXE-" + std::to_string(i), 1,
                      {item("BMS", "2." + std::to_string(i) + ".0")}))
                      .disposition,
                  RegistrationDisposition::APPLIED);
    }
    ASSERT_EQ(store_->record().applied.size(), 2u);
    EXPECT_EQ(store_->record().applied.front().execution_id, "EXE-2");

    // EXE-1 was evicted from the ring, so the same identity is treated as new.
    const auto reapplied = registrar_->recordInstalledVersions(
        makeRequest("EXE-1", 1, {item("BMS", "2.1.0")}));
    EXPECT_EQ(reapplied.disposition, RegistrationDisposition::APPLIED);
}

TEST_F(FotaVersionRegistrarTest, UnavailableStoreIsRetryable) {
    writeText(seed_path_, "{bad seed");
    fs::remove_all(fs::path(state_path_).parent_path());

    InventoryConfig config;
    config.mode = InventoryMode::SEED;
    config.seed_path = seed_path_;
    config.state_path = state_path_;
    auto failed_store = std::make_shared<EcuInventoryStore>(
        config, std::vector<std::string>{"BMS"}, adapter_, false);
    ASSERT_FALSE(failed_store->initialize("2026.07.1").ok());

    FotaVersionRegistrar registrar(failed_store,
                                   std::vector<std::string>{"BMS"}, 64);
    const auto response = registrar.recordInstalledVersions(
        makeRequest("EXE-1", 1, {item("BMS", "2.0.0")}));
    EXPECT_EQ(response.disposition, RegistrationDisposition::RETRYABLE_FAILED);
    EXPECT_EQ(response.error_code, "CCU-DIAG-1015");
}

TEST_F(FotaVersionRegistrarTest, VersionStringFormat) {
    EXPECT_TRUE(FotaVersionRegistrar::isValidVersionString("1.0.0"));
    EXPECT_TRUE(FotaVersionRegistrar::isValidVersionString("V1.2.3_r4+build-5"));
    EXPECT_FALSE(FotaVersionRegistrar::isValidVersionString(""));
    EXPECT_FALSE(FotaVersionRegistrar::isValidVersionString(" has space"));
    EXPECT_FALSE(FotaVersionRegistrar::isValidVersionString("-1.0.0"));
    EXPECT_FALSE(FotaVersionRegistrar::isValidVersionString("1.0.0/2"));
    EXPECT_FALSE(FotaVersionRegistrar::isValidVersionString(
        std::string(65, '1')));
}

} // namespace tests
} // namespace ccu_diag
