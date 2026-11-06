#pragma once

#include "data_models.h"
#include "config_loader.h"
#include <string>
#include <optional>
#include <unordered_map>

namespace ccu_diag {

// Thread safety: This class is NOT thread-safe.
// Concurrent calls to const methods are safe after construction/reload completes.
// Calls to reload() must be externally synchronized with all other method calls.
class DidRegistry {
public:
    explicit DidRegistry(const std::string& config_path);
    // Construct from an already-parsed unified configuration subtree
    // (CCU-DIAG-DSN-CR-009 §5.3). No filesystem access, no reload path.
    explicit DidRegistry(const DidRegistryConfig& config);
    
    // Get registry version
    std::string getRegistryVersion() const;
    
    // Get baseline DIDs
    const std::vector<uint16_t>& getBaselineDids() const;
    
    // Find ECU configuration by ID
    std::optional<EcuConfig> findEcuById(const std::string& ecu_id) const;
    
    // Find ECU configuration by logical address
    std::optional<EcuConfig> findEcuByLogicalAddress(uint16_t logical_address) const;
    
    // Get all ECU configurations
    const std::vector<EcuConfig>& getAllEcus() const;
    
    // Check if ECU is registered
    bool isEcuRegistered(const std::string& ecu_id) const;
    
    // Get DID entries for specific ECU
    std::vector<DidEntry> getDidsForEcu(const std::string& ecu_id) const;
    
    // Reload configuration
    void reload();
    
private:
    std::string config_path_;
    DidRegistryConfig config_;
    std::unordered_map<std::string, size_t> ecu_id_index_;
    std::unordered_map<uint16_t, size_t> ecu_address_index_;
    
    void buildIndices();
    void applyConfig(DidRegistryConfig new_config);
};

} // namespace ccu_diag
