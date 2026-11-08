#pragma once

// =============================================================================
// include/diag_payload_codec.h
// CCU-DIAG SOME/IP payload v1 codec (CCU-DIAG-DSN-CR-010 §5.2)
// =============================================================================
// Wire form is frozen at schemaVersion=1:
//   * 0x0002 METHOD_COLLECT_VEHICLE_INVENTORY response: versioned JSON with the
//     complete ecuList (camelCase field names, per design §5.2).
//   * 0x0005 METHOD_RECORD_INSTALLED_VERSIONS request/response.
// Encoding failures never produce a partial payload: callers must emit the
// error payload (encodeInventoryError) plus a non-E_OK return code instead of
// a "successful" empty inventory (CCU-DIAG-DSN-CR-010 D9).
// =============================================================================

#include "data_models.h"

#include <string>
#include <string_view>

namespace ccu_diag {

class DiagPayloadCodec {
public:
    // Serialize a complete snapshot as the 0x0002 payload v1.
    static std::string encodeInventoryV1(const VehicleSoftwareSnapshot& snapshot);

    // Error payload for a failed 0x0002 call: {schemaVersion, errorCode}.
    // Contains no VIN, inventory values or transport detail.
    static std::string encodeInventoryError(const std::string& diag_error_code);

    // Decode a 0x0005 request. Structural problems (bad JSON, missing identity,
    // bad item type) return false; per-item business validation is left to
    // FotaVersionRegistrar so rejectedItems can be reported item by item.
    static bool decodeRegistrationRequest(std::string_view payload,
                                          InstalledVersionsRequest& request,
                                          std::string& error_detail);

    // Serialize a 0x0005 response.
    static std::string encodeRegistrationResponse(
        const InstalledVersionsResponse& response);
};

} // namespace ccu_diag
