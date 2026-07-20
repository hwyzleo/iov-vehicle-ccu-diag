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
    auto it = ecu_id_index_.find(ecu_id);
    if (it != ecu_id_index_.end()) {
        return config_.ecus[it->second].dids;
    }
    return {};
}

void DidRegistry::reload() {
    auto new_config = ConfigLoader::loadDidRegistry(config_path_);

    std::unordered_map<std::string, size_t> new_id_index;
    std::unordered_map<uint16_t, size_t> new_address_index;

    for (size_t i = 0; i < new_config.ecus.size(); ++i) {
        const auto& ecu = new_config.ecus[i];
        if (new_id_index.count(ecu.ecu_id)) {
            throw std::runtime_error("Duplicate ECU ID: " + ecu.ecu_id);
        }
        if (new_address_index.count(ecu.logical_address)) {
            throw std::runtime_error("Duplicate ECU logical address: 0x" +
                std::to_string(ecu.logical_address));
        }
        new_id_index[ecu.ecu_id] = i;
        new_address_index[ecu.logical_address] = i;
    }

    config_ = std::move(new_config);
    ecu_id_index_ = std::move(new_id_index);
    ecu_address_index_ = std::move(new_address_index);
}

void DidRegistry::buildIndices() {
    ecu_id_index_.clear();
    ecu_address_index_.clear();

    for (size_t i = 0; i < config_.ecus.size(); ++i) {
        const auto& ecu = config_.ecus[i];
        if (ecu_id_index_.count(ecu.ecu_id)) {
            throw std::runtime_error("Duplicate ECU ID: " + ecu.ecu_id);
        }
        if (ecu_address_index_.count(ecu.logical_address)) {
            throw std::runtime_error("Duplicate ECU logical address: 0x" +
                std::to_string(ecu.logical_address));
        }
        ecu_id_index_[ecu.ecu_id] = i;
        ecu_address_index_[ecu.logical_address] = i;
    }
}

} // namespace cgw_diag
