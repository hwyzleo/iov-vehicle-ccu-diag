#include <gtest/gtest.h>
#include "someip_service.h"
#include "someip_types.h"

namespace cgw_diag {
namespace tests {

class SomeIpServiceTest : public ::testing::Test {
protected:
    void SetUp() override {
        config_.service_id = 0x1234;
        config_.instance_id = 0x0001;
        config_.major_version = 1;
        config_.minor_version = 0;
        config_.ip_address = "127.0.0.1";
        config_.port = 30501;
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
    service->registerMethodHandler(0x0001, [&handler_called](const SomeIpMessage& req) {
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
    EXPECT_EQ(service->getServiceId(), 0x1234);
    EXPECT_EQ(service->getInstanceId(), 0x0001);
}

} // namespace tests
} // namespace cgw_diag
