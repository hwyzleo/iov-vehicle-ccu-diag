#include "ccu_sec_client_proxy.h"
#include "someip_client_proxy.h"
#include "diag_log_adapter.h"
#include "diag_log_context.h"
#include <stdexcept>

namespace ccu_diag {

namespace {
const char* secResultToString(SecResult result) {
    switch (result) {
        case SecResult::SUCCESS:            return "SUCCESS";
        case SecResult::INVALID_KEY:        return "INVALID_KEY";
        case SecResult::INVALID_SEED:       return "INVALID_SEED";
        case SecResult::SERVICE_UNAVAILABLE:return "SERVICE_UNAVAILABLE";
        case SecResult::TIMEOUT:            return "TIMEOUT";
        case SecResult::UNKNOWN_ERROR:      return "UNKNOWN_ERROR";
        default: return "UNKNOWN";
    }
}
} // namespace

// CCU-SEC service method IDs (from design document)
constexpr MethodId METHOD_GET_SEED = 0x0001;
constexpr MethodId METHOD_VERIFY_KEY = 0x0002;

// UDS NRC codes from CCU-SEC (application-level, in SOME/IP payload)
constexpr uint8_t NRC_POSITIVE_RESPONSE = 0x00;
constexpr uint8_t NRC_INVALID_KEY = 0x35;

// SOME/IP header size: service_id(2) + method_id(2) + length(4) = 8 bytes
constexpr uint32_t SOMEIP_HEADER_SIZE = 8;

class CcuSecClientProxyImpl : public CcuSecClientProxy {
public:
    CcuSecClientProxyImpl() = default;

    explicit CcuSecClientProxyImpl(std::unique_ptr<SomeIpClientProxy> proxy)
        : someip_proxy_(std::move(proxy)) {}

    ~CcuSecClientProxyImpl() override = default;

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
            DiagLogAdapter::sec_client().warn(diag_events::SECURITY_ACCESS_FAILED,
                "getSeed failed: not connected",
                {dlog::f_str("security_level", "LEVEL_1"),
                 dlog::f_str("operation", "getSeed"),
                 dlog::f_str("result", "not_connected")}
            );
            return std::nullopt;
        }

        // CCU-DIAG-DSN-CR-008: DIAG 调用 CCU-SEC 时创建 SOME/IP Client 子作用域
        auto scope = make_someip_child_scope(
            hex_id(sec_service_config_.service_id),
            hex_id(METHOD_GET_SEED)
        );

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
            DiagLogAdapter::sec_client().warn(diag_events::SECURITY_ACCESS_FAILED,
                "getSeed failed: request exception",
                {dlog::f_str("security_level", "LEVEL_1"),
                 dlog::f_str("operation", "getSeed"),
                 dlog::f_str("result", "exception")}
            );
            return std::nullopt;
        }

        if (response.header.return_code != ReturnCode::E_OK) {
            DiagLogAdapter::sec_client().warn(diag_events::SECURITY_ACCESS_FAILED,
                "getSeed failed: SOME/IP error",
                {dlog::f_str("security_level", "LEVEL_1"),
                 dlog::f_str("operation", "getSeed"),
                 dlog::f_str("result", "someip_error")}
            );
            return std::nullopt;
        }

        if (response.payload.size() < 5) {
            DiagLogAdapter::sec_client().warn(diag_events::SECURITY_ACCESS_FAILED,
                "getSeed failed: invalid response",
                {dlog::f_str("security_level", "LEVEL_1"),
                 dlog::f_str("operation", "getSeed"),
                 dlog::f_str("result", "invalid_response")}
            );
            return std::nullopt;
        }

        SecuritySeed seed;
        seed.seed_id = (static_cast<uint32_t>(response.payload[0]) << 24) |
                       (static_cast<uint32_t>(response.payload[1]) << 16) |
                       (static_cast<uint32_t>(response.payload[2]) << 8) |
                       static_cast<uint32_t>(response.payload[3]);
        seed.seed.assign(response.payload.begin() + 4, response.payload.end());

        // CCU-DIAG-DSN-CR-008: 仅记录非密钥元数据（seed_id、seed 长度），
        // 不记录 seed 原值
        DiagLogAdapter::sec_client().info("diag.security.get_seed.succeeded",
            "getSeed succeeded",
            {dlog::f_str("security_level", "LEVEL_1"),
             dlog::f_str("operation", "getSeed"),
             dlog::f_int("seed_id", static_cast<int64_t>(seed.seed_id)),
             dlog::f_int("seed_length", static_cast<int64_t>(seed.seed.size())),
             dlog::f_str("someip_service_id", hex_id(sec_service_config_.service_id)),
             dlog::f_str("someip_method_id", hex_id(METHOD_GET_SEED))}
        );

        return seed;
    }

    SecResult verifyKey(SecLevel level, uint32_t seed_id,
                        const std::vector<uint8_t>& key) override {
        if (!isConnected()) {
            return SecResult::SERVICE_UNAVAILABLE;
        }

        // CCU-DIAG-DSN-CR-008: DIAG 调用 CCU-SEC 时创建 SOME/IP Client 子作用域
        auto scope = make_someip_child_scope(
            hex_id(sec_service_config_.service_id),
            hex_id(METHOD_VERIFY_KEY)
        );

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
            DiagLogAdapter::sec_client().warn(diag_events::SECURITY_ACCESS_FAILED,
                "verifyKey failed: request exception",
                {dlog::f_str("security_level", "LEVEL_1"),
                 dlog::f_str("operation", "verifyKey"),
                 dlog::f_int("seed_id", static_cast<int64_t>(seed_id)),
                 dlog::f_str("result", "exception")}
            );
            return SecResult::SERVICE_UNAVAILABLE;
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
            // CCU-DIAG-DSN-CR-008: verifyKey 成功（不记录 key 原值）
            DiagLogAdapter::sec_client().info("diag.security.verify_key.succeeded",
                "verifyKey succeeded",
                {dlog::f_str("security_level", "LEVEL_1"),
                 dlog::f_str("operation", "verifyKey"),
                 dlog::f_int("seed_id", static_cast<int64_t>(seed_id)),
                 dlog::f_str("result", "SUCCESS"),
                 dlog::f_str("someip_service_id", hex_id(sec_service_config_.service_id)),
                 dlog::f_str("someip_method_id", hex_id(METHOD_VERIFY_KEY))}
            );
            return SecResult::SUCCESS;
        }
        if (nrc == NRC_INVALID_KEY) {
            DiagLogAdapter::sec_client().warn(diag_events::SECURITY_ACCESS_FAILED,
                "verifyKey failed: invalid key",
                {dlog::f_str("security_level", "LEVEL_1"),
                 dlog::f_str("operation", "verifyKey"),
                 dlog::f_int("seed_id", static_cast<int64_t>(seed_id)),
                 dlog::f_str("result", secResultToString(SecResult::INVALID_KEY))}
            );
            return SecResult::INVALID_KEY;
        }

        DiagLogAdapter::sec_client().warn(diag_events::SECURITY_ACCESS_FAILED,
            "verifyKey failed: unknown NRC",
            {dlog::f_str("security_level", "LEVEL_1"),
             dlog::f_str("operation", "verifyKey"),
             dlog::f_int("seed_id", static_cast<int64_t>(seed_id)),
             dlog::f_str("result", secResultToString(SecResult::UNKNOWN_ERROR))}
        );
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

std::unique_ptr<CcuSecClientProxy> createCcuSecClientProxy() {
    return std::make_unique<CcuSecClientProxyImpl>();
}

std::unique_ptr<CcuSecClientProxy> createCcuSecClientProxy(
        std::unique_ptr<SomeIpClientProxy> proxy) {
    return std::make_unique<CcuSecClientProxyImpl>(std::move(proxy));
}

} // namespace ccu_diag
