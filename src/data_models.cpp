#include "data_models.h"

namespace ccu_diag {

std::string vinSourceToString(VinSource source) {
    switch (source) {
        case VinSource::PROVISIONED: return "provisioned";
        case VinSource::UNKNOWN: return "unknown";
    }
    return "unknown";
}

VinSource stringToVinSource(const std::string& str) {
    if (str == "provisioned" || str == "PROVISIONED") return VinSource::PROVISIONED;
    return VinSource::UNKNOWN;
}

std::string inventorySourceToString(InventorySource source) {
    switch (source) {
        case InventorySource::HARDWARE: return "hardware";
        case InventorySource::SEED: return "seed";
    }
    return "hardware";
}

InventorySource stringToInventorySource(const std::string& str) {
    if (str == "seed" || str == "SEED") return InventorySource::SEED;
    return InventorySource::HARDWARE;
}

std::string inventoryModeToString(InventoryMode mode) {
    switch (mode) {
        case InventoryMode::HARDWARE: return "hardware";
        case InventoryMode::SEED: return "seed";
    }
    return "hardware";
}

InventoryMode stringToInventoryMode(const std::string& str) {
    if (str == "seed" || str == "SEED") return InventoryMode::SEED;
    if (str == "hardware" || str == "HARDWARE") return InventoryMode::HARDWARE;
    // Callers that need fail-closed semantics validate the raw string first.
    return InventoryMode::HARDWARE;
}

std::string registrationDispositionToString(RegistrationDisposition disposition) {
    switch (disposition) {
        case RegistrationDisposition::APPLIED: return "APPLIED";
        case RegistrationDisposition::DUPLICATE: return "DUPLICATE";
        case RegistrationDisposition::REJECTED_INVALID: return "REJECTED_INVALID";
        case RegistrationDisposition::RETRYABLE_FAILED: return "RETRYABLE_FAILED";
    }
    return "RETRYABLE_FAILED";
}

} // namespace ccu_diag
