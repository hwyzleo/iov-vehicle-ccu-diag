#pragma once

#include "transport_adapter.h"
#include <functional>
#include <vector>
#include <memory>

namespace cgw_diag {
namespace tests {

class MockTransport : public TransportAdapter {
public:
    using RequestHandler = std::function<TransportResponse(uint16_t, const std::vector<uint8_t>&)>;

    MockTransport() = default;

    void setRequestHandler(RequestHandler handler) {
        handler_ = std::move(handler);
    }

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
        return reachable_;
    }

    void setReachable(bool reachable) {
        reachable_ = reachable;
    }

    std::string getTransportType() const override {
        return transport_type_;
    }

    void setTransportType(const std::string& type) {
        transport_type_ = type;
    }

private:
    RequestHandler handler_;
    bool reachable_ = true;
    std::string transport_type_ = "Mock";
};

} // namespace tests
} // namespace cgw_diag