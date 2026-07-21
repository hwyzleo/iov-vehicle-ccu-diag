#pragma once

#include "someip_types.h"
#include <memory>
#include <string>

namespace cgw_diag {

class SomeIpClientProxy {
public:
    virtual ~SomeIpClientProxy() = default;

    virtual bool initialize(const SomeIpServiceConfig& remote_service_config) = 0;
    virtual bool connect() = 0;
    virtual void disconnect() = 0;
    virtual bool isConnected() const = 0;

    virtual SomeIpMessage sendRequest(const SomeIpMessage& request) = 0;
    virtual bool sendNotification(const SomeIpMessage& notification) = 0;

    virtual void setRequestTimeout(uint32_t timeout_ms) = 0;
    virtual uint32_t getRequestTimeout() const = 0;
};

// Factory function
std::unique_ptr<SomeIpClientProxy> createSomeIpClientProxy();

} // namespace cgw_diag
