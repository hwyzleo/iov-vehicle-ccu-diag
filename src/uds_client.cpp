#include "uds_client.h"
#include "constants.h"
#include <algorithm>
#include <cstdio>
#include <stdexcept>

namespace cgw_diag {

UdsClient::UdsClient(std::shared_ptr<TransportAdapter> transport, const DiagConfig& config)
    : transport_(transport), config_(config) {
}

UdsResponse UdsClient::readDataByIdentifier(uint16_t logical_address, uint16_t did) {
    UdsResponse response;

    try {
        std::vector<uint8_t> request = createReadDataByIdentifierRequest(did);

        for (uint8_t retry = 0; retry <= config_.max_retry_count; ++retry) {
            auto transport_response = transport_->sendRequest(logical_address, request);

            if (!transport_response.success) {
                response.success = false;
                response.error_message = transport_response.error_message;

                if (retry < config_.max_retry_count) {
                    continue;
                }
                return response;
            }

            if (isPositiveResponse(transport_response.data)) {
                response.success = true;
                response.data = extractData(transport_response.data);
                return response;
            } else {
                response.success = false;
                response.nrc = extractNrc(transport_response.data);
                response.error_message = "Negative response received";
                return response;
            }
        }

    } catch (const std::exception& e) {
        response.success = false;
        response.error_message = e.what();
    }

    return response;
}

std::vector<UdsResponse> UdsClient::readMultipleDids(uint16_t logical_address,
                                                      const std::vector<uint16_t>& dids) {
    std::vector<UdsResponse> responses;

    for (uint16_t did : dids) {
        responses.push_back(readDataByIdentifier(logical_address, did));
    }

    return responses;
}

std::optional<std::string> UdsClient::parseVersionData(const std::vector<uint8_t>& data,
                                                       const std::string& encoding,
                                                       uint16_t length) {
    if (data.empty()) {
        return std::nullopt;
    }

    size_t bytes_to_read = std::min(static_cast<size_t>(length), data.size());

    if (encoding == "ASCII") {
        std::string result;
        for (size_t i = 0; i < bytes_to_read; ++i) {
            if (data[i] == 0) break;
            result += static_cast<char>(data[i]);
        }
        return result;
    } else if (encoding == "HEX") {
        std::string result;
        for (size_t i = 0; i < bytes_to_read; ++i) {
            char hex[3];
            snprintf(hex, sizeof(hex), "%02X", data[i]);
            result += hex;
        }
        return result;
    } else if (encoding == "BCD") {
        std::string result;
        for (size_t i = 0; i < bytes_to_read; ++i) {
            uint8_t high = (data[i] >> 4) & 0x0F;
            uint8_t low = data[i] & 0x0F;
            result += std::to_string(high);
            result += std::to_string(low);
        }
        return result;
    }

    return std::nullopt;
}

std::vector<uint8_t> UdsClient::createReadDataByIdentifierRequest(uint16_t did) {
    std::vector<uint8_t> request;
    request.push_back(constants::UDS_READ_DATA_BY_IDENTIFIER);
    request.push_back((did >> 8) & 0xFF);
    request.push_back(did & 0xFF);
    return request;
}

bool UdsClient::isPositiveResponse(const std::vector<uint8_t>& response) {
    if (response.empty()) {
        return false;
    }
    return response[0] == 0x62;
}

uint8_t UdsClient::extractNrc(const std::vector<uint8_t>& response) {
    if (response.size() >= 3 && response[0] == constants::UDS_NEGATIVE_RESPONSE) {
        return response[2];
    }
    return 0;
}

std::vector<uint8_t> UdsClient::extractData(const std::vector<uint8_t>& response) {
    if (response.size() > 3) {
        return std::vector<uint8_t>(response.begin() + 3, response.end());
    }
    return {};
}

} // namespace cgw_diag
