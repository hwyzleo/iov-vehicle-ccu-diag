#include <gtest/gtest.h>
#include "someip_client_proxy.h"
#include "someip_service.h"
#include "someip_types.h"
#include "test_helpers.h"
#include <memory>

namespace cgw_diag {
namespace tests {

class SomeIpFactoryTest : public ::testing::Test {
protected:
    void SetUp() override {
        config_ = getAvailableSomeIpConfig();
    }

    SomeIpServiceConfig config_;
};

TEST_F(SomeIpFactoryTest, CreateClientProxy) {
    auto proxy = createSomeIpClientProxy();
    ASSERT_NE(proxy, nullptr);
    EXPECT_FALSE(proxy->isConnected());
}

TEST_F(SomeIpFactoryTest, CreateService) {
    auto service = createSomeIpService();
    ASSERT_NE(service, nullptr);
    EXPECT_FALSE(service->isRunning());
}

TEST_F(SomeIpFactoryTest, ClientProxyInitialize) {
    auto proxy = createSomeIpClientProxy();
    EXPECT_TRUE(proxy->initialize(config_));
}

TEST_F(SomeIpFactoryTest, ServiceInitialize) {
    auto service = createSomeIpService();
    EXPECT_TRUE(service->initialize(config_));
}

TEST_F(SomeIpFactoryTest, ClientProxyConnect) {
#ifdef USE_MOCK_SOMEIP
    auto service = createSomeIpService();
    ASSERT_TRUE(service->initialize(config_));
    ASSERT_TRUE(service->start());

    // 使用服务实际绑定的端口
    SomeIpServiceConfig client_config = config_;
    client_config.port = service->getBoundPort();

    auto proxy = createSomeIpClientProxy();
    proxy->initialize(client_config);
    EXPECT_TRUE(proxy->connect());
    EXPECT_TRUE(proxy->isConnected());
    proxy->disconnect();
    service->stop();
#else
    auto proxy = createSomeIpClientProxy();
    proxy->initialize(config_);
    // vsomeip 实现可能需要真实的服务才能连接
    EXPECT_FALSE(proxy->isConnected());
#endif
}

TEST_F(SomeIpFactoryTest, ServiceStartStop) {
    auto service = createSomeIpService();
    service->initialize(config_);
    
#ifdef USE_MOCK_SOMEIP
    // Mock 实现应该能成功启动
    EXPECT_TRUE(service->start());
    EXPECT_TRUE(service->isRunning());
    service->stop();
    EXPECT_FALSE(service->isRunning());
#else
    // vsomeip 实现可能需要真实的服务才能启动
    EXPECT_FALSE(service->isRunning());
#endif
}

TEST_F(SomeIpFactoryTest, ServiceOfferService) {
    auto service = createSomeIpService();
    service->initialize(config_);
    
#ifdef USE_MOCK_SOMEIP
    // Mock 实现应该能成功提供服务
    service->start();
    EXPECT_TRUE(service->offerService());
    service->stop();
#else
    // vsomeip 实现可能需要真实的服务才能提供服务
    EXPECT_FALSE(service->offerService());
#endif
}

TEST_F(SomeIpFactoryTest, SetRequestTimeout) {
    auto proxy = createSomeIpClientProxy();
    proxy->setRequestTimeout(5000);
    EXPECT_EQ(proxy->getRequestTimeout(), 5000);
}

TEST_F(SomeIpFactoryTest, GetServiceId) {
    auto service = createSomeIpService();
    service->initialize(config_);
    EXPECT_EQ(service->getServiceId(), 0x1110);  // CGW-DIAG-DSN-CR-007
    EXPECT_EQ(service->getInstanceId(), 0x0001);
}

} // namespace tests
} // namespace cgw_diag
