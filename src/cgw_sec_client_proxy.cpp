#include "cgw_sec_client_proxy.h"
#include "someip_client_proxy.h"
#include <random>
#include <chrono>
#include <stdexcept>

namespace cgw_diag {

// CGW-SEC service method IDs (from design document)
constexpr MethodId METHOD_GET_SEED = 0x0001;
constexpr MethodId METHOD_VERIFY_KEY = 0x0002;

// NRC codes from CGW-SEC
constexpr uint8_t NRC_INVALID_KEY = 0x35;

class CgwSecClientProxyImpl : public CgwSecClientProxy {
public:
    CgwSecClientProxyImpl() = default;
    ~CgwSecClientProxyImpl() override = default;

    bool initialize(const SomeIpServiceConfig& sec_service_config) override {
        sec_service_config_ = sec_service_config;
        // Create SOME/IP client proxy for communication with CGW-SEC
        someip_proxy_ = createSomeIpClientProxy();
        if (!someip_proxy_) {
            return false;
        }
        return someip_proxy_->initialize(sec_service_config);
    }

    bool connect() override {
        if (!someip_proxy_) {
            return false;
        }
        return someip_proxy_->connect();
    }

    void disconnect() override {
        if (someip_proxy_) {
            someip_proxy_->disconnect();
        }
    }

    bool isConnected() const override {
        return someip_proxy_ && someip_proxy_->isConnected();
    }

    std::optional<SecuritySeed> getSeed(SecLevel level) override {
        if (!isConnected()) {
            return std::nullopt;
        }

        // Build SOME/IP request for getSeed
        SomeIpMessage request;
        request.header.service_id = sec_service_config_.service_id;
        request.header.method_id = METHOD_GET_SEED;
        request.header.message_type = MessageType::REQUEST;
        
        // Payload: security level (1 byte)
        request.payload.push_back(static_cast<uint8_t>(level));

        // Send request and receive response
        SomeIpMessage response = someip_proxy_->sendRequest(request);
        
        // Check response
        if (response.header.return_code != ReturnCode::E_OK) {
            return std::nullopt;
        }

        // Parse response: seed_id (4 bytes) + seed (variable length)
        if (response.payload.size() < 5) {  // At least 4 bytes seed_id + 1 byte seed
            return std::nullopt;
        }

        SecuritySeed seed;
        seed.seed_id = (response.payload[0] << 24) |
                       (response.payload[1] << 16) |
                       (response.payload[2] << 8) |
                       response.payload[3];
        
        seed.seed.assign(response.payload.begin() + 4, response.payload.end());
        
        return seed;
    }

    SecResult verifyKey(SecLevel level, uint32_t seed_id, 
                       const std::vector<uint8_t>& key) override {
        if (!isConnected()) {
            return SecResult::SERVICE_UNAVAILABLE;
        }

        // Build SOME/IP request for verifyKey
        SomeIpMessage request;
        request.header.service_id = sec_service_config_.service_id;
        request.header.method_id = METHOD_VERIFY_KEY;
        request.header.message_type = MessageType::REQUEST;
        
        // Payload: security level (1 byte) + seed_id (4 bytes) + key (variable)
        request.payload.push_back(static_cast<uint8_t>(level));
        request.payload.push_back((seed_id >> 24) & 0xFF);
        request.payload.push_back((seed_id >> 16) & 0xFF);
        request.payload.push_back((seed_id >> 8) & 0xFF);
        request.payload.push_back(seed_id & 0xFF);
        request.payload.insert(request.payload.end(), key.begin(), key.end());

        // Send request and receive response
        SomeIpMessage response = someip_proxy_->sendRequest(request);
        
        // Check response
        if (response.header.return_code == ReturnCode::E_OK) {
            return SecResult::SUCCESS;
        }
        
        // Map NRC to SecResult
        if (response.payload.size() >= 1) {
            uint8_t nrc = response.payload[0];
            if (nrc == NRC_INVALID_KEY) {
                return SecResult::INVALID_KEY;
            }
        }
        
        return SecResult::UNKNOWN_ERROR;
    }

    void setRequestTimeout(uint32_t timeout_ms) override {
        if (someip_proxy_) {
            someip_proxy_->setRequestTimeout(timeout_ms);
        }
    }

    uint32_t getRequestTimeout() const override {
        if (someip_proxy_) {
            return someip_proxy_->getRequestTimeout();
        }
        return 0;
    }

private:
    SomeIpServiceConfig sec_service_config_;
    std::unique_ptr<SomeIpClientProxy> someip_proxy_;
};

std::unique_ptr<CgwSecClientProxy> createCgwSecClientProxy() {
    return std::make_unique<CgwSecClientProxyImpl>();
}

} // namespace cgw_diag