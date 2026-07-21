#include "cgw_sec_client_proxy.h"
#include "someip_client_proxy.h"
#include <stdexcept>

namespace cgw_diag {

// CGW-SEC service method IDs (from design document)
constexpr MethodId METHOD_GET_SEED = 0x0001;
constexpr MethodId METHOD_VERIFY_KEY = 0x0002;

// UDS NRC codes from CGW-SEC (application-level, in SOME/IP payload)
constexpr uint8_t NRC_POSITIVE_RESPONSE = 0x00;
constexpr uint8_t NRC_INVALID_KEY = 0x35;

// SOME/IP header size: service_id(2) + method_id(2) + length(4) = 8 bytes
constexpr uint32_t SOMEIP_HEADER_SIZE = 8;

class CgwSecClientProxyImpl : public CgwSecClientProxy {
public:
    CgwSecClientProxyImpl() = default;

    explicit CgwSecClientProxyImpl(std::unique_ptr<SomeIpClientProxy> proxy)
        : someip_proxy_(std::move(proxy)) {}

    ~CgwSecClientProxyImpl() override = default;

    bool initialize(const SomeIpServiceConfig& sec_service_config) override {
        sec_service_config_ = sec_service_config;
        if (!someip_proxy_) {
            someip_proxy_ = createSomeIpClientProxy();
            if (!someip_proxy_) {
                return false;
            }
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

        SomeIpMessage request;
        request.header.service_id = sec_service_config_.service_id;
        request.header.method_id = METHOD_GET_SEED;
        request.header.message_type = MessageType::REQUEST;
        request.payload.push_back(static_cast<uint8_t>(level));
        request.header.length = SOMEIP_HEADER_SIZE + request.payload.size();

        SomeIpMessage response;
        try {
            response = someip_proxy_->sendRequest(request);
        } catch (...) {
            return std::nullopt;
        }

        if (response.header.return_code != ReturnCode::E_OK) {
            return std::nullopt;
        }

        if (response.payload.size() < 5) {
            return std::nullopt;
        }

        SecuritySeed seed;
        seed.seed_id = (static_cast<uint32_t>(response.payload[0]) << 24) |
                       (static_cast<uint32_t>(response.payload[1]) << 16) |
                       (static_cast<uint32_t>(response.payload[2]) << 8) |
                       static_cast<uint32_t>(response.payload[3]);
        seed.seed.assign(response.payload.begin() + 4, response.payload.end());

        return seed;
    }

    SecResult verifyKey(SecLevel level, uint32_t seed_id,
                        const std::vector<uint8_t>& key) override {
        if (!isConnected()) {
            return SecResult::SERVICE_UNAVAILABLE;
        }

        SomeIpMessage request;
        request.header.service_id = sec_service_config_.service_id;
        request.header.method_id = METHOD_VERIFY_KEY;
        request.header.message_type = MessageType::REQUEST;
        request.payload.push_back(static_cast<uint8_t>(level));
        request.payload.push_back((seed_id >> 24) & 0xFF);
        request.payload.push_back((seed_id >> 16) & 0xFF);
        request.payload.push_back((seed_id >> 8) & 0xFF);
        request.payload.push_back(seed_id & 0xFF);
        request.payload.insert(request.payload.end(), key.begin(), key.end());
        request.header.length = SOMEIP_HEADER_SIZE + request.payload.size();

        SomeIpMessage response;
        try {
            response = someip_proxy_->sendRequest(request);
        } catch (...) {
            return SecResult::UNKNOWN_ERROR;
        }

        // Transport-level errors: SOME/IP return_code indicates failure
        if (response.header.return_code != ReturnCode::E_OK) {
            switch (response.header.return_code) {
                case ReturnCode::E_TIMEOUT:
                    return SecResult::TIMEOUT;
                case ReturnCode::E_NOT_REACHABLE:
                case ReturnCode::E_NOT_READY:
                    return SecResult::SERVICE_UNAVAILABLE;
                default:
                    return SecResult::UNKNOWN_ERROR;
            }
        }

        // Application-level response: check UDS NRC in payload
        if (response.payload.empty()) {
            return SecResult::UNKNOWN_ERROR;
        }

        uint8_t nrc = response.payload[0];
        if (nrc == NRC_POSITIVE_RESPONSE) {
            return SecResult::SUCCESS;
        }
        if (nrc == NRC_INVALID_KEY) {
            return SecResult::INVALID_KEY;
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

std::unique_ptr<CgwSecClientProxy> createCgwSecClientProxy(
        std::unique_ptr<SomeIpClientProxy> proxy) {
    return std::make_unique<CgwSecClientProxyImpl>(std::move(proxy));
}

} // namespace cgw_diag
