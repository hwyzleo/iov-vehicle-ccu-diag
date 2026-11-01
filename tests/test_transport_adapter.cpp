#include <gtest/gtest.h>
#include "do_can_transport.h"
#include "doip_transport.h"

namespace ccu_diag {
namespace tests {

TEST(DoCanTransportTest, GetTransportType) {
    DoCanTransport transport("can0", 500000);
    EXPECT_EQ(transport.getTransportType(), "DoCAN");
}

TEST(DoCanTransportTest, IsReachable) {
    DoCanTransport transport("can0", 500000);
    // Simplified test - all addresses should be reachable
    EXPECT_TRUE(transport.isReachable(0x1A));
    EXPECT_TRUE(transport.isReachable(0x20));
}

TEST(DoCanTransportTest, SendRequestReadDataByIdentifier) {
    DoCanTransport transport("can0", 500000);
    
    // Test ReadDataByIdentifier request for software version
    std::vector<uint8_t> request = {0x22, 0xF1, 0x95};  // ReadDataByIdentifier for F195
    auto response = transport.sendRequest(0x1A, request);
    
    EXPECT_TRUE(response.success);
    ASSERT_GE(response.data.size(), 3);
    EXPECT_EQ(response.data[0], 0x62);  // Positive response
    EXPECT_EQ(response.data[1], 0xF1);  // DID high byte
    EXPECT_EQ(response.data[2], 0x95);  // DID low byte
}

TEST(DoIpTransportTest, GetTransportType) {
    DoIpTransport transport("192.168.1.1", 13400);
    EXPECT_EQ(transport.getTransportType(), "DoIP");
}

TEST(DoIpTransportTest, IsReachable) {
    DoIpTransport transport("192.168.1.1", 13400);
    // Simplified test - all addresses should be reachable
    EXPECT_TRUE(transport.isReachable(0x20));
    EXPECT_TRUE(transport.isReachable(0x21));
}

TEST(DoIpTransportTest, SendRequestReadDataByIdentifier) {
    DoIpTransport transport("192.168.1.1", 13400);
    
    // Test ReadDataByIdentifier request for hardware version
    std::vector<uint8_t> request = {0x22, 0xF1, 0x93};  // ReadDataByIdentifier for F193
    auto response = transport.sendRequest(0x20, request);
    
    EXPECT_TRUE(response.success);
    ASSERT_GE(response.data.size(), 3);
    EXPECT_EQ(response.data[0], 0x62);  // Positive response
    EXPECT_EQ(response.data[1], 0xF1);  // DID high byte
    EXPECT_EQ(response.data[2], 0x93);  // DID low byte
}

TEST(TransportAdapterTest, UnsupportedRequest) {
    DoCanTransport transport("can0", 500000);
    
    // Test unsupported request
    std::vector<uint8_t> request = {0x10, 0x01};  // DiagnosticSessionControl
    auto response = transport.sendRequest(0x1A, request);
    
    EXPECT_FALSE(response.success);
    EXPECT_FALSE(response.error_message.empty());
}

TEST(TransportAdapterTest, DoIpUnsupportedRequest) {
    DoIpTransport transport("192.168.1.1", 13400);
    
    // Test unsupported request
    std::vector<uint8_t> request = {0x10, 0x01};  // DiagnosticSessionControl
    auto response = transport.sendRequest(0x20, request);
    
    EXPECT_FALSE(response.success);
    EXPECT_FALSE(response.error_message.empty());
}

} // namespace tests
} // namespace ccu_diag
