#pragma once

#include "someip_types.h"

namespace cgw_diag {
namespace tests {

/**
 * @brief 获取可用的 SomeIP 配置
 * @return 端口为 0 的配置（系统自动分配）
 */
inline SomeIpServiceConfig getAvailableSomeIpConfig() {
    SomeIpServiceConfig config;
    config.service_id = 0x1110;  // CGW-DIAG-DSN-CR-007
    config.instance_id = 0x0001;
    config.major_version = 1;
    config.minor_version = 0;
    config.ip_address = "127.0.0.1";
    config.port = 0;  // 系统分配
    return config;
}

} // namespace tests
} // namespace cgw_diag
