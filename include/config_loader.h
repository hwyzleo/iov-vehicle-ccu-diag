#pragma once

#include "data_models.h"
#include <yaml-cpp/yaml.h>
#include <string>

namespace cgw_diag {

class ConfigLoader {
public:
    static DidRegistryConfig loadDidRegistry(const std::string& file_path);
    static DiagConfig loadDiagConfig(const std::string& file_path);
    
private:
    static EcuConfig parseEcuConfig(const YAML::Node& node);
    static DidEntry parseDidEntry(const YAML::Node& node);
};

} // namespace cgw_diag
