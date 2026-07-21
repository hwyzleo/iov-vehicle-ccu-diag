#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include "cgw_sec_client_proxy.h"
#include "someip_client_proxy.h"
#include "someip_types.h"

using namespace cgw_diag;
using ::testing::_;
using ::testing::Return;

class MockSomeIpClientProxy : public SomeIpClientProxy {
public:
    MOCK_METHOD(bool, initialize, (const SomeIpServiceConfig&), (override));
    MOCK_METHOD(bool, connect, (), (override));
    MOCK_METHOD(void, disconnect, (), (override));
    MOCK_METHOD(bool, isConnected, (), (const, override));
    MOCK_METHOD(SomeIpMessage, sendRequest, (const SomeIpMessage&), (override));
    MOCK_METHOD(bool, sendNotification, (const SomeIpMessage&), (override));
    MOCK_METHOD(void, setRequestTimeout, (uint32_t), (override));
    MOCK_METHOD(uint32_t, getRequestTimeout, (), (const, override));
};

class CgwSecClientProxyTest : public ::testing::Test {
protected:
    void SetUp() override {
        auto mock = std::make_unique<MockSomeIpClientProxy>();
        mock_ptr_ = mock.get();
        proxy_ = createCgwSecClientProxy(std::move(mock));
        config_.service_id = 0x1234;
        config_.instance_id = 0x0001;
        config_.ip_address = "127.0.0.1";
        config_.port = 30509;
    }

    SomeIpMessage makeResponse(ReturnCode rc, const std::vector<uint8_t>& payload = {}) {
        SomeIpMessage msg;
        msg.header.return_code = rc;
        msg.header.message_type = MessageType::RESPONSE;
        msg.payload = payload;
        return msg;
    }

    MockSomeIpClientProxy* mock_ptr_ = nullptr;
    std::unique_ptr<CgwSecClientProxy> proxy_;
    SomeIpServiceConfig config_;
};

TEST_F(CgwSecClientProxyTest, InitializeSuccess) {
    EXPECT_CALL(*mock_ptr_, initialize(_)).WillOnce(Return(true));
    EXPECT_TRUE(proxy_->initialize(config_));
}

TEST_F(CgwSecClientProxyTest, ConnectWithoutInitialize) {
    // No proxy created yet, connect should fail
    auto raw_proxy = createCgwSecClientProxy();
    EXPECT_FALSE(raw_proxy->connect());
}

TEST_F(CgwSecClientProxyTest, ConnectSuccess) {
    EXPECT_CALL(*mock_ptr_, initialize(_)).WillOnce(Return(true));
    EXPECT_CALL(*mock_ptr_, connect()).WillOnce(Return(true));
    proxy_->initialize(config_);
    EXPECT_TRUE(proxy_->connect());
}

TEST_F(CgwSecClientProxyTest, Disconnect) {
    EXPECT_CALL(*mock_ptr_, initialize(_)).WillOnce(Return(true));
    EXPECT_CALL(*mock_ptr_, connect()).WillOnce(Return(true));
    EXPECT_CALL(*mock_ptr_, disconnect()).Times(1);
    proxy_->initialize(config_);
    proxy_->connect();
    proxy_->disconnect();
}

TEST_F(CgwSecClientProxyTest, IsConnectedWhenConnected) {
    EXPECT_CALL(*mock_ptr_, initialize(_)).WillOnce(Return(true));
    EXPECT_CALL(*mock_ptr_, isConnected()).WillOnce(Return(true));
    proxy_->initialize(config_);
    EXPECT_TRUE(proxy_->isConnected());
}

TEST_F(CgwSecClientProxyTest, IsConnectedWhenDisconnected) {
    EXPECT_CALL(*mock_ptr_, initialize(_)).WillOnce(Return(true));
    EXPECT_CALL(*mock_ptr_, isConnected()).WillOnce(Return(false));
    proxy_->initialize(config_);
    EXPECT_FALSE(proxy_->isConnected());
}

TEST_F(CgwSecClientProxyTest, GetSeedWhenDisconnected) {
    EXPECT_CALL(*mock_ptr_, initialize(_)).WillOnce(Return(true));
    EXPECT_CALL(*mock_ptr_, isConnected()).WillOnce(Return(false));
    proxy_->initialize(config_);
    auto result = proxy_->getSeed(SecLevel::LEVEL_1);
    EXPECT_FALSE(result.has_value());
}

TEST_F(CgwSecClientProxyTest, GetSeedSuccess) {
    EXPECT_CALL(*mock_ptr_, initialize(_)).WillOnce(Return(true));
    EXPECT_CALL(*mock_ptr_, isConnected()).WillOnce(Return(true));

    std::vector<uint8_t> response_payload = {
        0x00, 0x00, 0x01, 0x02,  // seed_id = 258
        0xAA, 0xBB, 0xCC         // seed bytes
    };
    EXPECT_CALL(*mock_ptr_, sendRequest(_))
        .WillOnce(Return(makeResponse(ReturnCode::E_OK, response_payload)));

    proxy_->initialize(config_);
    auto result = proxy_->getSeed(SecLevel::LEVEL_1);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->seed_id, 258u);
    EXPECT_EQ(result->seed.size(), 3u);
    EXPECT_EQ(result->seed[0], 0xAA);
    EXPECT_EQ(result->seed[1], 0xBB);
    EXPECT_EQ(result->seed[2], 0xCC);
}

TEST_F(CgwSecClientProxyTest, GetSeedTransportError) {
    EXPECT_CALL(*mock_ptr_, initialize(_)).WillOnce(Return(true));
    EXPECT_CALL(*mock_ptr_, isConnected()).WillOnce(Return(true));
    EXPECT_CALL(*mock_ptr_, sendRequest(_))
        .WillOnce(Return(makeResponse(ReturnCode::E_TIMEOUT)));

    proxy_->initialize(config_);
    auto result = proxy_->getSeed(SecLevel::LEVEL_1);
    EXPECT_FALSE(result.has_value());
}

TEST_F(CgwSecClientProxyTest, GetSeedMalformedResponseTooShort) {
    EXPECT_CALL(*mock_ptr_, initialize(_)).WillOnce(Return(true));
    EXPECT_CALL(*mock_ptr_, isConnected()).WillOnce(Return(true));

    std::vector<uint8_t> short_payload = {0x00, 0x01};  // Only 2 bytes
    EXPECT_CALL(*mock_ptr_, sendRequest(_))
        .WillOnce(Return(makeResponse(ReturnCode::E_OK, short_payload)));

    proxy_->initialize(config_);
    auto result = proxy_->getSeed(SecLevel::LEVEL_1);
    EXPECT_FALSE(result.has_value());
}

TEST_F(CgwSecClientProxyTest, GetSeedMalformedResponseEmpty) {
    EXPECT_CALL(*mock_ptr_, initialize(_)).WillOnce(Return(true));
    EXPECT_CALL(*mock_ptr_, isConnected()).WillOnce(Return(true));
    EXPECT_CALL(*mock_ptr_, sendRequest(_))
        .WillOnce(Return(makeResponse(ReturnCode::E_OK)));

    proxy_->initialize(config_);
    auto result = proxy_->getSeed(SecLevel::LEVEL_1);
    EXPECT_FALSE(result.has_value());
}

TEST_F(CgwSecClientProxyTest, GetSeedExceptionSafety) {
    EXPECT_CALL(*mock_ptr_, initialize(_)).WillOnce(Return(true));
    EXPECT_CALL(*mock_ptr_, isConnected()).WillOnce(Return(true));
    EXPECT_CALL(*mock_ptr_, sendRequest(_))
        .WillOnce(testing::Throw(std::runtime_error("transport failure")));

    proxy_->initialize(config_);
    auto result = proxy_->getSeed(SecLevel::LEVEL_1);
    EXPECT_FALSE(result.has_value());
}

TEST_F(CgwSecClientProxyTest, VerifyKeyWhenDisconnected) {
    EXPECT_CALL(*mock_ptr_, initialize(_)).WillOnce(Return(true));
    EXPECT_CALL(*mock_ptr_, isConnected()).WillOnce(Return(false));
    proxy_->initialize(config_);
    std::vector<uint8_t> key = {0x01, 0x02, 0x03, 0x04};
    auto result = proxy_->verifyKey(SecLevel::LEVEL_1, 12345, key);
    EXPECT_EQ(result, SecResult::SERVICE_UNAVAILABLE);
}

TEST_F(CgwSecClientProxyTest, VerifyKeySuccess) {
    EXPECT_CALL(*mock_ptr_, initialize(_)).WillOnce(Return(true));
    EXPECT_CALL(*mock_ptr_, isConnected()).WillOnce(Return(true));

    std::vector<uint8_t> response_payload = {0x00};  // Positive response
    EXPECT_CALL(*mock_ptr_, sendRequest(_))
        .WillOnce(Return(makeResponse(ReturnCode::E_OK, response_payload)));

    proxy_->initialize(config_);
    std::vector<uint8_t> key = {0x01, 0x02, 0x03, 0x04};
    auto result = proxy_->verifyKey(SecLevel::LEVEL_1, 12345, key);
    EXPECT_EQ(result, SecResult::SUCCESS);
}

TEST_F(CgwSecClientProxyTest, VerifyKeyInvalidKeyNrc0x35) {
    EXPECT_CALL(*mock_ptr_, initialize(_)).WillOnce(Return(true));
    EXPECT_CALL(*mock_ptr_, isConnected()).WillOnce(Return(true));

    std::vector<uint8_t> response_payload = {0x35};  // NRC INVALID_KEY
    EXPECT_CALL(*mock_ptr_, sendRequest(_))
        .WillOnce(Return(makeResponse(ReturnCode::E_OK, response_payload)));

    proxy_->initialize(config_);
    std::vector<uint8_t> key = {0x01, 0x02, 0x03, 0x04};
    auto result = proxy_->verifyKey(SecLevel::LEVEL_1, 12345, key);
    EXPECT_EQ(result, SecResult::INVALID_KEY);
}

TEST_F(CgwSecClientProxyTest, VerifyKeyUnknownNrc) {
    EXPECT_CALL(*mock_ptr_, initialize(_)).WillOnce(Return(true));
    EXPECT_CALL(*mock_ptr_, isConnected()).WillOnce(Return(true));

    std::vector<uint8_t> response_payload = {0x10};  // Some unknown NRC
    EXPECT_CALL(*mock_ptr_, sendRequest(_))
        .WillOnce(Return(makeResponse(ReturnCode::E_OK, response_payload)));

    proxy_->initialize(config_);
    std::vector<uint8_t> key = {0x01, 0x02, 0x03, 0x04};
    auto result = proxy_->verifyKey(SecLevel::LEVEL_1, 12345, key);
    EXPECT_EQ(result, SecResult::UNKNOWN_ERROR);
}

TEST_F(CgwSecClientProxyTest, VerifyKeyTransportTimeout) {
    EXPECT_CALL(*mock_ptr_, initialize(_)).WillOnce(Return(true));
    EXPECT_CALL(*mock_ptr_, isConnected()).WillOnce(Return(true));
    EXPECT_CALL(*mock_ptr_, sendRequest(_))
        .WillOnce(Return(makeResponse(ReturnCode::E_TIMEOUT)));

    proxy_->initialize(config_);
    std::vector<uint8_t> key = {0x01, 0x02, 0x03, 0x04};
    auto result = proxy_->verifyKey(SecLevel::LEVEL_1, 12345, key);
    EXPECT_EQ(result, SecResult::TIMEOUT);
}

TEST_F(CgwSecClientProxyTest, VerifyKeyTransportNotReachable) {
    EXPECT_CALL(*mock_ptr_, initialize(_)).WillOnce(Return(true));
    EXPECT_CALL(*mock_ptr_, isConnected()).WillOnce(Return(true));
    EXPECT_CALL(*mock_ptr_, sendRequest(_))
        .WillOnce(Return(makeResponse(ReturnCode::E_NOT_REACHABLE)));

    proxy_->initialize(config_);
    std::vector<uint8_t> key = {0x01, 0x02, 0x03, 0x04};
    auto result = proxy_->verifyKey(SecLevel::LEVEL_1, 12345, key);
    EXPECT_EQ(result, SecResult::SERVICE_UNAVAILABLE);
}

TEST_F(CgwSecClientProxyTest, VerifyKeyTransportNotReady) {
    EXPECT_CALL(*mock_ptr_, initialize(_)).WillOnce(Return(true));
    EXPECT_CALL(*mock_ptr_, isConnected()).WillOnce(Return(true));
    EXPECT_CALL(*mock_ptr_, sendRequest(_))
        .WillOnce(Return(makeResponse(ReturnCode::E_NOT_READY)));

    proxy_->initialize(config_);
    std::vector<uint8_t> key = {0x01, 0x02, 0x03, 0x04};
    auto result = proxy_->verifyKey(SecLevel::LEVEL_1, 12345, key);
    EXPECT_EQ(result, SecResult::SERVICE_UNAVAILABLE);
}

TEST_F(CgwSecClientProxyTest, VerifyKeyEmptyPayload) {
    EXPECT_CALL(*mock_ptr_, initialize(_)).WillOnce(Return(true));
    EXPECT_CALL(*mock_ptr_, isConnected()).WillOnce(Return(true));
    EXPECT_CALL(*mock_ptr_, sendRequest(_))
        .WillOnce(Return(makeResponse(ReturnCode::E_OK)));

    proxy_->initialize(config_);
    std::vector<uint8_t> key = {0x01, 0x02, 0x03, 0x04};
    auto result = proxy_->verifyKey(SecLevel::LEVEL_1, 12345, key);
    EXPECT_EQ(result, SecResult::UNKNOWN_ERROR);
}

TEST_F(CgwSecClientProxyTest, VerifyKeyExceptionSafety) {
    EXPECT_CALL(*mock_ptr_, initialize(_)).WillOnce(Return(true));
    EXPECT_CALL(*mock_ptr_, isConnected()).WillOnce(Return(true));
    EXPECT_CALL(*mock_ptr_, sendRequest(_))
        .WillOnce(testing::Throw(std::runtime_error("transport failure")));

    proxy_->initialize(config_);
    std::vector<uint8_t> key = {0x01, 0x02, 0x03, 0x04};
    auto result = proxy_->verifyKey(SecLevel::LEVEL_1, 12345, key);
    EXPECT_EQ(result, SecResult::UNKNOWN_ERROR);
}

TEST_F(CgwSecClientProxyTest, SetRequestTimeout) {
    EXPECT_CALL(*mock_ptr_, setRequestTimeout(5000)).Times(1);
    proxy_->setRequestTimeout(5000);
}

TEST_F(CgwSecClientProxyTest, GetRequestTimeout) {
    EXPECT_CALL(*mock_ptr_, getRequestTimeout()).WillOnce(Return(3000));
    EXPECT_EQ(proxy_->getRequestTimeout(), 3000u);
}

TEST_F(CgwSecClientProxyTest, SetRequestTimeoutWithoutProxy) {
    auto raw_proxy = createCgwSecClientProxy();
    raw_proxy->setRequestTimeout(5000);  // Should not crash
}

TEST_F(CgwSecClientProxyTest, GetRequestTimeoutWithoutProxy) {
    auto raw_proxy = createCgwSecClientProxy();
    EXPECT_EQ(raw_proxy->getRequestTimeout(), 0u);
}

TEST_F(CgwSecClientProxyTest, VerifyKeyRequestPayloadFormat) {
    EXPECT_CALL(*mock_ptr_, initialize(_)).WillOnce(Return(true));
    EXPECT_CALL(*mock_ptr_, isConnected()).WillOnce(Return(true));

    SomeIpMessage captured_request;
    EXPECT_CALL(*mock_ptr_, sendRequest(_))
        .WillOnce([&captured_request](const SomeIpMessage& req) {
            captured_request = req;
            return SomeIpMessage{};
        });

    proxy_->initialize(config_);
    std::vector<uint8_t> key = {0xAA, 0xBB};
    proxy_->verifyKey(SecLevel::LEVEL_1, 0x12345678, key);

    // Verify request structure
    EXPECT_EQ(captured_request.header.service_id, 0x1234);
    EXPECT_EQ(captured_request.header.method_id, 0x0002);
    EXPECT_EQ(captured_request.header.message_type, MessageType::REQUEST);
    EXPECT_EQ(captured_request.header.length, 8u + 1 + 4 + 2);  // header + level + seed_id + key

    // Verify payload: level(1) + seed_id(4) + key(2)
    ASSERT_EQ(captured_request.payload.size(), 7u);
    EXPECT_EQ(captured_request.payload[0], 0x01);  // SecLevel::LEVEL_1
    EXPECT_EQ(captured_request.payload[1], 0x12);  // seed_id byte 0
    EXPECT_EQ(captured_request.payload[2], 0x34);  // seed_id byte 1
    EXPECT_EQ(captured_request.payload[3], 0x56);  // seed_id byte 2
    EXPECT_EQ(captured_request.payload[4], 0x78);  // seed_id byte 3
    EXPECT_EQ(captured_request.payload[5], 0xAA);  // key byte 0
    EXPECT_EQ(captured_request.payload[6], 0xBB);  // key byte 1
}

TEST_F(CgwSecClientProxyTest, GetSeedRequestPayloadFormat) {
    EXPECT_CALL(*mock_ptr_, initialize(_)).WillOnce(Return(true));
    EXPECT_CALL(*mock_ptr_, isConnected()).WillOnce(Return(true));

    SomeIpMessage captured_request;
    EXPECT_CALL(*mock_ptr_, sendRequest(_))
        .WillOnce([&captured_request](const SomeIpMessage& req) {
            captured_request = req;
            SomeIpMessage resp;
            resp.header.return_code = ReturnCode::E_OK;
            resp.payload = {0x00, 0x00, 0x00, 0x01, 0xFF};
            return resp;
        });

    proxy_->initialize(config_);
    proxy_->getSeed(SecLevel::LEVEL_1);

    EXPECT_EQ(captured_request.header.service_id, 0x1234);
    EXPECT_EQ(captured_request.header.method_id, 0x0001);
    EXPECT_EQ(captured_request.header.message_type, MessageType::REQUEST);
    EXPECT_EQ(captured_request.header.length, 8u + 1);  // header + level

    ASSERT_EQ(captured_request.payload.size(), 1u);
    EXPECT_EQ(captured_request.payload[0], 0x01);  // SecLevel::LEVEL_1
}
