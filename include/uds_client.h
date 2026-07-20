#pragma once

#include "data_models.h"
#include "transport_adapter.h"
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace cgw_diag {

struct UdsResponse {
    bool success = false;
    std::vector<uint8_t> data;
    uint8_t nrc = 0;
    std::string error_message;
};

class UdsClient {
public:
    UdsClient(std::shared_ptr<TransportAdapter> transport, const DiagConfig& config);

    UdsResponse readDataByIdentifier(uint16_t logical_address, uint16_t did);

    std::vector<UdsResponse> readMultipleDids(uint16_t logical_address,
                                               const std::vector<uint16_t>& dids);

    std::optional<std::string> parseVersionData(const std::vector<uint8_t>& data,
                                                const std::string& encoding,
                                                uint16_t length);

private:
    std::shared_ptr<TransportAdapter> transport_;
    DiagConfig config_;

    std::vector<uint8_t> createReadDataByIdentifierRequest(uint16_t did);
    bool isPositiveResponse(const std::vector<uint8_t>& response);
    std::optional<uint8_t> extractNrc(const std::vector<uint8_t>& response);
    std::vector<uint8_t> extractData(const std::vector<uint8_t>& response);
};

} // namespace cgw_diag
