// =============================================================================
// tests/test_diag_payload_codec.cpp
// 0x0002 payload v1 / 0x0005 registration wire contract
// (CCU-DIAG-DSN-CR-010 §5.2 / US-011 / US-013)
// =============================================================================

#include <gtest/gtest.h>

#include "constants.h"
#include "diag_payload_codec.h"

#include <nlohmann/json.hpp>

namespace ccu_diag {
namespace tests {

namespace {

VehicleSoftwareSnapshot makeSnapshot() {
    VehicleSoftwareSnapshot snapshot;
    snapshot.vin = "1234567890ABCDEFG";
    snapshot.vin_source = VinSource::PROVISIONED;
    snapshot.baseline_id = "BASE-001";
    snapshot.baseline_source = BaselineSource::FACTORY;
    snapshot.registry_version = "2026.07.1";
    snapshot.collected_at = "2026-11-08T10:00:00Z";
    snapshot.overall_result = CollectionStatus::ALL_OK;
    snapshot.schema_version = constants::DIAG_PAYLOAD_SCHEMA_VERSION;
    snapshot.inventory_source = InventorySource::HARDWARE;

    EcuVersionEntry bms;
    bms.ecu_id = "BMS";
    bms.part_number = "PN-BMS-001";
    bms.sw_version = "1.0.0";
    bms.hw_version = "HW-BMS-1.0";
    bms.source = ReadMethod::UDS_0x22;
    bms.status = EcuStatus::OK;
    snapshot.ecu_list.push_back(bms);

    EcuVersionEntry vcu;
    vcu.ecu_id = "VCU";
    vcu.sw_version = "2.0.0";
    vcu.source = ReadMethod::SOMEIP_GET_VERSION;
    vcu.status = EcuStatus::TIMEOUT;
    vcu.error_code = "CCU-DIAG-1002";
    snapshot.ecu_list.push_back(vcu);
    return snapshot;
}

TEST(DiagPayloadCodecTest, EncodeInventoryV1ContainsCompleteEcuList) {
    const std::string payload = DiagPayloadCodec::encodeInventoryV1(makeSnapshot());
    const auto j = nlohmann::json::parse(payload);

    ASSERT_TRUE(j.contains("schemaVersion"));
    EXPECT_EQ(j["schemaVersion"].get<int>(), 1);
    EXPECT_EQ(j["inventorySource"].get<std::string>(), "hardware");
    EXPECT_EQ(j["vin"].get<std::string>(), "1234567890ABCDEFG");
    EXPECT_EQ(j["vinSource"].get<std::string>(), "provisioned");
    EXPECT_EQ(j["baselineId"].get<std::string>(), "BASE-001");
    EXPECT_EQ(j["baselineSource"].get<std::string>(), "factory");
    EXPECT_EQ(j["registryVersion"].get<std::string>(), "2026.07.1");
    EXPECT_EQ(j["collectedAt"].get<std::string>(), "2026-11-08T10:00:00Z");
    EXPECT_EQ(j["overallResult"].get<std::string>(), "all_ok");

    ASSERT_TRUE(j["ecuList"].is_array());
    ASSERT_EQ(j["ecuList"].size(), 2u);

    const auto& bms = j["ecuList"][0];
    EXPECT_EQ(bms["ecuId"].get<std::string>(), "BMS");
    EXPECT_EQ(bms["partNumber"].get<std::string>(), "PN-BMS-001");
    EXPECT_EQ(bms["swVersion"].get<std::string>(), "1.0.0");
    EXPECT_EQ(bms["hwVersion"].get<std::string>(), "HW-BMS-1.0");
    EXPECT_EQ(bms["source"].get<std::string>(), "UDS_0x22");
    EXPECT_EQ(bms["status"].get<std::string>(), "ok");
    EXPECT_EQ(bms["errorCode"].get<std::string>(), "");

    // Optional fields stay present (empty) so FOTA sees a frozen field set.
    const auto& vcu = j["ecuList"][1];
    EXPECT_EQ(vcu["partNumber"].get<std::string>(), "");
    EXPECT_EQ(vcu["hwVersion"].get<std::string>(), "");
    EXPECT_EQ(vcu["source"].get<std::string>(), "SOMEIP_getVersion");
    EXPECT_EQ(vcu["status"].get<std::string>(), "timeout");
    EXPECT_EQ(vcu["errorCode"].get<std::string>(), "CCU-DIAG-1002");
}

// CCU-DIAG-DSN-CR-011: 0x0002 ecuList items expose the ECU serial number. The
// field is additive, so schemaVersion stays 1 and FOTA (which ignores unknown
// fields) keeps working.
TEST(DiagPayloadCodecTest, EncodeInventoryV1CarriesEcuSerialNumber) {
    auto snapshot = makeSnapshot();
    snapshot.ecu_list[0].serial_number = "00000002AA00000001";

    const auto j = nlohmann::json::parse(
        DiagPayloadCodec::encodeInventoryV1(snapshot));
    EXPECT_EQ(j["schemaVersion"].get<int>(), 1);
    ASSERT_EQ(j["ecuList"].size(), 2u);
    EXPECT_EQ(j["ecuList"][0]["serialNumber"].get<std::string>(),
              "00000002AA00000001");
    // Absent serial numbers stay present-but-empty (frozen field set).
    EXPECT_EQ(j["ecuList"][1]["serialNumber"].get<std::string>(), "");
}

TEST(DiagPayloadCodecTest, EncodeSeedSnapshotMarksSourceAndNullBaseline) {
    auto snapshot = makeSnapshot();
    snapshot.inventory_source = InventorySource::SEED;
    snapshot.baseline_id.reset();
    snapshot.baseline_source = BaselineSource::UNKNOWN;
    snapshot.overall_result = CollectionStatus::PARTIAL;

    const auto j = nlohmann::json::parse(
        DiagPayloadCodec::encodeInventoryV1(snapshot));
    EXPECT_EQ(j["inventorySource"].get<std::string>(), "seed");
    EXPECT_TRUE(j["baselineId"].is_null());
    EXPECT_EQ(j["baselineSource"].get<std::string>(), "unknown");
    EXPECT_EQ(j["overallResult"].get<std::string>(), "partial");
}

TEST(DiagPayloadCodecTest, ErrorPayloadCarriesCodeAndNoInventory) {
    const auto j = nlohmann::json::parse(
        DiagPayloadCodec::encodeInventoryError("CCU-DIAG-1012"));
    EXPECT_EQ(j["schemaVersion"].get<int>(), 1);
    EXPECT_EQ(j["errorCode"].get<std::string>(), "CCU-DIAG-1012");
    EXPECT_FALSE(j.contains("ecuList"));
    EXPECT_FALSE(j.contains("vin"));
}

TEST(DiagPayloadCodecTest, DecodeRegistrationRequestFotaWireFormat) {
    const std::string payload = R"({
      "schemaVersion": 1,
      "vehicleTaskId": "VT-001",
      "taskRevision": 3,
      "executionId": "EXE-1",
      "attemptNo": 1,
      "idempotencyKey": "EXE-1:1",
      "appliedAtMs": 1794000000000,
      "items": [
        {"ecuId":"BMS","packageId":"PKG-BMS-1","packageRevision":"r1",
         "installedVersion":"2.0.0"}
      ]
    })";

    InstalledVersionsRequest request;
    std::string detail;
    ASSERT_TRUE(DiagPayloadCodec::decodeRegistrationRequest(payload, request, detail))
        << detail;
    EXPECT_EQ(request.schema_version, 1u);
    EXPECT_EQ(request.vehicle_task_id, "VT-001");
    EXPECT_EQ(request.task_revision, 3u);
    EXPECT_EQ(request.execution_id, "EXE-1");
    EXPECT_EQ(request.attempt_no, 1u);
    EXPECT_EQ(request.idempotency_key, "EXE-1:1");
    EXPECT_EQ(request.applied_at_ms, 1794000000000);
    ASSERT_EQ(request.items.size(), 1u);
    EXPECT_EQ(request.items[0].ecu_id, "BMS");
    EXPECT_EQ(request.items[0].package_id, "PKG-BMS-1");
    EXPECT_EQ(request.items[0].package_revision, "r1");
    EXPECT_EQ(request.items[0].installed_version, "2.0.0");
}

TEST(DiagPayloadCodecTest, DecodeRegistrationRequestRejectsStructuralProblems) {
    InstalledVersionsRequest request;
    std::string detail;

    EXPECT_FALSE(DiagPayloadCodec::decodeRegistrationRequest("", request, detail));
    EXPECT_FALSE(DiagPayloadCodec::decodeRegistrationRequest("not json", request,
                                                             detail));
    EXPECT_FALSE(DiagPayloadCodec::decodeRegistrationRequest(
        R"({"schemaVersion":2,"executionId":"E","attemptNo":1,
            "items":[{"ecuId":"BMS","installedVersion":"1.0.0"}]})",
        request, detail));
    EXPECT_FALSE(DiagPayloadCodec::decodeRegistrationRequest(
        R"({"schemaVersion":1,"attemptNo":1,
            "items":[{"ecuId":"BMS","installedVersion":"1.0.0"}]})",
        request, detail));
    EXPECT_FALSE(DiagPayloadCodec::decodeRegistrationRequest(
        R"({"schemaVersion":1,"executionId":"E","attemptNo":0,
            "items":[{"ecuId":"BMS","installedVersion":"1.0.0"}]})",
        request, detail));
    EXPECT_FALSE(DiagPayloadCodec::decodeRegistrationRequest(
        R"({"schemaVersion":1,"executionId":"E","attemptNo":1,"items":[]})",
        request, detail));
}

TEST(DiagPayloadCodecTest, DecodeKeepsPerItemFieldsForRegistrarValidation) {
    // Missing per-item values must reach the registrar so the response can
    // report MISSING_FIELD / UNKNOWN_ECU / INVALID_VERSION.
    InstalledVersionsRequest request;
    std::string detail;
    ASSERT_TRUE(DiagPayloadCodec::decodeRegistrationRequest(
        R"({"schemaVersion":1,"executionId":"E","attemptNo":1,
            "items":[{"ecuId":""},{"installedVersion":"1.0.0"}]})",
        request, detail))
        << detail;
    ASSERT_EQ(request.items.size(), 2u);
    EXPECT_TRUE(request.items[0].ecu_id.empty());
    EXPECT_TRUE(request.items[0].installed_version.empty());
    EXPECT_TRUE(request.items[1].ecu_id.empty());
    EXPECT_EQ(request.items[1].installed_version, "1.0.0");
}

TEST(DiagPayloadCodecTest, EncodeRegistrationResponseApplied) {
    InstalledVersionsResponse response;
    response.disposition = RegistrationDisposition::APPLIED;
    response.registration_id = "REG-ABC";
    response.applied_count = 2;
    response.effective_updated_at = "2026-11-08T10:05:00Z";
    response.error_code = "SHOULD-NOT-LEAK";

    const auto j = nlohmann::json::parse(
        DiagPayloadCodec::encodeRegistrationResponse(response));
    EXPECT_EQ(j["schemaVersion"].get<int>(), 1);
    EXPECT_EQ(j["disposition"].get<std::string>(), "APPLIED");
    EXPECT_EQ(j["registrationId"].get<std::string>(), "REG-ABC");
    EXPECT_EQ(j["appliedCount"].get<int>(), 2);
    EXPECT_TRUE(j["rejectedItems"].is_array());
    EXPECT_TRUE(j["rejectedItems"].empty());
    EXPECT_EQ(j["effectiveUpdatedAt"].get<std::string>(), "2026-11-08T10:05:00Z");
    EXPECT_FALSE(j.contains("errorCode"));
}

TEST(DiagPayloadCodecTest, EncodeRegistrationResponseRejectedItems) {
    InstalledVersionsResponse response;
    response.disposition = RegistrationDisposition::REJECTED_INVALID;
    response.applied_count = 0;
    response.rejected_items.push_back(RejectedVersionItem{"BMS", "INVALID_VERSION"});
    response.rejected_items.push_back(RejectedVersionItem{"VCU", "UNKNOWN_ECU"});

    const auto j = nlohmann::json::parse(
        DiagPayloadCodec::encodeRegistrationResponse(response));
    EXPECT_EQ(j["disposition"].get<std::string>(), "REJECTED_INVALID");
    EXPECT_FALSE(j.contains("registrationId"));
    ASSERT_EQ(j["rejectedItems"].size(), 2u);
    EXPECT_EQ(j["rejectedItems"][0]["ecuId"].get<std::string>(), "BMS");
    EXPECT_EQ(j["rejectedItems"][0]["reasonCode"].get<std::string>(),
              "INVALID_VERSION");
}

} // namespace
} // namespace tests
} // namespace ccu_diag
