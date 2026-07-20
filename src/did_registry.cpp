#include "did_registry.h"
#include <stdexcept>

namespace cgw_diag {

DidRegistry::DidRegistry(const std::string& config_path) 
    : config_path_(config_path) {
    reload();
}

std::string DidRegistry::getRegistryVersion() const {
    return config_.registry_version;
}

const std::vector<uint16_t>& DidRegistry::getBaselineDids() const {
    return config_.baseline_dids;
}

std::optional<EcuConfig> DidRegistry::findEcuById(const std::string& ecu_id) const {
    auto it = ecu_id_index_.find(ecu_id);
    if (it != ecu_id_index_.end()) {
        return config_.ecus[it->second];
    }
    return std::nullopt;
}

std::optional<EcuConfig> DidRegistry::findEcuByLogicalAddress(uint16_t logical_address) const {
    auto it = ecu_address_index_.find(logical_address);
    if (it != ecu_address_index_.end()) {
        return config_.ecus[it->second];
    }
    return std::nullopt;
}

const std::vector<EcuConfig>& DidRegistry::getAllEcus() const {
    return config_.ecus;
}

bool DidRegistry::isEcuRegistered(const std::string& ecu_id) const {
    return ecu_id_index_.find(ecu_id) != ecu_id_index_.end();
}

std::vector<DidEntry> DidRegistry::getDidsForEcu(const std::string& ecu_id) const {
    auto ecu = findEcuById(ecu_id);
    if (ecu) {
        return ecu->dids;
    }
    return {};
}

void DidRegistry::reload() {
    config_ = ConfigLoader::loadDidRegistry(config_path_);
    buildIndices();
}

void DidRegistry::buildIndices() {
    ecu_id_index_.clear();
    ecu_address_index_.clear();
    
    for (size_t i = 0; i < config_.ecus.size(); ++i) {
        const auto& ecu = config_.ecus[i];
        ecu_id_index_[ecu.ecu_id] = i;
        ecu_address_index_[ecu.logical_address] = i;
    }
}

} // namespace cgw_diag
