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
    if (str == "provisioned") return VinSource::PROVISIONED;
    return VinSource::UNKNOWN;
}

} // namespace ccu_diag
