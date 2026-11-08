#include "fota_someip_service.h"
#include "constants.h"
#include "diag_log_adapter.h"
#include "diag_log_context.h"
#include "diag_payload_codec.h"
#include "error_codes.h"
#include <nlohmann/json.hpp>
#include <chrono>
#include <iostream>

namespace ccu_diag {

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

    service_->registerMethodHandler(
        constants::METHOD_RECORD_INSTALLED_VERSIONS,
        [this](const SomeIpMessage& request) {
            return handleRecordInstalledVersions(request);
        });
}

SomeIpMessage FotaSomeIpService::handleCollectVehicleInventory(const SomeIpMessage& request) {
    // CCU-DIAG-DSN-CR-008: SOME/IP Provider 收到 FOTA 请求时建立 ContextScope
    auto trace_id = generate_trace_id();
    auto request_id = generate_request_id();
    auto scope = make_someip_context_scope(
        trace_id, request_id,
        hex_id(request.header.service_id),
        hex_id(request.header.method_id),
        hex_id(request.header.client_id)
    );

    DiagLogAdapter::facade().info(diag_events::INVENTORY_REQUEST_ACCEPTED,
        "FOTA inventory request accepted",
        {dlog::f_str("request_id", request_id),
         dlog::f_str("someip_service_id", hex_id(request.header.service_id)),
         dlog::f_str("someip_method_id", hex_id(request.header.method_id))}
    );

    SomeIpMessage response;
    response.header.service_id = request.header.service_id;
    response.header.method_id = request.header.method_id;
    response.header.message_type = MessageType::RESPONSE;

    try {
        auto snapshot = facade_->collectVehicleInventory();
        if (snapshot.ecu_list.empty()) {
            // Never answer "success + empty list" (CCU-DIAG-DSN-CR-010 D9).
            throw DiagException(ErrorCode::INVENTORY_STATE_UNAVAILABLE,
                                "inventory snapshot is empty");
        }

        const std::string json_str = DiagPayloadCodec::encodeInventoryV1(snapshot);
        response.payload.assign(json_str.begin(), json_str.end());
        response.header.return_code = ReturnCode::E_OK;
    } catch (const DiagException& e) {
        const std::string diag_code = ErrorCodeMapper::toDiagCode(e.code());
        DiagLogAdapter::facade().error(diag_events::INVENTORY_LOAD_FAILED,
            "FOTA inventory request failed",
            {dlog::f_str("request_id", request_id),
             dlog::f_str("someip_method_id", hex_id(request.header.method_id)),
             dlog::f_str("error_code", diag_code),
             dlog::f_str("reason", e.what())}
        );
        const std::string json_str = DiagPayloadCodec::encodeInventoryError(diag_code);
        response.payload.assign(json_str.begin(), json_str.end());
        response.header.return_code = ReturnCode::E_NOT_OK;
    } catch (const std::exception& e) {
        DiagLogAdapter::facade().error(diag_events::INVENTORY_LOAD_FAILED,
            "FOTA inventory request failed",
            {dlog::f_str("request_id", request_id),
             dlog::f_str("someip_method_id", hex_id(request.header.method_id)),
             dlog::f_str("error_code",
                 ErrorCodeMapper::toDiagCode(ErrorCode::INVENTORY_STATE_UNAVAILABLE)),
             dlog::f_str("reason", e.what())}
        );
        const std::string json_str = DiagPayloadCodec::encodeInventoryError(
            ErrorCodeMapper::toDiagCode(ErrorCode::INVENTORY_STATE_UNAVAILABLE));
        response.payload.assign(json_str.begin(), json_str.end());
        response.header.return_code = ReturnCode::E_NOT_OK;
    }

    return response;
}

SomeIpMessage FotaSomeIpService::handleRecordInstalledVersions(
    const SomeIpMessage& request) {
    auto trace_id = generate_trace_id();
    auto request_id = generate_request_id();
    auto scope = make_someip_context_scope(
        trace_id, request_id,
        hex_id(request.header.service_id),
        hex_id(request.header.method_id),
        hex_id(request.header.client_id)
    );

    const auto start = std::chrono::steady_clock::now();

    SomeIpMessage response;
    response.header.service_id = request.header.service_id;
    response.header.method_id = request.header.method_id;
    response.header.message_type = MessageType::RESPONSE;

    InstalledVersionsRequest parsed;
    std::string decode_error;
    const std::string payload_text(request.payload.begin(), request.payload.end());

    InstalledVersionsResponse result;
    if (!DiagPayloadCodec::decodeRegistrationRequest(payload_text, parsed,
                                                     decode_error)) {
        result.schema_version = constants::DIAG_PAYLOAD_SCHEMA_VERSION;
        result.disposition = RegistrationDisposition::REJECTED_INVALID;
        result.error_code = ErrorCodeMapper::toDiagCode(ErrorCode::REGISTRATION_INVALID);
        result.rejected_items.push_back(RejectedVersionItem{"", "INVALID_REQUEST"});
    } else {
        DiagLogAdapter::fota_registrar().info(diag_events::REGISTRATION_REQUESTED,
            "Installed version registration requested",
            {dlog::f_str("request_id", request_id),
             dlog::f_str("execution_id", parsed.execution_id),
             dlog::f_int("attempt_no", static_cast<int64_t>(parsed.attempt_no)),
             dlog::f_int("item_count", static_cast<int64_t>(parsed.items.size()))}
        );
        result = facade_->recordInstalledVersions(parsed);
    }

    const auto end = std::chrono::steady_clock::now();
    const auto duration_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        end - start).count();
    const auto rejected_count = static_cast<int64_t>(result.rejected_items.size());

    switch (result.disposition) {
        case RegistrationDisposition::APPLIED:
            DiagLogAdapter::fota_registrar().info(diag_events::REGISTRATION_APPLIED,
                "Installed versions registered",
                {dlog::f_str("execution_id", parsed.execution_id),
                 dlog::f_int("attempt_no", static_cast<int64_t>(parsed.attempt_no)),
                 dlog::f_int("applied_count", static_cast<int64_t>(result.applied_count)),
                 dlog::f_str("registration_id", result.registration_id),
                 dlog::f_int("duration_ms", duration_ms)}
            );
            DiagLogAdapter::inventory_store().info(diag_events::INVENTORY_UPDATED,
                "ECU inventory updated by registration",
                {dlog::f_str("execution_id", parsed.execution_id),
                 dlog::f_int("attempt_no", static_cast<int64_t>(parsed.attempt_no)),
                 dlog::f_int("applied_count", static_cast<int64_t>(result.applied_count)),
                 dlog::f_str("effective_updated_at", result.effective_updated_at)}
            );
            break;
        case RegistrationDisposition::DUPLICATE:
            DiagLogAdapter::fota_registrar().info(diag_events::REGISTRATION_DUPLICATE,
                "Installed versions already registered (idempotent replay)",
                {dlog::f_str("execution_id", parsed.execution_id),
                 dlog::f_int("attempt_no", static_cast<int64_t>(parsed.attempt_no)),
                 dlog::f_str("registration_id", result.registration_id),
                 dlog::f_int("duration_ms", duration_ms)}
            );
            break;
        case RegistrationDisposition::REJECTED_INVALID:
            DiagLogAdapter::fota_registrar().error(diag_events::REGISTRATION_REJECTED,
                "Installed version registration rejected",
                {dlog::f_str("execution_id", parsed.execution_id),
                 dlog::f_int("attempt_no", static_cast<int64_t>(parsed.attempt_no)),
                 dlog::f_int("rejected_count", rejected_count),
                 dlog::f_str("error_code", result.error_code),
                 dlog::f_str("reason", decode_error),
                 dlog::f_int("duration_ms", duration_ms)}
            );
            break;
        case RegistrationDisposition::RETRYABLE_FAILED:
            DiagLogAdapter::fota_registrar().warn(
                diag_events::REGISTRATION_RETRYABLE_FAILED,
                "Installed version registration failed, retryable",
                {dlog::f_str("execution_id", parsed.execution_id),
                 dlog::f_int("attempt_no", static_cast<int64_t>(parsed.attempt_no)),
                 dlog::f_str("error_code", result.error_code),
                 dlog::f_int("duration_ms", duration_ms)}
            );
            break;
    }

    const std::string json_str = DiagPayloadCodec::encodeRegistrationResponse(result);
    response.payload.assign(json_str.begin(), json_str.end());
    response.header.return_code = ReturnCode::E_OK;
    return response;
}

SomeIpMessage FotaSomeIpService::handleGetEcuVersion(const SomeIpMessage& request) {
    // CCU-DIAG-DSN-CR-008: 建立请求上下文
    auto trace_id = generate_trace_id();
    auto request_id = generate_request_id();
    auto scope = make_someip_context_scope(
        trace_id, request_id,
        hex_id(request.header.service_id),
        hex_id(request.header.method_id),
        hex_id(request.header.client_id)
    );

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
    // CCU-DIAG-DSN-CR-008: 建立请求上下文
    auto trace_id = generate_trace_id();
    auto request_id = generate_request_id();
    auto scope = make_someip_context_scope(
        trace_id, request_id,
        hex_id(request.header.service_id),
        hex_id(request.header.method_id),
        hex_id(request.header.client_id)
    );

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
    // CCU-DIAG-DSN-CR-008: 建立请求上下文
    auto trace_id = generate_trace_id();
    auto request_id = generate_request_id();
    auto scope = make_someip_context_scope(
        trace_id, request_id,
        hex_id(request.header.service_id),
        hex_id(request.header.method_id),
        hex_id(request.header.client_id)
    );

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

} // namespace ccu_diag
