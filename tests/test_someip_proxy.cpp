#include <gtest/gtest.h>
#include "someip_client_proxy.h"
#include "someip_service.h"
#include "someip_types.h"
#include "test_helpers.h"

namespace cgw_diag {
namespace tests {

class SomeIpProxyTest : public ::testing::Test {
protected:
    void SetUp() override {
        service_config_ = getAvailableSomeIpConfig();
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
    auto service = createSomeIpService();
    ASSERT_TRUE(service->initialize(service_config_));
    ASSERT_TRUE(service->start());

    // 使用服务实际绑定的端口
    SomeIpServiceConfig client_config = service_config_;
    client_config.port = service->getBoundPort();

    auto proxy = createSomeIpClientProxy();
    proxy->initialize(client_config);
    EXPECT_TRUE(proxy->connect());
    EXPECT_TRUE(proxy->isConnected());
    proxy->disconnect();
    EXPECT_FALSE(proxy->isConnected());

    service->stop();
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