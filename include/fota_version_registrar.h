#pragma once

// =============================================================================
// include/fota_version_registrar.h
// 0x0005 METHOD_RECORD_INSTALLED_VERSIONS handler logic
// (CCU-DIAG-DSN-CR-010 D6/D7/D8)
// =============================================================================
// Semantics:
//   * validates the request per item (MISSING_FIELD / UNKNOWN_ECU /
//     INVALID_VERSION / FIELD_TOO_LONG) and rejects the WHOLE request without
//     any change (all-or-nothing atomic commit),
//   * idempotency key = executionId + attemptNo; same key + same item digest
//     -> DUPLICATE (existing registrationId, no change); same key + different
//     content -> REJECTED_INVALID + CCU-DIAG-1016,
//   * APPLIED updates only the ECU software versions and updatedAt; it never
//     changes registryVersion/baseline/VIN and never writes / flashes an ECU,
//   * persistence failure -> RETRYABLE_FAILED + CCU-DIAG-1015, record unchanged.
// =============================================================================

#include "data_models.h"
#include "ecu_inventory_store.h"

#include <memory>
#include <string>
#include <vector>

namespace ccu_diag {

class FotaVersionRegistrar {
public:
    FotaVersionRegistrar(std::shared_ptr<EcuInventoryStore> store,
                         std::vector<std::string> known_ecu_ids,
                         uint32_t idempotency_window = 64);

    InstalledVersionsResponse recordInstalledVersions(
        const InstalledVersionsRequest& request);

    static bool isValidVersionString(const std::string& version);

private:
    bool isKnownEcu(const std::string& ecu_id,
                    const EcuInventoryRecord& record) const;
    static std::string canonicalItemsDigest(
        const std::vector<InstalledVersionItem>& items);
    static std::string makeRegistrationId(const std::string& key,
                                          const std::string& digest);

    std::shared_ptr<EcuInventoryStore> store_;
    std::vector<std::string> known_ecu_ids_;
    uint32_t idempotency_window_;
};

} // namespace ccu_diag
