#include <gtest/gtest.h>
#include "version_collector.h"
#include "did_registry.h"
#include "diagnostic_router.h"
#include "uds_client.h"
#include "error_codes.h"
#include "mock_transport.h"
#include "vin_store.h"
#include <fstream>
#include <memory>
#include <filesystem>
#include <algorithm>

namespace cgw_diag {
namespace tests {

class VersionCollectorTest : public ::testing::Test {
protected:
    void SetUp() override {
        temp_dir_ = std::filesystem::temp_directory_path();
        config_path_ = (temp_dir_ / "test_version_collector.yaml").string();
        createTestConfig();

        registry = std::make_shared<DidRegistry>(config_path_);

        mock_transport = std::make_shared<MockTransport>();
        mock_transport->setTransportType("DoCAN");

        mock_doip_transport = std::make_shared<MockTransport>();
        mock_doip_transport->setTransportType("DoIP");

        DiagConfig diag_config;
        diag_config.max_retry_count = 2;
        uds_client = std::make_shared<UdsClient>(mock_transport, diag_config);

        router = std::make_shared<DiagnosticRouter>(registry, uds_client,
                                                      mock_transport, mock_doip_transport);

        vin_store_path_ = (temp_dir_ / "test_vin_store.dat").string();
        vin_store = std::shared_ptr<VinStore>(createVinStore(vin_store_path_).release());

        collector = std::make_unique<VersionCollector>(registry, router,
                                                        mock_transport, mock_doip_transport,
                                                        vin_store);
    }

    void TearDown() override {
        std::filesystem::remove(config_path_);
        std::filesystem::remove(vin_store_path_);
    }

    void createTestConfig() {
        std::ofstream file(config_path_);
        file << R"(
registryVersion: "2026.07.0"
baselineDids: [0xF195, 0xF188, 0xF187, 0xF193]
ecus:
  - ecuId: "BMS"
    logicalAddress: 0x1A
    transport: "DoCAN"
    readMethod: "UDS_0x22"
    securityRequired: false
    dids:
      - did: 0xF195
        field: "swVersion"
        encoding: "ASCII"
        length: 16
      - did: 0xF187
        field: "partNumber"
        encoding: "ASCII"
        length: 11
  - ecuId: "IDCM"
    logicalAddress: 0x20
    transport: "DoIP"
    readMethod: "UDS_0x22"
    securityRequired: false
    dids:
      - did: 0xF195
        field: "swVersion"
        encoding: "ASCII"
        length: 16
)";
        file.close();
    }

    std::filesystem::path temp_dir_;
    std::string config_path_;
    std::string vin_store_path_;
    std::shared_ptr<DidRegistry> registry;
    std::shared_ptr<MockTransport> mock_transport;
    std::shared_ptr<MockTransport> mock_doip_transport;
    std::shared_ptr<UdsClient> uds_client;
    std::shared_ptr<DiagnosticRouter> router;
    std::shared_ptr<VinStore> vin_store;
    std::unique_ptr<VersionCollector> collector;
};

TEST_F(VersionCollectorTest, CollectVehicleInventoryAllOk) {
    mock_transport->setRequestHandler([](uint16_t addr, const std::vector<uint8_t>& request) {
        TransportResponse resp;
        resp.success = true;
        uint16_t did = (request[1] << 8) | request[2];
        resp.data = {0x62, request[1], request[2]};
        if (addr == 0x1A) {
            if (did == 0xF195) {
                resp.data.insert(resp.data.end(), {'1', '.', '0', '.', '0', '\0'});
            } else if (did == 0xF187) {
                resp.data.insert(resp.data.end(), {'P', 'A', 'R', 'T', '1', '2', '3', '\0'});
            }
        } else if (addr == 0x20) {
            resp.data.insert(resp.data.end(), {'2', '.', '0', '.', '0', '\0'});
        }
        return resp;
    });

    vin_store->writeVin("1234567890ABCDEFG");

    collector->setBaselineInfo("BASELINE_V1.0", BaselineSource::FACTORY);

    auto snapshot = collector->collectVehicleInventory();

    EXPECT_EQ(snapshot.vin, "1234567890ABCDEFG");
    EXPECT_EQ(snapshot.vin_source, VinSource::PROVISIONED);
    EXPECT_EQ(snapshot.baseline_id, "BASELINE_V1.0");
    EXPECT_EQ(snapshot.baseline_source, BaselineSource::FACTORY);
    EXPECT_EQ(snapshot.registry_version, "2026.07.0");
    EXPECT_FALSE(snapshot.collected_at.empty());
    EXPECT_EQ(snapshot.overall_result, CollectionStatus::ALL_OK);

    ASSERT_EQ(snapshot.ecu_list.size(), 2);

    auto bms_it = std::find_if(snapshot.ecu_list.begin(), snapshot.ecu_list.end(),
                               [](const EcuVersionEntry& e) { return e.ecu_id == "BMS"; });
    ASSERT_NE(bms_it, snapshot.ecu_list.end());
    EXPECT_EQ(bms_it->status, EcuStatus::OK);
    EXPECT_EQ(bms_it->source, ReadMethod::UDS_0x22);
    EXPECT_TRUE(bms_it->sw_version.has_value());
    EXPECT_EQ(bms_it->sw_version.value(), "1.0.0");
    EXPECT_TRUE(bms_it->part_number.has_value());
    EXPECT_EQ(bms_it->part_number.value(), "PART123");

    auto idcm_it = std::find_if(snapshot.ecu_list.begin(), snapshot.ecu_list.end(),
                                [](const EcuVersionEntry& e) { return e.ecu_id == "IDCM"; });
    ASSERT_NE(idcm_it, snapshot.ecu_list.end());
    EXPECT_EQ(idcm_it->status, EcuStatus::OK);
    EXPECT_TRUE(idcm_it->sw_version.has_value());
    EXPECT_EQ(idcm_it->sw_version.value(), "2.0.0");
}

TEST_F(VersionCollectorTest, CollectVehicleInventoryPartial) {
    mock_transport->setRequestHandler([](uint16_t addr, const std::vector<uint8_t>& request) {
        TransportResponse resp;
        if (addr == 0x1A) {
            resp.success = true;
            resp.data = {0x62, request[1], request[2], '1', '.', '0', '\0'};
        } else {
            resp.success = false;
            resp.error_message = "Transport error";
        }
        return resp;
    });

    mock_doip_transport->setRequestHandler([](uint16_t, const std::vector<uint8_t>&) {
        TransportResponse resp;
        resp.success = false;
        resp.error_message = "Timeout";
        return resp;
    });

    auto snapshot = collector->collectVehicleInventory();

    EXPECT_EQ(snapshot.overall_result, CollectionStatus::PARTIAL);
}

TEST_F(VersionCollectorTest, CollectVehicleInventoryFailed) {
    mock_transport->setRequestHandler([](uint16_t, const std::vector<uint8_t>&) {
        TransportResponse resp;
        resp.success = false;
        resp.error_message = "Transport error";
        return resp;
    });

    mock_doip_transport->setRequestHandler([](uint16_t, const std::vector<uint8_t>&) {
        TransportResponse resp;
        resp.success = false;
        resp.error_message = "Timeout";
        return resp;
    });

    auto snapshot = collector->collectVehicleInventory();

    EXPECT_EQ(snapshot.overall_result, CollectionStatus::FAILED);
}

TEST_F(VersionCollectorTest, CollectEcuVersionSuccess) {
    mock_transport->setRequestHandler([](uint16_t, const std::vector<uint8_t>& request) {
        TransportResponse resp;
        resp.success = true;
        resp.data = {0x62, request[1], request[2], '1', '.', '0', '.', '0', '\0'};
        return resp;
    });

    auto entry = collector->collectEcuVersion("BMS");

    EXPECT_EQ(entry.ecu_id, "BMS");
    EXPECT_EQ(entry.source, ReadMethod::UDS_0x22);
    EXPECT_EQ(entry.status, EcuStatus::OK);
    EXPECT_TRUE(entry.sw_version.has_value());
    EXPECT_EQ(entry.sw_version.value(), "1.0.0");
}

TEST_F(VersionCollectorTest, CollectEcuVersionNotRegistered) {
    auto entry = collector->collectEcuVersion("UNKNOWN");

    EXPECT_EQ(entry.ecu_id, "UNKNOWN");
    EXPECT_EQ(entry.status, EcuStatus::MISSING);
    EXPECT_TRUE(entry.error_code.has_value());
    EXPECT_EQ(entry.error_code.value(), ErrorCodeMapper::toDiagCode(ErrorCode::ECU_NOT_REGISTERED));
}

TEST_F(VersionCollectorTest, CollectEcuVersionTransportFailure) {
    mock_transport->setRequestHandler([](uint16_t, const std::vector<uint8_t>&) {
        TransportResponse resp;
        resp.success = false;
        resp.error_message = "Transport error";
        return resp;
    });

    auto entry = collector->collectEcuVersion("BMS");

    EXPECT_EQ(entry.ecu_id, "BMS");
    EXPECT_EQ(entry.source, ReadMethod::UDS_0x22);
    EXPECT_EQ(entry.status, EcuStatus::TIMEOUT);
    EXPECT_TRUE(entry.error_code.has_value());
}

TEST_F(VersionCollectorTest, CollectEcuVersionNegativeResponse) {
    mock_transport->setRequestHandler([](uint16_t, const std::vector<uint8_t>&) {
        TransportResponse resp;
        resp.success = true;
        resp.data = {0x7F, 0x22, 0x31};  // NRC 0x31 (RequestOutOfRange)
        return resp;
    });

    auto entry = collector->collectEcuVersion("BMS");

    EXPECT_EQ(entry.ecu_id, "BMS");
    EXPECT_EQ(entry.status, EcuStatus::NRC);
    EXPECT_TRUE(entry.error_code.has_value());
}

TEST_F(VersionCollectorTest, SetBaselineInfo) {
    collector->setBaselineInfo("NEW_BASELINE", BaselineSource::LAST_OTA);

    mock_transport->setRequestHandler([](uint16_t addr, const std::vector<uint8_t>& request) {
        TransportResponse resp;
        resp.success = true;
        resp.data = {0x62, request[1], request[2], '1', '.', '0', '\0'};
        return resp;
    });

    mock_doip_transport->setRequestHandler([](uint16_t addr, const std::vector<uint8_t>& request) {
        TransportResponse resp;
        resp.success = true;
        resp.data = {0x62, request[1], request[2], '2', '.', '0', '\0'};
        return resp;
    });

    auto snapshot = collector->collectVehicleInventory();

    EXPECT_EQ(snapshot.baseline_id, "NEW_BASELINE");
    EXPECT_EQ(snapshot.baseline_source, BaselineSource::LAST_OTA);
}

TEST_F(VersionCollectorTest, DefaultBaselineInfo) {
    mock_transport->setRequestHandler([](uint16_t addr, const std::vector<uint8_t>& request) {
        TransportResponse resp;
        resp.success = true;
        resp.data = {0x62, request[1], request[2], '1', '.', '0', '\0'};
        return resp;
    });

    mock_doip_transport->setRequestHandler([](uint16_t addr, const std::vector<uint8_t>& request) {
        TransportResponse resp;
        resp.success = true;
        resp.data = {0x62, request[1], request[2], '2', '.', '0', '\0'};
        return resp;
    });

    auto snapshot = collector->collectVehicleInventory();

    EXPECT_FALSE(snapshot.baseline_id.has_value());
    EXPECT_EQ(snapshot.baseline_source, BaselineSource::UNKNOWN);
}

TEST_F(VersionCollectorTest, CollectVehicleInventoryEmptyEcuList) {
    std::ofstream file(config_path_);
    file << R"(
registryVersion: "2026.07.0"
baselineDids: []
ecus: []
)";
    file.close();

    registry->reload();

    auto snapshot = collector->collectVehicleInventory();

    EXPECT_EQ(snapshot.ecu_list.size(), 0);
    EXPECT_EQ(snapshot.overall_result, CollectionStatus::FAILED);
}

} // namespace tests
} // namespace cgw_diag
