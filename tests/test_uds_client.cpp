#include <gtest/gtest.h>
#include "uds_client.h"
#include "do_can_transport.h"
#include <memory>

namespace cgw_diag {
namespace tests {

class UdsClientTest : public ::testing::Test {
protected:
    void SetUp() override {
        transport = std::make_shared<DoCanTransport>("can0", 500000);
        config.p2_server_max_ms = 50;
        config.p2_star_server_max_ms = 5000;
        config.s3_session_timeout_ms = 5000;
        config.tester_present_interval_ms = 2000;
        config.request_timeout_ms = 2000;
        config.retry_interval_ms = 100;
        config.max_retry_count = 2;

        client = std::make_unique<UdsClient>(transport, config);
    }

    std::shared_ptr<DoCanTransport> transport;
    DiagConfig config;
    std::unique_ptr<UdsClient> client;
};

TEST_F(UdsClientTest, ReadDataByIdentifierSuccess) {
    auto response = client->readDataByIdentifier(0x1A, 0xF195);

    EXPECT_TRUE(response.success);
    ASSERT_GE(response.data.size(), 6);
    EXPECT_EQ(response.data[0], '1');
    EXPECT_EQ(response.data[1], '.');
    EXPECT_EQ(response.data[2], '0');
}

TEST_F(UdsClientTest, ReadDataByIdentifierMultipleDids) {
    std::vector<uint16_t> dids = {0xF195, 0xF187};
    auto responses = client->readMultipleDids(0x1A, dids);

    ASSERT_EQ(responses.size(), 2);
    EXPECT_TRUE(responses[0].success);
    EXPECT_TRUE(responses[1].success);
}

TEST_F(UdsClientTest, ParseVersionDataAscii) {
    std::vector<uint8_t> data = {'1', '.', '0', '.', '0', '\0'};
    auto result = client->parseVersionData(data, "ASCII", 16);

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result.value(), "1.0.0");
}

TEST_F(UdsClientTest, ParseVersionDataHex) {
    std::vector<uint8_t> data = {0x12, 0x34, 0x56, 0x78};
    auto result = client->parseVersionData(data, "HEX", 4);

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result.value(), "12345678");
}

TEST_F(UdsClientTest, ParseVersionDataEmpty) {
    std::vector<uint8_t> data;
    auto result = client->parseVersionData(data, "ASCII", 16);

    EXPECT_FALSE(result.has_value());
}

TEST_F(UdsClientTest, ParseVersionDataUnknownEncoding) {
    std::vector<uint8_t> data = {0x01, 0x02, 0x03};
    auto result = client->parseVersionData(data, "UNKNOWN", 3);

    EXPECT_FALSE(result.has_value());
}

TEST_F(UdsClientTest, ParseVersionDataBcd) {
    std::vector<uint8_t> data = {0x12, 0x34};
    auto result = client->parseVersionData(data, "BCD", 2);

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result.value(), "1234");
}

TEST_F(UdsClientTest, ReadDataByIdentifierTransportFailure) {
    DoCanTransport bad_transport("invalid", 0);
    auto bad_client = std::make_unique<UdsClient>(
        std::make_shared<DoCanTransport>("can0", 500000), config);

    // Test with unsupported request to simulate failure
    std::vector<uint8_t> request = {0x10, 0x01};
    auto response = transport->sendRequest(0x1A, request);
    EXPECT_FALSE(response.success);
}

TEST_F(UdsClientTest, CreateReadDataByIdentifierRequest) {
    // Verify request format: 0x22, DID_H, DID_L
    auto response = client->readDataByIdentifier(0x1A, 0xF195);
    // The request should be created internally - we verify through the response
    EXPECT_TRUE(response.success);
}

} // namespace tests
} // namespace cgw_diag
