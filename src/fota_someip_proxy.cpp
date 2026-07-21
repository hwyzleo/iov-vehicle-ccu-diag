#include "fota_someip_proxy.h"
#include "constants.h"
#include <nlohmann/json.hpp>

namespace cgw_diag {

FotaSomeIpProxy::FotaSomeIpProxy(std::unique_ptr<SomeIpClientProxy> proxy)
    : proxy_(std::move(proxy)) {
}

bool FotaSomeIpProxy::initialize(const SomeIpServiceConfig& remote_service_config) {
    return proxy_->initialize(remote_service_config);
}

bool FotaSomeIpProxy::connect() {
    return proxy_->connect();
}

void FotaSomeIpProxy::disconnect() {
    proxy_->disconnect();
}

bool FotaSomeIpProxy::isConnected() const {
    return proxy_->isConnected();
}

VehicleSoftwareSnapshot FotaSomeIpProxy::collectVehicleInventory() {
    VehicleSoftwareSnapshot snapshot;
    snapshot.overall_result = CollectionStatus::FAILED;

    if (!proxy_->isConnected()) {
        return snapshot;
    }

    SomeIpMessage request;
    request.header.service_id = constants::CGW_DIAG_SERVICE_ID;
    request.header.method_id = constants::METHOD_COLLECT_VEHICLE_INVENTORY;
    request.header.message_type = MessageType::REQUEST;

    auto response = proxy_->sendRequest(request);

    if (response.header.return_code == ReturnCode::E_OK) {
        try {
            std::string json_str(response.payload.begin(), response.payload.end());
            nlohmann::json j = nlohmann::json::parse(json_str);

            snapshot.vin = j.value("vin", "");
            snapshot.registry_version = j.value("registry_version", "");
            snapshot.collected_at = j.value("collected_at", "");

            std::string vin_source_str = j.value("vin_source", "UNKNOWN");
            if (vin_source_str == "PROVISIONED") {
                snapshot.vin_source = VinSource::PROVISIONED;
            } else {
                snapshot.vin_source = VinSource::UNKNOWN;
            }

            std::string baseline_source_str = j.value("baseline_source", "UNKNOWN");
            if (baseline_source_str == "FACTORY") {
                snapshot.baseline_source = BaselineSource::FACTORY;
            } else if (baseline_source_str == "LAST_OTA") {
                snapshot.baseline_source = BaselineSource::LAST_OTA;
            } else {
                snapshot.baseline_source = BaselineSource::UNKNOWN;
            }

            std::string overall_str = j.value("overall_result", "FAILED");
            if (overall_str == "ALL_OK") {
                snapshot.overall_result = CollectionStatus::ALL_OK;
            } else if (overall_str == "PARTIAL") {
                snapshot.overall_result = CollectionStatus::PARTIAL;
            } else {
                snapshot.overall_result = CollectionStatus::FAILED;
            }
        } catch (const std::exception& e) {
            snapshot.overall_result = CollectionStatus::FAILED;
        }
    }

    return snapshot;
}

EcuVersionEntry FotaSomeIpProxy::getEcuVersion(const std::string& ecu_id) {
    EcuVersionEntry entry;
    entry.ecu_id = ecu_id;
    entry.status = EcuStatus::MISSING;

    if (!proxy_->isConnected()) {
        return entry;
    }

    SomeIpMessage request;
    request.header.service_id = constants::CGW_DIAG_SERVICE_ID;
    request.header.method_id = constants::METHOD_GET_ECU_VERSION;
    request.header.message_type = MessageType::REQUEST;
    request.payload.assign(ecu_id.begin(), ecu_id.end());

    auto response = proxy_->sendRequest(request);

    if (response.header.return_code == ReturnCode::E_OK) {
        try {
            std::string json_str(response.payload.begin(), response.payload.end());
            nlohmann::json j = nlohmann::json::parse(json_str);

            entry.ecu_id = j.value("ecu_id", ecu_id);

            std::string source_str = j.value("source", "UDS_0x22");
            if (source_str == "SOMEIP_GET_VERSION") {
                entry.source = ReadMethod::SOMEIP_GET_VERSION;
            } else {
                entry.source = ReadMethod::UDS_0x22;
            }

            std::string status_str = j.value("status", "MISSING");
            if (status_str == "OK") {
                entry.status = EcuStatus::OK;
            } else if (status_str == "NRC") {
                entry.status = EcuStatus::NRC;
            } else if (status_str == "TIMEOUT") {
                entry.status = EcuStatus::TIMEOUT;
            } else if (status_str == "UNREACHABLE") {
                entry.status = EcuStatus::UNREACHABLE;
            } else if (status_str == "PARSE_ERROR") {
                entry.status = EcuStatus::PARSE_ERROR;
            } else {
                entry.status = EcuStatus::MISSING;
            }

            if (j.contains("sw_version")) {
                entry.sw_version = j["sw_version"].get<std::string>();
            }
            if (j.contains("part_number")) {
                entry.part_number = j["part_number"].get<std::string>();
            }
            if (j.contains("hw_version")) {
                entry.hw_version = j["hw_version"].get<std::string>();
            }
            if (j.contains("error_code")) {
                entry.error_code = j["error_code"].get<std::string>();
            }
        } catch (const std::exception& e) {
            entry.status = EcuStatus::PARSE_ERROR;
        }
    }

    return entry;
}

std::string FotaSomeIpProxy::getRegistryVersion() {
    if (!proxy_->isConnected()) {
        return "";
    }

    SomeIpMessage request;
    request.header.service_id = constants::CGW_DIAG_SERVICE_ID;
    request.header.method_id = constants::METHOD_GET_REGISTRY_VERSION;
    request.header.message_type = MessageType::REQUEST;

    auto response = proxy_->sendRequest(request);

    if (response.header.return_code == ReturnCode::E_OK) {
        return std::string(response.payload.begin(), response.payload.end());
    }

    return "";
}

void FotaSomeIpProxy::setRequestTimeout(uint32_t timeout_ms) {
    proxy_->setRequestTimeout(timeout_ms);
}

} // namespace cgw_diag