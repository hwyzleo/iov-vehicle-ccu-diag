#include <gtest/gtest.h>
#include "cgw_sec_client_proxy.h"
#include "someip_types.h"

using namespace cgw_diag;

class CgwSecClientProxyTest : public ::testing::Test {
protected:
    void SetUp() override {
        proxy_ = createCgwSecClientProxy();
        config_.service_id = 0x1234;
        config_.instance_id = 0x0001;
        config_.ip_address = "127.0.0.1";
        config_.port = 30509;
    }

    std::unique_ptr<CgwSecClientProxy> proxy_;
    SomeIpServiceConfig config_;
};

TEST_F(CgwSecClientProxyTest, InitializeSuccess) {
    EXPECT_TRUE(proxy_->initialize(config_));
}

TEST_F(CgwSecClientProxyTest, ConnectWithoutInitialize) {
    EXPECT_FALSE(proxy_->connect());
}

TEST_F(CgwSecClientProxyTest, GetSeedWhenDisconnected) {
    proxy_->initialize(config_);
    auto result = proxy_->getSeed(SecLevel::LEVEL_1);
    EXPECT_FALSE(result.has_value());
}

TEST_F(CgwSecClientProxyTest, VerifyKeyWhenDisconnected) {
    proxy_->initialize(config_);
    std::vector<uint8_t> key = {0x01, 0x02, 0x03, 0x04};
    auto result = proxy_->verifyKey(SecLevel::LEVEL_1, 12345, key);
    EXPECT_EQ(result, SecResult::SERVICE_UNAVAILABLE);
}