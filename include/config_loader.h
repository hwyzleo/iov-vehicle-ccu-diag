#pragma once

#include "data_models.h"
#include "someip_types.h"
#include "log_types.h"
#include <yaml-cpp/yaml.h>
#include <string>

namespace cgw_diag {

class ConfigLoader {
public:
    static DidRegistryConfig loadDidRegistry(const std::string& file_path);
    static DiagConfig loadDiagConfig(const std::string& file_path);
    static SomeIpServiceConfig loadSomeIpConfig(const std::string& file_path);

    // 加载结构化日志配置（CGW-DIAG-DSN-CR-008）
    // 从 YAML 读取 common.log.* 与 diag.log.*，返回 LogConfig
    static cgw::fw::log::LogConfig loadLogConfig(const std::string& file_path);
    
private:
    static EcuConfig parseEcuConfig(const YAML::Node& node);
    static DidEntry parseDidEntry(const YAML::Node& node);
    static void parseLogConfigNode(const YAML::Node& root, cgw::fw::log::LogConfig& log_config);
};

} // namespace cgw_diag
