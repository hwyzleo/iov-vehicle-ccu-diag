#include "fota_someip_service.h"
#include "constants.h"
#include <nlohmann/json.hpp>
#include <iostream>

namespace cgw_diag {

FotaSomeIpService::FotaSomeIpService(std::shared_ptr<FotaFacade> facade,
                                     std::unique_ptr<SomeIpService> service)
    : facade_(std::move(facade)), service_(std::move(service)) {
}

bool FotaSomeIpService::initialize(const SomeIpServiceConfig& config) {
    if (!service_->initialize(config)) {
        return false;
    }
    registerMethodHandlers();
    return true;
}

bool FotaSomeIpService::start() {
    return service_->start();
}

void FotaSomeIpService::stop() {
    service_->stop();
}

bool FotaSomeIpService::isRunning() const {
    return service_->isRunning();
}

bool FotaSomeIpService::offerService() {
    return service_->offerService();
}

void FotaSomeIpService::stopOfferService() {
    service_->stopOfferService();
}

uint16_t FotaSomeIpService::getBoundPort() const {
    return service_->getBoundPort();
}

void FotaSomeIpService::registerMethodHandlers() {
    service_->registerMethodHandler(
        constants::METHOD_COLLECT_VEHICLE_INVENTORY,
        [this](const SomeIpMessage& request) {
            return handleCollectVehicleInventory(request);
        });

    service_->registerMethodHandler(
        constants::METHOD_GET_ECU_VERSION,
        [this](const SomeIpMessage& request) {
            return handleGetEcuVersion(request);
        });

    service_->registerMethodHandler(
        constants::METHOD_GET_REGISTRY_VERSION,
        [this](const SomeIpMessage& request) {
            return handleGetRegistryVersion(request);
        });

    service_->registerMethodHandler(
        constants::METHOD_READ_VIN,
        [this](const SomeIpMessage& request) {
            return handleReadVin(request);
        });
}

SomeIpMessage FotaSomeIpService::handleCollectVehicleInventory(const SomeIpMessage& request) {
    SomeIpMessage response;
    response.header.service_id = request.header.service_id;
    response.header.method_id = request.header.method_id;
    response.header.message_type = MessageType::RESPONSE;

    try {
        auto snapshot = facade_->collectVehicleInventory();

        nlohmann::json j;
        j["vin"] = snapshot.vin;
        j["registry_version"] = snapshot.registry_version;
        j["collected_at"] = snapshot.collected_at;

        std::string vin_source_str;
        switch (snapshot.vin_source) {
            case VinSource::PROVISIONED: vin_source_str = "PROVISIONED"; break;
            case VinSource::UNKNOWN: vin_source_str = "UNKNOWN"; break;
        }
        j["vin_source"] = vin_source_str;

        std::string baseline_source_str;
        switch (snapshot.baseline_source) {
            case BaselineSource::FACTORY: baseline_source_str = "FACTORY"; break;
            case BaselineSource::LAST_OTA: baseline_source_str = "LAST_OTA"; break;
            case BaselineSource::UNKNOWN: baseline_source_str = "UNKNOWN"; break;
        }
        j["baseline_source"] = baseline_source_str;

        std::string overall_str;
        switch (snapshot.overall_result) {
            case CollectionStatus::ALL_OK: overall_str = "ALL_OK"; break;
            case CollectionStatus::PARTIAL: overall_str = "PARTIAL"; break;
            case CollectionStatus::FAILED: overall_str = "FAILED"; break;
        }
        j["overall_result"] = overall_str;

        std::string json_str = j.dump();
        response.payload.assign(json_str.begin(), json_str.end());
        response.header.return_code = ReturnCode::E_OK;
    } catch (const std::exception& e) {
        response.header.return_code = ReturnCode::E_NOT_OK;
    }

    return response;
}

SomeIpMessage FotaSomeIpService::handleGetEcuVersion(const SomeIpMessage& request) {
    SomeIpMessage response;
    response.header.service_id = request.header.service_id;
    response.header.method_id = request.header.method_id;
    response.header.message_type = MessageType::RESPONSE;

    try {
        std::string ecu_id(request.payload.begin(), request.payload.end());
        auto entry = facade_->getEcuVersion(ecu_id);

        nlohmann::json j;
        j["ecu_id"] = entry.ecu_id;

        std::string source_str;
        switch (entry.source) {
            case ReadMethod::UDS_0x22: source_str = "UDS_0x22"; break;
            case ReadMethod::SOMEIP_GET_VERSION: source_str = "SOMEIP_GET_VERSION"; break;
        }
        j["source"] = source_str;

        std::string status_str;
        switch (entry.status) {
            case EcuStatus::OK: status_str = "OK"; break;
            case EcuStatus::MISSING: status_str = "MISSING"; break;
            case EcuStatus::NRC: status_str = "NRC"; break;
            case EcuStatus::TIMEOUT: status_str = "TIMEOUT"; break;
            case EcuStatus::UNREACHABLE: status_str = "UNREACHABLE"; break;
            case EcuStatus::PARSE_ERROR: status_str = "PARSE_ERROR"; break;
        }
        j["status"] = status_str;

        if (entry.sw_version.has_value()) {
            j["sw_version"] = entry.sw_version.value();
        }
        if (entry.part_number.has_value()) {
            j["part_number"] = entry.part_number.value();
        }
        if (entry.hw_version.has_value()) {
            j["hw_version"] = entry.hw_version.value();
        }
        if (entry.error_code.has_value()) {
            j["error_code"] = entry.error_code.value();
        }

        std::string json_str = j.dump();
        response.payload.assign(json_str.begin(), json_str.end());
        response.header.return_code = ReturnCode::E_OK;
    } catch (const std::exception& e) {
        response.header.return_code = ReturnCode::E_NOT_OK;
    }

    return response;
}

SomeIpMessage FotaSomeIpService::handleGetRegistryVersion(const SomeIpMessage& request) {
    SomeIpMessage response;
    response.header.service_id = request.header.service_id;
    response.header.method_id = request.header.method_id;
    response.header.message_type = MessageType::RESPONSE;

    try {
        std::string version = facade_->getRegistryVersion();
        response.payload.assign(version.begin(), version.end());
        response.header.return_code = ReturnCode::E_OK;
    } catch (const std::exception& e) {
        response.header.return_code = ReturnCode::E_NOT_OK;
    }

    return response;
}

SomeIpMessage FotaSomeIpService::handleReadVin(const SomeIpMessage& request) {
    SomeIpMessage response;
    response.header.service_id = request.header.service_id;
    response.header.method_id = request.header.method_id;
    response.header.message_type = MessageType::RESPONSE;

    try {
        auto vin = facade_->readVin();
        
        if (vin.has_value()) {
            // Return raw VIN string (17 bytes) as SOME/IP payload
            response.payload.assign(vin.value().begin(), vin.value().end());
            response.header.return_code = ReturnCode::E_OK;
        } else {
            // VIN not provisioned - return error
            response.header.return_code = ReturnCode::E_NOT_OK;
        }
    } catch (const std::exception& e) {
        response.header.return_code = ReturnCode::E_NOT_OK;
    }

    return response;
}

} // namespace cgw_diag
