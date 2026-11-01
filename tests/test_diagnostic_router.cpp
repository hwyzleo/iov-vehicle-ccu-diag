#include <gtest/gtest.h>
#include "diagnostic_router.h"
#include "did_registry.h"
#include "uds_client.h"
#include "mock_transport.h"
#include <fstream>
#include <memory>
#include <functional>
#include <filesystem>

namespace ccu_diag {
namespace tests {

class DiagnosticRouterTest : public ::testing::Test {
protected:
    void SetUp() override {
        temp_dir_ = std::filesystem::temp_directory_path();
        config_path_ = (temp_dir_ / "test_did_registry.yaml").string();
        createTestConfig();

        registry = std::make_shared<DidRegistry>(config_path_);

        mock_transport = std::make_shared<MockTransport>();
        mock_transport->setTransportType("DoCAN");

        mock_doip_transport = std::make_shared<MockTransport>();
        mock_doip_transport->setTransportType("DoIP");

        DiagConfig config;
        config.max_retry_count = 2;
        uds_client = std::make_shared<UdsClient>(mock_transport, config);

        router = std::make_unique<DiagnosticRouter>(registry, uds_client,
                                                     mock_transport, mock_doip_transport);
    }

    void TearDown() override {
        std::filesystem::remove(config_path_);
    }

    void createTestConfig() {
        std::ofstream file(config_path_);
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
  - ecuId: "VCU"
    logicalAddress: 0x30
    transport: "DoIP"
    readMethod: "SOMEIP_getVersion"
    securityRequired: false
    someipService: "vehicle.version"
    fields:
      - "swVersion"
)";
        file.close();
    }

    std::filesystem::path temp_dir_;
    std::string config_path_;
    std::shared_ptr<DidRegistry> registry;
    std::shared_ptr<MockTransport> mock_transport;
    std::shared_ptr<MockTransport> mock_doip_transport;
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

TEST_F(DiagnosticRouterTest, RouteRequestResponsePending) {
    mock_transport->setRequestHandler([](uint16_t, const std::vector<uint8_t>&) {
        TransportResponse resp;
        resp.success = true;
        resp.data = {0x7F, 0x22, 0x78};  // NRC 0x78 (ResponsePending)
        return resp;
    });

    auto result = router->routeRequest("BMS", 0xF195);

    EXPECT_FALSE(result.success);
    EXPECT_EQ(result.error_code, ErrorCode::RESPONSE_PENDING);
}

TEST_F(DiagnosticRouterTest, IsEcuReachable) {
    EXPECT_TRUE(router->isEcuReachable("BMS"));
    EXPECT_FALSE(router->isEcuReachable("UNKNOWN"));
}

TEST_F(DiagnosticRouterTest, IsEcuReachableWhenTransportUnreachable) {
    mock_transport->setReachable(false);

    EXPECT_FALSE(router->isEcuReachable("BMS"));
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

TEST_F(DiagnosticRouterTest, RouteRequestSomeIpSkipsDidValidation) {
    auto result = router->routeRequest("VCU", 0xFFFF);

    EXPECT_FALSE(result.success);
    EXPECT_NE(result.error_code, ErrorCode::DID_NOT_SUPPORTED);
}

} // namespace tests
} // namespace ccu_diag