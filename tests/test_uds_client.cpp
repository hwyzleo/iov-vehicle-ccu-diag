#include <gtest/gtest.h>
#include "uds_client.h"
#include "transport_adapter.h"
#include <memory>
#include <vector>
#include <functional>

namespace cgw_diag {
namespace tests {

// MockTransport with configurable responses
class MockTransport : public TransportAdapter {
public:
    using RequestHandler = std::function<TransportResponse(uint16_t, const std::vector<uint8_t>&)>;

    MockTransport() = default;

    void setRequestHandler(RequestHandler handler) {
        handler_ = std::move(handler);
    }

    // Capture the last request for verification
    struct CapturedRequest {
        uint16_t logical_address;
        std::vector<uint8_t> request;
    };
    std::vector<CapturedRequest> captured_requests;

    TransportResponse sendRequest(uint16_t logical_address,
                                  const std::vector<uint8_t>& request) override {
        captured_requests.push_back({logical_address, request});
        if (handler_) {
            return handler_(logical_address, request);
        }
        TransportResponse resp;
        resp.success = false;
        resp.error_message = "No handler configured";
        return resp;
    }

    bool isReachable(uint16_t logical_address) override {
        (void)logical_address;
        return true;
    }

    std::string getTransportType() const override {
        return "Mock";
    }

private:
    RequestHandler handler_;
};

class UdsClientTest : public ::testing::Test {
protected:
    void SetUp() override {
        mock_transport = std::make_shared<MockTransport>();
        config.p2_server_max_ms = 50;
        config.p2_star_server_max_ms = 5000;
        config.s3_session_timeout_ms = 5000;
        config.tester_present_interval_ms = 2000;
        config.request_timeout_ms = 2000;
        config.retry_interval_ms = 100;
        config.max_retry_count = 2;

        client = std::make_unique<UdsClient>(mock_transport, config);
    }

    // Helper: build a positive response for a given DID
    static TransportResponse makePositiveResponse(uint16_t did,
                                                   const std::vector<uint8_t>& payload) {
        TransportResponse resp;
        resp.success = true;
        resp.data = {0x62, static_cast<uint8_t>((did >> 8) & 0xFF),
                     static_cast<uint8_t>(did & 0xFF)};
        resp.data.insert(resp.data.end(), payload.begin(), payload.end());
        return resp;
    }

    // Helper: build a negative response with given NRC
    static TransportResponse makeNegativeResponse(uint8_t nrc) {
        TransportResponse resp;
        resp.success = true;
        resp.data = {0x7F, 0x22, nrc};
        return resp;
    }

    // Helper: build a transport failure
    static TransportResponse makeTransportFailure(const std::string& msg) {
        TransportResponse resp;
        resp.success = false;
        resp.error_message = msg;
        return resp;
    }

    std::shared_ptr<MockTransport> mock_transport;
    DiagConfig config;
    std::unique_ptr<UdsClient> client;
};

TEST_F(UdsClientTest, ReadDataByIdentifierSuccess) {
    mock_transport->setRequestHandler([](uint16_t, const std::vector<uint8_t>& request) {
        uint16_t did = (request[1] << 8) | request[2];
        return UdsClientTest::makePositiveResponse(did, {'1', '.', '0', '.', '0', '\0'});
    });

    auto response = client->readDataByIdentifier(0x1A, 0xF195);

    EXPECT_TRUE(response.success);
    ASSERT_GE(response.data.size(), 6);
    EXPECT_EQ(response.data[0], '1');
    EXPECT_EQ(response.data[1], '.');
    EXPECT_EQ(response.data[2], '0');
}

TEST_F(UdsClientTest, ReadDataByIdentifierMultipleDids) {
    mock_transport->setRequestHandler([](uint16_t, const std::vector<uint8_t>& request) {
        uint16_t did = (request[1] << 8) | request[2];
        return UdsClientTest::makePositiveResponse(did, {'V', 'A', 'L', '\0'});
    });

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

TEST_F(UdsClientTest, ReadDataByIdentifierTransportFailureReturnsError) {
    mock_transport->setRequestHandler([](uint16_t, const std::vector<uint8_t>&) {
        return UdsClientTest::makeTransportFailure("transport error");
    });

    auto response = client->readDataByIdentifier(0x1A, 0xF195);

    EXPECT_FALSE(response.success);
    EXPECT_EQ(response.error_message, "transport error");
}

TEST_F(UdsClientTest, ReadDataByIdentifierTransportFailureRetries) {
    int call_count = 0;
    mock_transport->setRequestHandler([&call_count](uint16_t, const std::vector<uint8_t>&) {
        call_count++;
        return UdsClientTest::makeTransportFailure("fail");
    });

    auto response = client->readDataByIdentifier(0x1A, 0xF195);

    EXPECT_FALSE(response.success);
    // max_retry_count=2, so loop runs 3 times (0, 1, 2)
    EXPECT_EQ(call_count, 3);
}

TEST_F(UdsClientTest, ReadDataByIdentifierRetriesThenSucceeds) {
    int call_count = 0;
    mock_transport->setRequestHandler([&call_count](uint16_t, const std::vector<uint8_t>& request) {
        call_count++;
        if (call_count < 3) {
            return UdsClientTest::makeTransportFailure("transient error");
        }
        uint16_t did = (request[1] << 8) | request[2];
        return UdsClientTest::makePositiveResponse(did, {'O', 'K', '\0'});
    });

    auto response = client->readDataByIdentifier(0x1A, 0xF195);

    EXPECT_TRUE(response.success);
    EXPECT_EQ(call_count, 3);
}

TEST_F(UdsClientTest, ReadDataByIdentifierNegativeResponse) {
    mock_transport->setRequestHandler([](uint16_t, const std::vector<uint8_t>&) {
        return UdsClientTest::makeNegativeResponse(0x31); // requestOutOfRange
    });

    auto response = client->readDataByIdentifier(0x1A, 0xF195);

    EXPECT_FALSE(response.success);
    EXPECT_EQ(response.nrc, 0x31);
    EXPECT_EQ(response.error_message, "Negative response received");
}

TEST_F(UdsClientTest, CreateReadDataByIdentifierRequest) {
    mock_transport->setRequestHandler([](uint16_t, const std::vector<uint8_t>& request) {
        return UdsClientTest::makePositiveResponse(0xF195, {'V', '\0'});
    });

    client->readDataByIdentifier(0x1A, 0xF195);

    ASSERT_EQ(mock_transport->captured_requests.size(), 1);
    const auto& req = mock_transport->captured_requests[0].request;
    ASSERT_EQ(req.size(), 3);
    EXPECT_EQ(req[0], 0x22);
    EXPECT_EQ(req[1], 0xF1);  // DID high byte
    EXPECT_EQ(req[2], 0x95);  // DID low byte
}

} // namespace tests
} // namespace cgw_diag
