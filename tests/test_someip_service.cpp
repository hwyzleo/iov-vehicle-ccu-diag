#include <gtest/gtest.h>
#include "someip_service.h"
#include "someip_types.h"
#include "fota_someip_service.h"
#include "fota_facade.h"
#include "fota_version_registrar.h"
#include "ecu_inventory_store.h"
#include "did_registry.h"
#include "diagnostic_router.h"
#include "uds_client.h"
#include "version_collector.h"
#include "mock_transport.h"
#include "vin_store.h"
#include "constants.h"
#include "test_helpers.h"
#include <nlohmann/json.hpp>
#include <map>
#include <fstream>
#include <memory>
#include <filesystem>

#include <unistd.h>

namespace ccu_diag {
namespace tests {

class SomeIpServiceTest : public ::testing::Test {
protected:
    void SetUp() override {
        config_ = getAvailableSomeIpConfig();
    }

    SomeIpServiceConfig config_;
};

TEST_F(SomeIpServiceTest, InitialState) {
    auto service = createSomeIpService();
    ASSERT_NE(service, nullptr);
    EXPECT_FALSE(service->isRunning());
}

TEST_F(SomeIpServiceTest, Initialize) {
    auto service = createSomeIpService();
    EXPECT_TRUE(service->initialize(config_));
}

TEST_F(SomeIpServiceTest, StartStop) {
    auto service = createSomeIpService();
    service->initialize(config_);
    EXPECT_TRUE(service->start());
    EXPECT_TRUE(service->isRunning());
    service->stop();
    EXPECT_FALSE(service->isRunning());
}

TEST_F(SomeIpServiceTest, RegisterMethodHandler) {
    auto service = createSomeIpService();
    service->initialize(config_);

    bool handler_called = false;
    service->registerMethodHandler(0x0001, [&handler_called](const SomeIpMessage&) {
        handler_called = true;
        SomeIpMessage resp;
        resp.header.return_code = ReturnCode::E_OK;
        return resp;
    });

    service->start();
    service->offerService();

    service->stop();
}

TEST_F(SomeIpServiceTest, GetServiceId) {
    auto service = createSomeIpService();
    service->initialize(config_);
    EXPECT_EQ(service->getServiceId(), 0x1110);  // CCU-DIAG-DSN-CR-007
    EXPECT_EQ(service->getInstanceId(), 0x0001);
}

class FotaSomeIpServiceTest : public ::testing::Test {
protected:
    void SetUp() override {
        temp_dir_ = std::filesystem::temp_directory_path();
        vin_store_path_ = (temp_dir_ / "test_vin_store_someip.dat").string();
        vin_store = std::shared_ptr<VinStore>(createVinStore(vin_store_path_).release());

        auto someip_service = createSomeIpService();
        facade = std::make_shared<FotaFacade>(nullptr, nullptr, vin_store);
        fota_service = std::make_unique<FotaSomeIpService>(facade, std::move(someip_service));

        SomeIpServiceConfig config = getAvailableSomeIpConfig();
        config.service_id = constants::CCU_DIAG_SERVICE_ID;
        config.instance_id = constants::CCU_DIAG_INSTANCE_ID;
        config.major_version = constants::CCU_DIAG_MAJOR_VERSION;
        config.minor_version = constants::CCU_DIAG_MINOR_VERSION;
        fota_service->initialize(config);
    }

    void TearDown() override {
        std::filesystem::remove(vin_store_path_);
    }

    std::filesystem::path temp_dir_;
    std::string vin_store_path_;
    std::shared_ptr<VinStore> vin_store;
    std::shared_ptr<FotaFacade> facade;
    std::unique_ptr<FotaSomeIpService> fota_service;
};

TEST_F(FotaSomeIpServiceTest, HandleReadVinProvisioned) {
    vin_store->writeVin("1234567890ABCDEFG");

    SomeIpMessage request;
    request.header.service_id = constants::CCU_DIAG_SERVICE_ID;
    request.header.method_id = constants::METHOD_READ_VIN;
    request.header.message_type = MessageType::REQUEST;

    auto response = fota_service->handleReadVin(request);

    EXPECT_EQ(response.header.return_code, ReturnCode::E_OK);
    EXPECT_EQ(response.header.message_type, MessageType::RESPONSE);

    // SOME/IP returns raw VIN string (17 bytes), not JSON
    std::string vin_str(response.payload.begin(), response.payload.end());
    EXPECT_EQ(vin_str, "1234567890ABCDEFG");
}

TEST_F(FotaSomeIpServiceTest, HandleReadVinNotProvisioned) {
    SomeIpMessage request;
    request.header.service_id = constants::CCU_DIAG_SERVICE_ID;
    request.header.method_id = constants::METHOD_READ_VIN;
    request.header.message_type = MessageType::REQUEST;

    auto response = fota_service->handleReadVin(request);

    // VIN not provisioned - should return error
    EXPECT_EQ(response.header.return_code, ReturnCode::E_NOT_OK);
    EXPECT_EQ(response.header.message_type, MessageType::RESPONSE);
}

// ---------------------------------------------------------------------------
// 0x0002 payload v1 / 0x0005 registration handler wiring
// (CCU-DIAG-DSN-CR-010 §5.2)
// ---------------------------------------------------------------------------

class CapturingSomeIpService : public SomeIpService {
public:
    bool initialize(const SomeIpServiceConfig& config) override {
        config_ = config;
        return true;
    }
    bool start() override { running_ = true; return true; }
    void stop() override { running_ = false; }
    bool isRunning() const override { return running_; }
    void registerMethodHandler(MethodId method_id,
                               SomeIpRequestHandler handler) override {
        handlers_[method_id] = std::move(handler);
    }
    void registerEventHandler(EventId, SomeIpNotificationHandler) override {}
    bool offerService() override { return true; }
    void stopOfferService() override {}
    ServiceId getServiceId() const override { return config_.service_id; }
    uint16_t getInstanceId() const override { return config_.instance_id; }
    uint16_t getBoundPort() const override { return config_.port; }

    bool has(MethodId method_id) const {
        return handlers_.count(method_id) != 0;
    }
    SomeIpMessage invoke(MethodId method_id, const SomeIpMessage& request) const {
        return handlers_.at(method_id)(request);
    }

private:
    SomeIpServiceConfig config_;
    bool running_ = false;
    std::map<MethodId, SomeIpRequestHandler> handlers_;
};

class InventoryHandlerTest : public ::testing::Test {
protected:
    void SetUp() override {
        const auto* info = ::testing::UnitTest::GetInstance()->current_test_info();
        temp_dir_ = std::filesystem::temp_directory_path() /
                    ("ccu_diag_handler_" + std::to_string(::getpid()) + "_" +
                     info->name());
        std::filesystem::remove_all(temp_dir_);
        std::filesystem::create_directories(temp_dir_);

        registry_path_ = (temp_dir_ / "registry.yaml").string();
        {
            std::ofstream file(registry_path_);
            file << R"(
registryVersion: "2026.07.1"
baselineDids: [0xF195]
ecus:
  - ecuId: "BMS"
    logicalAddress: 0x1A
    transport: "DoCAN"
    readMethod: "UDS_0x22"
    dids:
      - did: 0xF195
        field: "swVersion"
        encoding: "ASCII"
        length: 16
  - ecuId: "VCU"
    logicalAddress: 0x21
    transport: "DoCAN"
    readMethod: "UDS_0x22"
    dids:
      - did: 0xF195
        field: "swVersion"
        encoding: "ASCII"
        length: 16
)";
        }

        registry_ = std::make_shared<DidRegistry>(registry_path_);
        docan_ = std::make_shared<MockTransport>();
        doip_ = std::make_shared<MockTransport>();
        docan_->setRequestHandler([](uint16_t, const std::vector<uint8_t>& request) {
            TransportResponse response;
            response.success = true;
            response.data = {0x62, request[1], request[2]};
            response.data.insert(response.data.end(), {'1', '.', '0', '.', '0', 0});
            return response;
        });
        DiagConfig diag_config;
        diag_config.max_retry_count = 0;
        auto uds_client = std::make_shared<UdsClient>(docan_, diag_config);
        router_ = std::make_shared<DiagnosticRouter>(registry_, uds_client,
                                                     docan_, doip_);
        vin_store_ = std::shared_ptr<VinStore>(
            createVinStore((temp_dir_ / "vin.dat").string()).release());
        vin_store_->writeVin("1234567890ABCDEFG");
        seed_path_ = (temp_dir_ / "seed.json").string();
        state_path_ = (temp_dir_ / "state" / "inventory.json").string();
    }

    void TearDown() override {
        std::filesystem::remove_all(temp_dir_);
    }

    void build(InventoryMode mode) {
        InventoryConfig config;
        config.mode = mode;
        config.seed_path = seed_path_;
        config.state_path = state_path_;
        config.idempotency_window = 8;
        if (mode == InventoryMode::SEED) {
            std::ofstream seed(seed_path_);
            seed << R"({
  "schemaVersion": 1,
  "registryVersion": "2026.07.1",
  "ecus": [
    {"ecuId":"BMS","swVersion":"1.0.0"},
    {"ecuId":"VCU","swVersion":"1.0.0"}
  ]
})";
        }
        store_ = std::make_shared<EcuInventoryStore>(
            config, std::vector<std::string>{"BMS", "VCU"}, nullptr, false);
        store_result_ = store_->initialize("2026.07.1");
        collector_ = std::make_shared<VersionCollector>(
            registry_, router_, docan_, doip_, vin_store_, store_);
        registrar_ = std::make_shared<FotaVersionRegistrar>(
            store_, std::vector<std::string>{"BMS", "VCU"}, 8);
        facade_ = std::make_shared<FotaFacade>(collector_, registry_, vin_store_,
                                               registrar_);

        auto capturing = std::make_unique<CapturingSomeIpService>();
        capturing_ = capturing.get();
        service_ = std::make_unique<FotaSomeIpService>(facade_,
                                                       std::move(capturing));
        SomeIpServiceConfig config_someip = getAvailableSomeIpConfig();
        ASSERT_TRUE(service_->initialize(config_someip));
    }

    SomeIpMessage invoke(MethodId method_id, const std::string& payload = "") {
        SomeIpMessage request;
        request.header.service_id = constants::CCU_DIAG_SERVICE_ID;
        request.header.method_id = method_id;
        request.header.message_type = MessageType::REQUEST;
        request.payload.assign(payload.begin(), payload.end());
        return capturing_->invoke(method_id, request);
    }

    std::filesystem::path temp_dir_;
    std::string registry_path_;
    std::string seed_path_;
    std::string state_path_;
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
    CapturingSomeIpService* capturing_ = nullptr;
    std::unique_ptr<FotaSomeIpService> service_;
};

TEST_F(InventoryHandlerTest, RegistersAllMethodIdsIncludingRegistration) {
    build(InventoryMode::SEED);
    EXPECT_TRUE(capturing_->has(constants::METHOD_READ_VIN));
    EXPECT_TRUE(capturing_->has(constants::METHOD_COLLECT_VEHICLE_INVENTORY));
    EXPECT_TRUE(capturing_->has(constants::METHOD_GET_ECU_VERSION));
    EXPECT_TRUE(capturing_->has(constants::METHOD_GET_REGISTRY_VERSION));
    EXPECT_TRUE(capturing_->has(constants::METHOD_RECORD_INSTALLED_VERSIONS));
    EXPECT_EQ(constants::METHOD_RECORD_INSTALLED_VERSIONS, 0x0005);
}

TEST_F(InventoryHandlerTest, CollectReturnsFrozenPayloadV1) {
    build(InventoryMode::SEED);
    ASSERT_TRUE(store_result_.ok()) << store_result_.detail;

    const auto response = invoke(constants::METHOD_COLLECT_VEHICLE_INVENTORY);
    EXPECT_EQ(response.header.return_code, ReturnCode::E_OK);

    const auto j = nlohmann::json::parse(
        std::string(response.payload.begin(), response.payload.end()));
    EXPECT_EQ(j["schemaVersion"].get<int>(), 1);
    EXPECT_EQ(j["inventorySource"].get<std::string>(), "seed");
    EXPECT_EQ(j["vin"].get<std::string>(), "1234567890ABCDEFG");
    EXPECT_EQ(j["vinSource"].get<std::string>(), "provisioned");
    ASSERT_EQ(j["ecuList"].size(), 2u);
    EXPECT_EQ(j["ecuList"][0]["ecuId"].get<std::string>(), "BMS");
    EXPECT_EQ(j["ecuList"][0]["source"].get<std::string>(), "UDS_0x22");
    EXPECT_EQ(j["ecuList"][0]["status"].get<std::string>(), "ok");
}

TEST_F(InventoryHandlerTest, CollectFailsClosedWhenSeedInvalid) {
    // mode=seed with no seed file -> CCU-DIAG-1013, never empty success.
    build(InventoryMode::SEED);
    std::filesystem::remove(seed_path_);
    std::filesystem::remove_all(temp_dir_ / "state");
    // Rebuild store without a seed file.
    InventoryConfig config;
    config.mode = InventoryMode::SEED;
    config.seed_path = seed_path_;
    config.state_path = state_path_;
    store_ = std::make_shared<EcuInventoryStore>(
        config, std::vector<std::string>{"BMS", "VCU"}, nullptr, false);
    store_result_ = store_->initialize("2026.07.1");
    ASSERT_FALSE(store_result_.ok());
    collector_ = std::make_shared<VersionCollector>(
        registry_, router_, docan_, doip_, vin_store_, store_);
    registrar_ = std::make_shared<FotaVersionRegistrar>(
        store_, std::vector<std::string>{"BMS", "VCU"}, 8);
    facade_ = std::make_shared<FotaFacade>(collector_, registry_, vin_store_,
                                           registrar_);
    auto capturing = std::make_unique<CapturingSomeIpService>();
    capturing_ = capturing.get();
    service_ = std::make_unique<FotaSomeIpService>(facade_, std::move(capturing));
    ASSERT_TRUE(service_->initialize(getAvailableSomeIpConfig()));

    const auto response = invoke(constants::METHOD_COLLECT_VEHICLE_INVENTORY);
    EXPECT_EQ(response.header.return_code, ReturnCode::E_NOT_OK);
    const auto j = nlohmann::json::parse(
        std::string(response.payload.begin(), response.payload.end()));
    EXPECT_EQ(j["errorCode"].get<std::string>(), "CCU-DIAG-1013");
    EXPECT_FALSE(j.contains("ecuList"));
}

TEST_F(InventoryHandlerTest, RegistrationDispositionsOverSomeIp) {
    build(InventoryMode::SEED);
    ASSERT_TRUE(store_result_.ok()) << store_result_.detail;

    auto body = [](const std::string& version) {
        nlohmann::json request;
        request["schemaVersion"] = 1;
        request["executionId"] = "EXE-9";
        request["attemptNo"] = 1;
        request["appliedAtMs"] = 1794000000000;
        request["items"] = nlohmann::json::array(
            {{{ "ecuId", "BMS" },
              { "packageId", "PKG" },
              { "packageRevision", "r1" },
              { "installedVersion", version }}});
        return request.dump();
    };

    const auto applied = invoke(constants::METHOD_RECORD_INSTALLED_VERSIONS,
                               body("2.0.0"));
    EXPECT_EQ(applied.header.return_code, ReturnCode::E_OK);
    auto j = nlohmann::json::parse(
        std::string(applied.payload.begin(), applied.payload.end()));
    EXPECT_EQ(j["disposition"].get<std::string>(), "APPLIED");
    EXPECT_FALSE(j["registrationId"].get<std::string>().empty());
    EXPECT_EQ(j["appliedCount"].get<int>(), 1);

    const auto duplicate = invoke(constants::METHOD_RECORD_INSTALLED_VERSIONS,
                                 body("2.0.0"));
    j = nlohmann::json::parse(
        std::string(duplicate.payload.begin(), duplicate.payload.end()));
    EXPECT_EQ(j["disposition"].get<std::string>(), "DUPLICATE");

    const auto rejected = invoke(constants::METHOD_RECORD_INSTALLED_VERSIONS,
                                body("bad version!"));
    j = nlohmann::json::parse(
        std::string(rejected.payload.begin(), rejected.payload.end()));
    EXPECT_EQ(j["disposition"].get<std::string>(), "REJECTED_INVALID");
    ASSERT_EQ(j["rejectedItems"].size(), 1u);
    EXPECT_EQ(j["rejectedItems"][0]["reasonCode"].get<std::string>(),
              "INVALID_VERSION");

    const auto malformed = invoke(constants::METHOD_RECORD_INSTALLED_VERSIONS,
                                 "not json");
    j = nlohmann::json::parse(
        std::string(malformed.payload.begin(), malformed.payload.end()));
    EXPECT_EQ(j["disposition"].get<std::string>(), "REJECTED_INVALID");
    EXPECT_EQ(j["rejectedItems"][0]["reasonCode"].get<std::string>(),
              "INVALID_REQUEST");
}

TEST_F(InventoryHandlerTest, RegistrationNeverWritesDownlink) {
    build(InventoryMode::HARDWARE);
    ASSERT_TRUE(store_result_.ok()) << store_result_.detail;

    // One collection issues only UDS 0x22 reads.
    const auto collect = invoke(constants::METHOD_COLLECT_VEHICLE_INVENTORY);
    EXPECT_EQ(collect.header.return_code, ReturnCode::E_OK);
    const size_t after_collect = docan_->captured_requests.size();
    ASSERT_FALSE(docan_->captured_requests.empty());

    const auto response = invoke(
        constants::METHOD_RECORD_INSTALLED_VERSIONS,
        R"({"schemaVersion":1,"executionId":"EXE-1","attemptNo":1,"items":[{"ecuId":"BMS","installedVersion":"2.0.0"}]})");
    const auto j = nlohmann::json::parse(
        std::string(response.payload.begin(), response.payload.end()));
    EXPECT_EQ(j["disposition"].get<std::string>(), "APPLIED");

    EXPECT_EQ(docan_->captured_requests.size(), after_collect);
    for (const auto& captured : docan_->captured_requests) {
        EXPECT_EQ(captured.request[0], 0x22);
    }
}

} // namespace tests
} // namespace ccu_diag
