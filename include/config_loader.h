#pragma once

#include "data_models.h"
#include "someip_types.h"
#include "log_types.h"
#include <yaml-cpp/yaml.h>
#include <initializer_list>
#include <string>

namespace ccu_diag {

// Unified production configuration loaded from /etc/ccu/conf.d/diag.yaml
// plus the BUILD-owned /etc/ccu/common.yaml (CCU-DIAG-DSN-CR-009 §5).
struct UnifiedDiagConfig {
    DidRegistryConfig registry;
    DiagConfig diag;
    TransportConfig transport;
    SomeIpServiceConfig someip;
    SomeIpServiceConfig sec_someip;
    ccu::fw::log::LogConfig log;
};

class ConfigLoader {
public:
    // Production entry: unified diag: subtree + common.log.* from common.yaml.
    // Fails closed on top-level common:, unknown/deprecated root nodes or
    // unknown diag.* sub-keys; never silently ignores migrated config.
    static UnifiedDiagConfig loadUnifiedConfig(
        const std::string& diag_config_path,
        const std::string& common_config_path);

    // Legacy per-domain loaders (development/test override entry points only;
    // production does not rely on them, but they remain supported for the
    // --did-registry / --someip-config / --sec-config overrides).
    static DidRegistryConfig loadDidRegistry(const std::string& file_path);
    static DiagConfig loadDiagConfig(const std::string& file_path);
    static SomeIpServiceConfig loadSomeIpConfig(const std::string& file_path);

    // Legacy combined-file log loader (common.log.* + diag.log.* in one file).
    static ccu::fw::log::LogConfig loadLogConfig(const std::string& file_path);

private:
    static EcuConfig parseEcuConfig(const YAML::Node& node);
    static DidEntry parseDidEntry(const YAML::Node& node);
    static void parseLogConfigNode(const YAML::Node& root, ccu::fw::log::LogConfig& log_config);

    static DidRegistryConfig parseDidRegistryNode(const YAML::Node& node, bool strict);
    static DiagConfig parseDiagConfigNode(const YAML::Node& node);
    static TransportConfig parseTransportNode(const YAML::Node& node);
    static SomeIpServiceConfig parseSomeIpConfigNode(
        const YAML::Node& node,
        const SomeIpServiceConfig& defaults,
        bool allow_methods);
    static void rejectUnknownKeys(const YAML::Node& node,
                                  std::initializer_list<const char*> allowed,
                                  const std::string& context);
};

} // namespace ccu_diag
