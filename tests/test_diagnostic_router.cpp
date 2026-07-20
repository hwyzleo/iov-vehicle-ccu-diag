#include <gtest/gtest.h>
#include "diagnostic_router.h"
#include "did_registry.h"
#include "uds_client.h"
#include "transport_adapter.h"
#include <fstream>
#include <memory>
#include <functional>

namespace cgw_diag {
namespace tests {

class MockTransport : public TransportAdapter {
public:
    using RequestHandler = std::function<TransportResponse(uint16_t, const std::vector<uint8_t>&)>;

    MockTransport() = default;

    void setRequestHandler(RequestHandler handler) {
        handler_ = std::move(handler);
    }

    TransportResponse sendRequest(uint16_t logical_address,
                                  const std::vector<uint8_t>& request) override {
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

class DiagnosticRouterTest : public ::testing::Test {
protected:
    void SetUp() override {
        createTestConfig();

        registry = std::make_shared<DidRegistry>("test_did_registry.yaml");

        mock_transport = std::make_shared<MockTransport>();
        DiagConfig config;
        config.max_retry_count = 2;
        uds_client = std::make_shared<UdsClient>(mock_transport, config);

        router = std::make_unique<DiagnosticRouter>(registry, uds_client);
    }

    void TearDown() override {
        std::remove("test_did_registry.yaml");
    }

    void createTestConfig() {
        std::ofstream file("test_did_registry.yaml");
        file << R"(
registryVersion: "2026.07.0"
baselineDids: [0xF195, 0xF188, 0xF187, 0xF193]
ecus:
  - ecuId: "BMS"
    logicalAddress: 0x1A
    transport: "DoCAN"
    readMethod: "UDS_0x22"
    securityRequired: false
    dids:
      - did: 0xF195
        field: "swVersion"
        encoding: "ASCII"
        length: 16
      - did: 0xF187
        field: "partNumber"
        encoding: "ASCII"
        length: 11
  - ecuId: "IDCM"
    logicalAddress: 0x20
    transport: "DoIP"
    readMethod: "UDS_0x22"
    securityRequired: false
    dids:
      - did: 0xF195
        field: "swVersion"
        encoding: "ASCII"
        length: 16
)";
        file.close();
    }

    std::shared_ptr<DidRegistry> registry;
    std::shared_ptr<MockTransport> mock_transport;
    std::shared_ptr<UdsClient> uds_client;
    std::unique_ptr<DiagnosticRouter> router;
};

TEST_F(DiagnosticRouterTest, RouteRequestSuccess) {
    mock_transport->setRequestHandler([](uint16_t, const std::vector<uint8_t>& request) {
        TransportResponse resp;
        resp.success = true;
        resp.data = {0x62, request[1], request[2], '1', '.', '0', '.', '0', '\0'};
        return resp;
    });

    auto result = router->routeRequest("BMS", 0xF195);

    EXPECT_TRUE(result.success);
    EXPECT_EQ(result.error_code, ErrorCode::SUCCESS);
    ASSERT_GE(result.response_data.size(), 3);
    EXPECT_EQ(result.response_data[0], '1');
    EXPECT_EQ(result.response_data[1], '.');
    EXPECT_EQ(result.response_data[2], '0');
}

TEST_F(DiagnosticRouterTest, RouteRequestEcuNotRegistered) {
    auto result = router->routeRequest("UNKNOWN", 0xF195);

    EXPECT_FALSE(result.success);
    EXPECT_EQ(result.error_code, ErrorCode::ECU_NOT_REGISTERED);
    EXPECT_FALSE(result.error_message.empty());
}

TEST_F(DiagnosticRouterTest, RouteRequestDidNotSupported) {
    auto result = router->routeRequest("BMS", 0xFFFF);

    EXPECT_FALSE(result.success);
    EXPECT_EQ(result.error_code, ErrorCode::DID_NOT_SUPPORTED);
}

TEST_F(DiagnosticRouterTest, RouteRequestTransportFailure) {
    mock_transport->setRequestHandler([](uint16_t, const std::vector<uint8_t>&) {
        TransportResponse resp;
        resp.success = false;
        resp.error_message = "Transport error";
        return resp;
    });

    auto result = router->routeRequest("BMS", 0xF195);

    EXPECT_FALSE(result.success);
    EXPECT_EQ(result.error_code, ErrorCode::REQUEST_TIMEOUT);
}

TEST_F(DiagnosticRouterTest, RouteRequestNegativeResponse) {
    mock_transport->setRequestHandler([](uint16_t, const std::vector<uint8_t>&) {
        TransportResponse resp;
        resp.success = true;
        resp.data = {0x7F, 0x22, 0x31};  // NRC 0x31 (RequestOutOfRange)
        return resp;
    });

    auto result = router->routeRequest("BMS", 0xF195);

    EXPECT_FALSE(result.success);
    EXPECT_EQ(result.error_code, ErrorCode::NEGATIVE_RESPONSE);
}

TEST_F(DiagnosticRouterTest, IsEcuReachable) {
    EXPECT_TRUE(router->isEcuReachable("BMS"));
    EXPECT_FALSE(router->isEcuReachable("UNKNOWN"));
}

TEST_F(DiagnosticRouterTest, GetTransportForEcu) {
    auto transport = router->getTransportForEcu("BMS");
    EXPECT_NE(transport, nullptr);
    EXPECT_EQ(transport->getTransportType(), "DoCAN");

    auto unknown_transport = router->getTransportForEcu("UNKNOWN");
    EXPECT_EQ(unknown_transport, nullptr);
}

TEST_F(DiagnosticRouterTest, GetTransportForDoIpEcu) {
    auto transport = router->getTransportForEcu("IDCM");
    EXPECT_NE(transport, nullptr);
    EXPECT_EQ(transport->getTransportType(), "DoIP");
}

} // namespace tests
} // namespace cgw_diag
