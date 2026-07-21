#pragma once

#include <string>
#include <optional>
#include <mutex>
#include <memory>
#include <cstdint>

namespace cgw_diag {

class VinStore {
public:
    virtual ~VinStore() = default;

    // Read VIN from protected NVM
    virtual std::optional<std::string> readVin() const = 0;

    // Write VIN to protected NVM (write-once semantics)
    virtual bool writeVin(const std::string& vin) = 0;

    // Check if VIN is provisioned
    virtual bool isVinProvisioned() const = 0;

    // Reset VIN (for authorized rewrites only)
    virtual bool resetVin() = 0;

    // Get VIN length validation
    static constexpr size_t VIN_LENGTH = 17;
    static bool isValidVin(const std::string& vin);
};

// Factory function for creating VinStore instance
std::unique_ptr<VinStore> createVinStore(const std::string& storage_path = "");

} // namespace cgw_diag
