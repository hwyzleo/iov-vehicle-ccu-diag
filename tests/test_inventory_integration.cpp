// =============================================================================
// tests/test_inventory_integration.cpp
// Seed/hardware inventory + 0x0005 registration end-to-end over the in-process
// SOME/IP transport (mock backend), CCU-DIAG-DSN-CR-010 §测试矩阵（集成/契约）。
// =============================================================================

#include <gtest/gtest.h>

#include "constants.h"
#include "diag_payload_codec.h"
#include "did_registry.h"
#include "diagnostic_router.h"
#include "ecu_inventory_store.h"
#include "fota_facade.h"
#include "fota_someip_proxy.h"
#include "fota_someip_service.h"
#include "fota_version_registrar.h"
#include "mock_transport.h"
#include "someip_service.h"
#include "test_helpers.h"
#include "uds_client.h"
#include "version_collector.h"
#include "vin_store.h"

#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>

#include <unistd.h>

namespace ccu_diag {
namespace tests {

namespace fs = std::filesystem;

namespace {

void writeText(const fs::path& path, const std::string& text) {
    fs::create_directories(path.parent_path());
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    file << text;
}

std::string seedJson() {
    return R"({
      "schemaVersion": 1,
      "registryVersion": "2026.11.2",
      "baselineCode": "BASE-001",
      "ecus": [
        {"ecuId":"BMS","partNumber":"PN-BMS","softwarePartNumber":"SPN-BMS","swVersion":"1.0.0","hwVersion":"HW-BMS"},
        {"ecuId":"VCU","partNumber":"PN-VCU","softwarePartNumber":"SPN-VCU","swVersion":"1.0.0","hwVersion":"HW-VCU"}
      ]
    })";
}

InstalledVersionsRequest registrationRequest(const std::string& version) {
    InstalledVersionsRequest request;
    request.schema_version = 1;
    request.vehicle_task_id = "VT-001";
    request.task_revision = 1;
    request.execution_id = "EXE-1";
    request.attempt_no = 1;
    request.idempotency_key = "EXE-1:1";
    request.applied_at_ms = 1794000000000;
    InstalledVersionItem item;
    item.ecu_id = "BMS";
    item.package_id = "PKG-BMS";
    item.package_revision = "r1";
    item.installed_version = version;
    request.items.push_back(item);
    return request;
}

} // namespace

class InventoryIntegrationTest : public ::testing::Test {
protected:
    void SetUp() override {
        const auto* info = ::testing::UnitTest::GetInstance()->current_test_info();
        temp_dir_ = fs::temp_directory_path() /
                    ("ccu_diag_inv_it_" + std::to_string(::getpid()) + "_" +
                     info->name());
        fs::remove_all(temp_dir_);
        fs::create_directories(temp_dir_);

        registry_path_ = (temp_dir_ / "registry.yaml").string();
        writeText(registry_path_, R"(
registryVersion: "2026.07.1"
baselineDids: [0xF195]
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
  - ecuId: "VCU"
    logicalAddress: 0x21
    transport: "DoCAN"
    readMethod: "UDS_0x22"
    securityRequired: false
    dids:
      - did: 0xF195
        field: "swVersion"
        encoding: "ASCII"
        length: 16
)");

        seed_path_ = (temp_dir_ / "seed.json").string();
        state_path_ = (temp_dir_ / "state" / "ecu_inventory.json").string();
        vin_path_ = (temp_dir_ / "vin.dat").string();

        registry_ = std::make_shared<DidRegistry>(registry_path_);
        docan_ = std::make_shared<MockTransport>();
        docan_->setTransportType("DoCAN");
        doip_ = std::make_shared<MockTransport>();
        doip_->setTransportType("DoIP");
        DiagConfig diag_config;
        diag_config.max_retry_count = 0;
        auto uds_client = std::make_shared<UdsClient>(docan_, diag_config);
        router_ = std::make_shared<DiagnosticRouter>(registry_, uds_client,
                                                     docan_, doip_);
        vin_store_ = std::shared_ptr<VinStore>(
            createVinStore(vin_path_).release());
    }

    void TearDown() override {
        stopService();
        fs::remove_all(temp_dir_);
    }

    InventoryConfig config(InventoryMode mode) const {
        InventoryConfig cfg;
        cfg.mode = mode;
        cfg.seed_path = seed_path_;
        cfg.state_path = state_path_;
        cfg.idempotency_window = 8;
        return cfg;
    }

    void startService(InventoryMode mode) {
        store_ = std::make_shared<EcuInventoryStore>(
            config(mode), std::vector<std::string>{"BMS", "VCU"}, nullptr, false);
        store_result_ = store_->initialize(registry_->getRegistryVersion());

        collector_ = std::make_shared<VersionCollector>(
            registry_, router_, docan_, doip_, vin_store_, store_);
        registrar_ = std::make_shared<FotaVersionRegistrar>(
            store_, std::vector<std::string>{"BMS", "VCU"}, 8);
        facade_ = std::make_shared<FotaFacade>(collector_, registry_, vin_store_,
                                               registrar_);

        service_config_ = getAvailableSomeIpConfig();
        service_ = std::make_unique<FotaSomeIpService>(
            facade_, createSomeIpService());
        ASSERT_TRUE(service_->initialize(service_config_));
        ASSERT_TRUE(service_->start());
        ASSERT_TRUE(service_->offerService());

        auto client_config = service_config_;
        client_config.port = service_->getBoundPort();
        proxy_ = std::make_unique<FotaSomeIpProxy>(createSomeIpClientProxy());
        ASSERT_TRUE(proxy_->initialize(client_config));
        ASSERT_TRUE(proxy_->connect());
    }

    void stopService() {
        if (proxy_) {
            proxy_->disconnect();
            proxy_.reset();
        }
        if (service_) {
            service_->stop();
            service_.reset();
        }
    }

    fs::path temp_dir_;
    std::string registry_path_;
    std::string seed_path_;
    std::string state_path_;
    std::string vin_path_;

    std::shared_ptr<DidRegistry> registry_;
    std::shared_ptr<MockTransport> docan_;
    std::shared_ptr<MockTransport> doip_;
    std::shared_ptr<DiagnosticRouter> router_;
    std::shared_ptr<VinStore> vin_store_;

    std::shared_ptr<EcuInventoryStore> store_;
    InventoryStoreResult store_result_;
    std::shared_ptr<VersionCollector> collector_;
    std::shared_ptr<FotaVersionRegistrar> registrar_;
    std::shared_ptr<FotaFacade> facade_;
    SomeIpServiceConfig service_config_;
    std::unique_ptr<FotaSomeIpService> service_;
    std::unique_ptr<FotaSomeIpProxy> proxy_;
};

TEST_F(InventoryIntegrationTest, SeedModeEndToEndWithoutDownlinkDiagnostics) {
    writeText(seed_path_, seedJson());
    vin_store_->writeVin("1234567890ABCDEFG");
    startService(InventoryMode::SEED);
    ASSERT_TRUE(store_result_.ok()) << store_result_.detail;

    const auto snapshot = proxy_->collectVehicleInventory();
    EXPECT_EQ(snapshot.schema_version, 1u);
    EXPECT_EQ(snapshot.inventory_source, InventorySource::SEED);
    EXPECT_EQ(snapshot.overall_result, CollectionStatus::ALL_OK);
    EXPECT_EQ(snapshot.registry_version, "2026.07.1");
    EXPECT_EQ(snapshot.baseline_id.value_or(""), "BASE-001");
    ASSERT_EQ(snapshot.ecu_list.size(), 2u);
    EXPECT_EQ(snapshot.ecu_list[0].ecu_id, "BMS");
    EXPECT_EQ(snapshot.ecu_list[0].sw_version.value_or(""), "1.0.0");
    EXPECT_EQ(snapshot.ecu_list[0].part_number.value_or(""), "PN-BMS");
    // CCU-DIAG-DSN-CR-012: the software part number is carried end-to-end and
    // stays a distinct fact from the assembly part number.
    EXPECT_EQ(snapshot.ecu_list[0].software_part_number.value_or(""), "SPN-BMS");
    EXPECT_EQ(snapshot.ecu_list[1].software_part_number.value_or(""), "SPN-VCU");
    EXPECT_NE(snapshot.ecu_list[0].software_part_number.value_or(""),
              snapshot.ecu_list[0].part_number.value_or(""));
    EXPECT_EQ(snapshot.ecu_list[0].status, EcuStatus::OK);

    // seed mode must never touch the downlink diagnostic channel.
    EXPECT_TRUE(docan_->captured_requests.empty());
    EXPECT_TRUE(doip_->captured_requests.empty());

    // Repeated collection is stable.
    const auto again = proxy_->collectVehicleInventory();
    EXPECT_EQ(again.ecu_list[0].sw_version.value_or(""), "1.0.0");
    EXPECT_EQ(again.ecu_list[0].software_part_number.value_or(""), "SPN-BMS");
}

TEST_F(InventoryIntegrationTest, SeedRegistrationThenCollectShowsNewVersion) {
    writeText(seed_path_, seedJson());
    vin_store_->writeVin("1234567890ABCDEFG");
    startService(InventoryMode::SEED);
    ASSERT_TRUE(store_result_.ok()) << store_result_.detail;

    const auto applied = proxy_->recordInstalledVersions(
        registrationRequest("2.0.0"));
    ASSERT_EQ(applied.disposition, RegistrationDisposition::APPLIED);
    EXPECT_FALSE(applied.registration_id.empty());
    EXPECT_EQ(applied.applied_count, 1u);

    const auto snapshot = proxy_->collectVehicleInventory();
    ASSERT_EQ(snapshot.ecu_list.size(), 2u);
    EXPECT_EQ(snapshot.ecu_list[0].ecu_id, "BMS");
    EXPECT_EQ(snapshot.ecu_list[0].sw_version.value_or(""), "2.0.0");
    // Registration never rewrites the software part number fact.
    EXPECT_EQ(snapshot.ecu_list[0].software_part_number.value_or(""), "SPN-BMS");
    EXPECT_EQ(snapshot.inventory_source, InventorySource::SEED);

    // Idempotent replay returns the same registrationId without changes.
    const auto duplicate = proxy_->recordInstalledVersions(
        registrationRequest("2.0.0"));
    EXPECT_EQ(duplicate.disposition, RegistrationDisposition::DUPLICATE);
    EXPECT_EQ(duplicate.registration_id, applied.registration_id);
}

TEST_F(InventoryIntegrationTest, RestartPreservesRecordAndIdempotency) {
    writeText(seed_path_, seedJson());
    vin_store_->writeVin("1234567890ABCDEFG");
    startService(InventoryMode::SEED);
    ASSERT_TRUE(store_result_.ok()) << store_result_.detail;
    const auto applied = proxy_->recordInstalledVersions(
        registrationRequest("2.0.0"));
    ASSERT_EQ(applied.disposition, RegistrationDisposition::APPLIED);
    stopService();

    // New process/store instance over the same state file.
    startService(InventoryMode::SEED);
    ASSERT_TRUE(store_result_.ok()) << store_result_.detail;

    const auto snapshot = proxy_->collectVehicleInventory();
    ASSERT_FALSE(snapshot.ecu_list.empty());
    EXPECT_EQ(snapshot.ecu_list[0].sw_version.value_or(""), "2.0.0");
    EXPECT_EQ(snapshot.ecu_list[0].software_part_number.value_or(""), "SPN-BMS");

    // Applied ring survived, so replay is still detected as DUPLICATE.
    const auto duplicate = proxy_->recordInstalledVersions(
        registrationRequest("2.0.0"));
    EXPECT_EQ(duplicate.disposition, RegistrationDisposition::DUPLICATE);
    EXPECT_EQ(duplicate.registration_id, applied.registration_id);
}

TEST_F(InventoryIntegrationTest, HardwareModeReturnsFullListAndNeverWrites) {
    docan_->setRequestHandler([](uint16_t address,
                                 const std::vector<uint8_t>& request) {
        TransportResponse response;
        response.success = true;
        const uint16_t did = static_cast<uint16_t>((request[1] << 8) | request[2]);
        response.data = {0x62, request[1], request[2]};
        if (address == 0x1A && did == 0xF195) {
            response.data.insert(response.data.end(), {'1', '.', '2', '.', '3', 0});
        } else if (address == 0x1A && did == 0xF187) {
            response.data.insert(response.data.end(), {'P', 'N', '1', 0});
        } else {
            response.data.insert(response.data.end(), {'9', '.', '9', '.', '9', 0});
        }
        return response;
    });
    vin_store_->writeVin("1234567890ABCDEFG");

    startService(InventoryMode::HARDWARE);
    ASSERT_TRUE(store_result_.ok()) << store_result_.detail;

    const auto snapshot = proxy_->collectVehicleInventory();
    EXPECT_EQ(snapshot.inventory_source, InventorySource::HARDWARE);
    EXPECT_EQ(snapshot.overall_result, CollectionStatus::ALL_OK);
    ASSERT_EQ(snapshot.ecu_list.size(), 2u);
    EXPECT_EQ(snapshot.ecu_list[0].ecu_id, "BMS");
    EXPECT_EQ(snapshot.ecu_list[0].sw_version.value_or(""), "1.2.3");
    EXPECT_EQ(snapshot.ecu_list[0].part_number.value_or(""), "PN1");
    EXPECT_EQ(snapshot.ecu_list[0].status, EcuStatus::OK);

    // Only UDS 0x22 reads may appear on the downlink channel.
    ASSERT_FALSE(docan_->captured_requests.empty());
    for (const auto& captured : docan_->captured_requests) {
        ASSERT_FALSE(captured.request.empty());
        EXPECT_EQ(captured.request[0], 0x22);
    }

    // Registration only updates the local fact record: no downlink writes.
    const size_t requests_before = docan_->captured_requests.size();
    const auto rejected = proxy_->recordInstalledVersions(
        registrationRequest("bad version!"));
    EXPECT_EQ(rejected.disposition, RegistrationDisposition::REJECTED_INVALID);
    EXPECT_EQ(docan_->captured_requests.size(), requests_before);
}

TEST_F(InventoryIntegrationTest, CorruptRecordFailsClosedWithErrorPayload) {
    writeText(seed_path_, seedJson());
    // Store initialized/imported first, then corrupt the record.
    {
        EcuInventoryStore store(config(InventoryMode::SEED),
                                {"BMS", "VCU"}, nullptr, false);
        ASSERT_TRUE(store.initialize("2026.07.1").ok());
    }
    writeText(state_path_, "{corrupted");

    startService(InventoryMode::SEED);
    EXPECT_FALSE(store_result_.ok());
    EXPECT_EQ(store_result_.diagCode(), "CCU-DIAG-1012");

    // 0x0002 must fail with an explicit error code, never an empty success.
    // Close the high-level proxy first: the mock provider serves one client.
    proxy_->disconnect();
    auto raw = createSomeIpClientProxy();
    auto client_config = service_config_;
    client_config.port = service_->getBoundPort();
    ASSERT_TRUE(raw->initialize(client_config));
    ASSERT_TRUE(raw->connect());

    SomeIpMessage request;
    request.header.service_id = constants::CCU_DIAG_SERVICE_ID;
    request.header.method_id = constants::METHOD_COLLECT_VEHICLE_INVENTORY;
    request.header.message_type = MessageType::REQUEST;
    auto response = raw->sendRequest(request);
    raw->disconnect();
    EXPECT_EQ(response.header.return_code, ReturnCode::E_NOT_OK);

    const auto j = nlohmann::json::parse(
        std::string(response.payload.begin(), response.payload.end()));
    EXPECT_EQ(j["errorCode"].get<std::string>(), "CCU-DIAG-1012");
    EXPECT_FALSE(j.contains("ecuList"));
}

TEST_F(InventoryIntegrationTest, RegistrationWithoutRecordInHardwareModeWorks) {
    // Q2-A: hardware mode with no record validates against the DidRegistry and
    // creates the fact record on first successful registration.
    startService(InventoryMode::HARDWARE);
    ASSERT_TRUE(store_result_.ok()) << store_result_.detail;
    EXPECT_FALSE(fs::exists(state_path_));

    const auto applied = proxy_->recordInstalledVersions(
        registrationRequest("2.0.0"));
    EXPECT_EQ(applied.disposition, RegistrationDisposition::APPLIED);
    EXPECT_TRUE(fs::exists(state_path_));
    EXPECT_EQ(store_->record().inventory_source, "hardware");
    EXPECT_EQ(store_->record().findEcu("BMS")->sw_version, "2.0.0");
}

} // namespace tests
} // namespace ccu_diag
