#pragma once

#include "someip_types.h"
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace cgw_diag {

class SomeIpClientProxy;

enum class SecLevel : uint8_t {
    LEVEL_1 = 0x01
};

enum class SecResult {
    SUCCESS,
    INVALID_KEY,
    INVALID_SEED,
    SERVICE_UNAVAILABLE,
    TIMEOUT,
    UNKNOWN_ERROR
};

struct SecuritySeed {
    uint32_t seed_id = 0;
    std::vector<uint8_t> seed;
};

class CgwSecClientProxy {
public:
    virtual ~CgwSecClientProxy() = default;

    virtual bool initialize(const SomeIpServiceConfig& sec_service_config) = 0;
    virtual bool connect() = 0;
    virtual void disconnect() = 0;
    virtual bool isConnected() const = 0;

    virtual std::optional<SecuritySeed> getSeed(SecLevel level) = 0;
    virtual SecResult verifyKey(SecLevel level, uint32_t seed_id,
                                const std::vector<uint8_t>& key) = 0;

    virtual void setRequestTimeout(uint32_t timeout_ms) = 0;
    virtual uint32_t getRequestTimeout() const = 0;
};

std::unique_ptr<CgwSecClientProxy> createCgwSecClientProxy();
std::unique_ptr<CgwSecClientProxy> createCgwSecClientProxy(
    std::unique_ptr<SomeIpClientProxy> proxy);

} // namespace cgw_diag
