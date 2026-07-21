#include <gtest/gtest.h>
#include "someip_client_proxy.h"
#include "someip_types.h"

namespace cgw_diag {
namespace tests {

class SomeIpProxyTest : public ::testing::Test {
protected:
    void SetUp() override {
        service_config_.service_id = 0x1234;
        service_config_.instance_id = 0x0001;
        service_config_.major_version = 1;
        service_config_.minor_version = 0;
        service_config_.ip_address = "127.0.0.1";
        service_config_.port = 30501;
    }

    SomeIpServiceConfig service_config_;
};

TEST_F(SomeIpProxyTest, InitialState) {
    auto proxy = createSomeIpClientProxy();
    ASSERT_NE(proxy, nullptr);
    EXPECT_FALSE(proxy->isConnected());
}

TEST_F(SomeIpProxyTest, Initialize) {
    auto proxy = createSomeIpClientProxy();
    EXPECT_TRUE(proxy->initialize(service_config_));
}

TEST_F(SomeIpProxyTest, ConnectDisconnect) {
    auto proxy = createSomeIpClientProxy();
    proxy->initialize(service_config_);
    EXPECT_TRUE(proxy->connect());
    EXPECT_TRUE(proxy->isConnected());
    proxy->disconnect();
    EXPECT_FALSE(proxy->isConnected());
}

TEST_F(SomeIpProxyTest, SetRequestTimeout) {
    auto proxy = createSomeIpClientProxy();
    proxy->setRequestTimeout(5000);
    EXPECT_EQ(proxy->getRequestTimeout(), 5000);
}

TEST_F(SomeIpProxyTest, SendRequestWithoutConnection) {
    auto proxy = createSomeIpClientProxy();
    proxy->initialize(service_config_);

    SomeIpMessage request;
    request.header.service_id = 0x1234;
    request.header.method_id = 0x0001;
    request.header.message_type = MessageType::REQUEST;

    auto response = proxy->sendRequest(request);
    EXPECT_EQ(response.header.return_code, ReturnCode::E_NOT_OK);
}

} // namespace tests
} // namespace cgw_diag