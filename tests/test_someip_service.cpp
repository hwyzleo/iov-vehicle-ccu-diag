#include <gtest/gtest.h>
#include "someip_service.h"
#include "someip_types.h"
#include "fota_someip_service.h"
#include "fota_facade.h"
#include "vin_store.h"
#include "constants.h"
#include "test_helpers.h"
#include <nlohmann/json.hpp>
#include <memory>
#include <filesystem>

namespace cgw_diag {
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
    EXPECT_EQ(service->getServiceId(), 0x7725);
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
        config.service_id = constants::CGW_DIAG_SERVICE_ID;
        config.instance_id = constants::CGW_DIAG_INSTANCE_ID;
        config.major_version = constants::CGW_DIAG_MAJOR_VERSION;
        config.minor_version = constants::CGW_DIAG_MINOR_VERSION;
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
    request.header.service_id = constants::CGW_DIAG_SERVICE_ID;
    request.header.method_id = constants::METHOD_READ_VIN;
    request.header.message_type = MessageType::REQUEST;

    auto response = fota_service->handleReadVin(request);

    EXPECT_EQ(response.header.return_code, ReturnCode::E_OK);
    EXPECT_EQ(response.header.message_type, MessageType::RESPONSE);

    std::string json_str(response.payload.begin(), response.payload.end());
    auto j = nlohmann::json::parse(json_str);
    EXPECT_EQ(j["vin"], "1234567890ABCDEFG");
    EXPECT_EQ(j["status"], "provisioned");
}

TEST_F(FotaSomeIpServiceTest, HandleReadVinNotProvisioned) {
    SomeIpMessage request;
    request.header.service_id = constants::CGW_DIAG_SERVICE_ID;
    request.header.method_id = constants::METHOD_READ_VIN;
    request.header.message_type = MessageType::REQUEST;

    auto response = fota_service->handleReadVin(request);

    EXPECT_EQ(response.header.return_code, ReturnCode::E_OK);
    EXPECT_EQ(response.header.message_type, MessageType::RESPONSE);

    std::string json_str(response.payload.begin(), response.payload.end());
    auto j = nlohmann::json::parse(json_str);
    EXPECT_TRUE(j["vin"].is_null());
    EXPECT_EQ(j["status"], "not_provisioned");
}

} // namespace tests
} // namespace cgw_diag
