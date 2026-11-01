#include "vin_store.h"
#include <fstream>
#include <filesystem>
#include <algorithm>
#include <cctype>
#include <iostream>

namespace ccu_diag {

class VinStoreImpl : public VinStore {
public:
    explicit VinStoreImpl(const std::string& storage_path)
        : storage_path_(storage_path), allow_rewrite_(false) {
        loadFromStorage();
    }

    std::optional<std::string> readVin() const override {
        std::lock_guard<std::mutex> lock(mutex_);
        if (vin_.empty()) {
            return std::nullopt;
        }
        return vin_;
    }

    bool writeVin(const std::string& vin) override {
        std::lock_guard<std::mutex> lock(mutex_);

        if (!isValidVin(vin)) {
            return false;
        }

        // Write-once semantics
        if (!vin_.empty()) {
            return false;
        }

        vin_ = vin;
        saveToStorage();
        return true;
    }

    bool isVinProvisioned() const override {
        std::lock_guard<std::mutex> lock(mutex_);
        return !vin_.empty();
    }

    void setAllowRewrite(bool allow) override {
        std::lock_guard<std::mutex> lock(mutex_);
        allow_rewrite_ = allow;
    }

    bool resetVin() override {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!allow_rewrite_) {
            return false;
        }
        vin_.clear();
        saveToStorage();
        return true;
    }

private:
    void loadFromStorage() {
        if (storage_path_.empty()) {
            return;
        }

        try {
            if (std::filesystem::exists(storage_path_)) {
                std::ifstream file(storage_path_);
                if (file.is_open()) {
                    std::getline(file, vin_);
                    // Trim whitespace
                    vin_.erase(std::remove_if(vin_.begin(), vin_.end(), ::isspace), vin_.end());
                }
            }
        } catch (...) {
            // Ignore storage errors, VIN remains empty
        }
    }

    void saveToStorage() {
        if (storage_path_.empty()) {
            return;
        }

        try {
            std::ofstream file(storage_path_);
            if (file.is_open()) {
                file << vin_;
            }
        } catch (...) {
            // Ignore storage errors
        }
    }

    std::string storage_path_;
    std::string vin_;
    bool allow_rewrite_;
    mutable std::mutex mutex_;
};

bool VinStore::isValidVin(const std::string& vin) {
    if (vin.length() != VIN_LENGTH) {
        return false;
    }

    // Check for valid VIN characters (alphanumeric, no I, O, Q)
    for (char c : vin) {
        if (!std::isalnum(c) || c == 'I' || c == 'O' || c == 'Q') {
            return false;
        }
    }

    return true;
}

std::unique_ptr<VinStore> createVinStore(const std::string& storage_path) {
    return std::make_unique<VinStoreImpl>(storage_path);
}

} // namespace ccu_diag
