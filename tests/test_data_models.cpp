#include <gtest/gtest.h>
#include "data_models.h"

namespace cgw_diag {
namespace tests {

TEST(DataModelsTest, EcuConfigCreation) {
    EcuConfig config;
    config.ecu_id = "BMS";
    config.logical_address = 0x1A;
    config.transport = TransportType::DOCAN;
    config.read_method = ReadMethod::UDS_0x22;
    config.security_required = false;
    
    DidEntry did_entry;
    did_entry.did = 0xF195;
    did_entry.field = "swVersion";
    did_entry.encoding = "ASCII";
    did_entry.length = 16;
    
    config.dids.push_back(did_entry);
    
    EXPECT_EQ(config.ecu_id, "BMS");
    EXPECT_EQ(config.logical_address, 0x1A);
    EXPECT_EQ(config.transport, TransportType::DOCAN);
    EXPECT_EQ(config.read_method, ReadMethod::UDS_0x22);
    EXPECT_FALSE(config.security_required);
    EXPECT_EQ(config.dids.size(), 1);
    EXPECT_EQ(config.dids[0].did, 0xF195);
}

TEST(DataModelsTest, VehicleSoftwareSnapshotCreation) {
    VehicleSoftwareSnapshot snapshot;
    snapshot.vin = "1234567890ABCDEFG";
    snapshot.baseline_id = "BASELINE_V1.0";
    snapshot.baseline_source = BaselineSource::FACTORY;
    snapshot.registry_version = "2026.07.0";
    snapshot.collected_at = "2026-07-20T12:00:00Z";
    snapshot.overall_result = CollectionStatus::ALL_OK;
    
    EcuVersionEntry entry;
    entry.ecu_id = "BMS";
    entry.part_number = "PART123";
    entry.sw_version = "1.0.0";
    entry.hw_version = "HW1.0";
    entry.source = ReadMethod::UDS_0x22;
    entry.status = EcuStatus::OK;
    
    snapshot.ecu_list.push_back(entry);
    
    EXPECT_EQ(snapshot.vin, "1234567890ABCDEFG");
    EXPECT_EQ(snapshot.baseline_id, "BASELINE_V1.0");
    EXPECT_EQ(snapshot.baseline_source, BaselineSource::FACTORY);
    EXPECT_EQ(snapshot.registry_version, "2026.07.0");
    EXPECT_EQ(snapshot.overall_result, CollectionStatus::ALL_OK);
    EXPECT_EQ(snapshot.ecu_list.size(), 1);
    EXPECT_EQ(snapshot.ecu_list[0].ecu_id, "BMS");
}

TEST(DataModelsTest, DiagConfigDefaults) {
    DiagConfig config;
    config.p2_server_max_ms = 50;
    config.p2_star_server_max_ms = 5000;
    config.s3_session_timeout_ms = 5000;
    config.tester_present_interval_ms = 2000;
    config.request_timeout_ms = 2000;
    config.retry_interval_ms = 100;
    config.max_retry_count = 2;
    
    EXPECT_EQ(config.p2_server_max_ms, 50);
    EXPECT_EQ(config.p2_star_server_max_ms, 5000);
    EXPECT_EQ(config.s3_session_timeout_ms, 5000);
    EXPECT_EQ(config.tester_present_interval_ms, 2000);
    EXPECT_EQ(config.request_timeout_ms, 2000);
    EXPECT_EQ(config.retry_interval_ms, 100);
    EXPECT_EQ(config.max_retry_count, 2);
}

} // namespace tests
} // namespace cgw_diag
