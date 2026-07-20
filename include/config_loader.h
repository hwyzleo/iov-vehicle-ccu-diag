#pragma once

#include "data_models.h"
#include <string>

namespace cgw_diag {

class ConfigLoader {
public:
    static DidRegistryConfig loadDidRegistry(const std::string& file_path);
    static DiagConfig loadDiagConfig(const std::string& file_path);
    
private:
    static EcuConfig parseEcuConfig(const void* ecu_node);
    static DidEntry parseDidEntry(const void* did_node);
};

} // namespace cgw_diag
