#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <functional>

namespace ccu_diag {

// SOME/IP Service ID
using ServiceId = uint16_t;

// SOME/IP Method ID
using MethodId = uint16_t;

// SOME/IP Event ID
using EventId = uint16_t;

// SOME/IP Client ID
using ClientId = uint16_t;

// SOME/IP Session ID
using SessionId = uint16_t;

// SOME/IP Message Type
enum class MessageType : uint8_t {
    REQUEST = 0x00,
    REQUEST_NO_RETURN = 0x01,
    NOTIFICATION = 0x02,
    RESPONSE = 0x80,
    ERROR = 0x81
};

// SOME/IP Return Code
enum class ReturnCode : uint8_t {
    E_OK = 0x00,
    E_NOT_OK = 0x01,
    E_UNKNOWN_SERVICE = 0x02,
    E_UNKNOWN_METHOD = 0x03,
    E_NOT_READY = 0x04,
    E_NOT_REACHABLE = 0x05,
    E_TIMEOUT = 0x06,
    E_WRONG_PROTOCOL_VERSION = 0x07,
    E_WRONG_MESSAGE_TYPE = 0x08
};

// SOME/IP Message Header
struct SomeIpHeader {
    ServiceId service_id = 0;
    MethodId method_id = 0;
    uint32_t length = 0;
    ClientId client_id = 0;
    SessionId session_id = 0;
    uint8_t protocol_version = 0x01;
    uint8_t interface_version = 0x01;
    MessageType message_type = MessageType::REQUEST;
    ReturnCode return_code = ReturnCode::E_OK;
};

// SOME/IP Message
struct SomeIpMessage {
    SomeIpHeader header;
    std::vector<uint8_t> payload;
};

// SOME/IP Service Configuration
struct SomeIpServiceConfig {
    ServiceId service_id = 0;
    uint16_t instance_id = 0;
    uint8_t major_version = 1;
    uint32_t minor_version = 0;
    std::string ip_address;
    uint16_t port = 0;
    // 本进程 vsomeip application 名（必须在平台 vsomeip.json 的 applications
    // 列表中登记，如 ccu-diag/0x1110）。空值表示使用
    // constants::DEFAULT_SOMEIP_APPLICATION_NAME。见 someip_backend.h。
    std::string application_name;
};

// SOME/IP Method Configuration
struct SomeIpMethodConfig {
    MethodId method_id = 0;
    uint8_t major_version = 1;
};

// SOME/IP Event Configuration
struct SomeIpEventConfig {
    EventId event_id = 0;
    uint8_t major_version = 1;
};

// Callback types
using SomeIpRequestHandler = std::function<SomeIpMessage(const SomeIpMessage& request)>;
using SomeIpNotificationHandler = std::function<void(const SomeIpMessage& notification)>;

} // namespace ccu_diag
