#include <gtest/gtest.h>
#include "version_collector.h"
#include "did_registry.h"
#include "diagnostic_router.h"
#include "ecu_inventory_store.h"
#include "uds_client.h"
#include "error_codes.h"
#include "mock_transport.h"
#include "vin_store.h"
#include <fstream>
#include <memory>
#include <filesystem>
#include <algorithm>

namespace ccu_diag {
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

// CCU-DIAG-DSN-CR-011: hardware mode maps DID 0xF18C into serial_number.
TEST_F(VersionCollectorTest, CollectEcuVersionMapsSerialNumberDid) {
    std::ofstream file(config_path_);
    file << R"(
registryVersion: "2026.11.1"
baselineDids: [0xF195, 0xF18C]
ecus:
  - ecuId: "TBOX"
    logicalAddress: 0x10
    transport: "DoCAN"
    readMethod: "UDS_0x22"
    securityRequired: false
    dids:
      - did: 0xF195
        field: "swVersion"
        encoding: "ASCII"
        length: 16
      - did: 0xF18C
        field: "serialNumber"
        encoding: "ASCII"
        length: 20
)";
    file.close();

    auto registry = std::make_shared<DidRegistry>(config_path_);
    DiagConfig diag_config;
    diag_config.max_retry_count = 2;
    auto uds_client = std::make_shared<UdsClient>(mock_transport, diag_config);
    auto router = std::make_shared<DiagnosticRouter>(registry, uds_client,
                                                     mock_transport, mock_doip_transport);
    VersionCollector collector(registry, router, mock_transport,
                               mock_doip_transport, vin_store);

    mock_transport->setRequestHandler([](uint16_t addr, const std::vector<uint8_t>& request) {
        TransportResponse resp;
        resp.success = true;
        uint16_t did = (request[1] << 8) | request[2];
        resp.data = {0x62, request[1], request[2]};
        if (did == 0xF195) {
            resp.data.insert(resp.data.end(), {'V', '0', '.', '0', '.', '1', '\0'});
        } else if (did == 0xF18C) {
            const std::string sn = "00000002AA00000001";
            resp.data.insert(resp.data.end(), sn.begin(), sn.end());
            resp.data.push_back('\0');
        }
        (void)addr;
        return resp;
    });

    auto entry = collector.collectEcuVersion("TBOX");
    EXPECT_EQ(entry.status, EcuStatus::OK);
    ASSERT_TRUE(entry.sw_version.has_value());
    EXPECT_EQ(entry.sw_version.value(), "V0.0.1");
    ASSERT_TRUE(entry.serial_number.has_value());
    EXPECT_EQ(entry.serial_number.value(), "00000002AA00000001");
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

    mock_transport->setRequestHandler([](uint16_t, const std::vector<uint8_t>& request) {
        TransportResponse resp;
        resp.success = true;
        resp.data = {0x62, request[1], request[2], '1', '.', '0', '\0'};
        return resp;
    });

    mock_doip_transport->setRequestHandler([](uint16_t, const std::vector<uint8_t>& request) {
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
    mock_transport->setRequestHandler([](uint16_t, const std::vector<uint8_t>& request) {
        TransportResponse resp;
        resp.success = true;
        resp.data = {0x62, request[1], request[2], '1', '.', '0', '\0'};
        return resp;
    });

    mock_doip_transport->setRequestHandler([](uint16_t, const std::vector<uint8_t>& request) {
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

TEST_F(VersionCollectorTest, SeedModeUsesRecordAndSkipsDownlink) {
    const std::string seed_path = (temp_dir_ / "test_seed.json").string();
    const std::string state_path = (temp_dir_ / "test_inventory.json").string();
    {
        std::ofstream seed(seed_path);
        seed << R"({
  "schemaVersion": 1,
  "registryVersion": "2026.07.0",
  "baselineCode": "BASE-9",
  "ecus": [
    {"ecuId":"BMS","partNumber":"PN-BMS","swVersion":"1.2.3","hwVersion":"HW-BMS"},
    {"ecuId":"IDCM","swVersion":"4.5.6"}
  ]
})";
    }

    InventoryConfig config;
    config.mode = InventoryMode::SEED;
    config.seed_path = seed_path;
    config.state_path = state_path;
    config.idempotency_window = 8;
    auto store = std::make_shared<EcuInventoryStore>(
        config, std::vector<std::string>{"BMS", "IDCM"}, nullptr, false);
    ASSERT_TRUE(store->initialize("2026.07.0").ok());

    vin_store->writeVin("1234567890ABCDEFG");
    VersionCollector seed_collector(registry, router, mock_transport,
                                    mock_doip_transport, vin_store, store);
    const auto snapshot = seed_collector.collectVehicleInventory();

    EXPECT_EQ(snapshot.schema_version, 1u);
    EXPECT_EQ(snapshot.inventory_source, InventorySource::SEED);
    EXPECT_EQ(snapshot.registry_version, "2026.07.0");
    EXPECT_EQ(snapshot.overall_result, CollectionStatus::ALL_OK);
    EXPECT_EQ(snapshot.baseline_id.value_or(""), "BASE-9");
    EXPECT_EQ(snapshot.baseline_source, BaselineSource::UNKNOWN);
    ASSERT_EQ(snapshot.ecu_list.size(), 2u);
    EXPECT_EQ(snapshot.ecu_list[0].ecu_id, "BMS");
    EXPECT_EQ(snapshot.ecu_list[0].sw_version.value_or(""), "1.2.3");
    EXPECT_EQ(snapshot.ecu_list[0].part_number.value_or(""), "PN-BMS");
    EXPECT_EQ(snapshot.ecu_list[0].status, EcuStatus::OK);
    EXPECT_EQ(snapshot.ecu_list[1].source, ReadMethod::UDS_0x22);

    // No downlink diagnostic request was issued by the seed path.
    EXPECT_TRUE(mock_transport->captured_requests.empty());
    EXPECT_TRUE(mock_doip_transport->captured_requests.empty());

    // Single-ECU read also answers from the record in seed mode.
    const auto entry = seed_collector.collectEcuVersion("BMS");
    EXPECT_EQ(entry.sw_version.value_or(""), "1.2.3");
    EXPECT_TRUE(mock_transport->captured_requests.empty());

    std::filesystem::remove(seed_path);
    std::filesystem::remove(state_path);
}

TEST_F(VersionCollectorTest, SeedModeMissingVersionIsMarkedNotOk) {
    const std::string seed_path = (temp_dir_ / "test_seed_missing.json").string();
    const std::string state_path = (temp_dir_ / "test_inventory_missing.json").string();
    {
        std::ofstream seed(seed_path);
        seed << R"({
  "schemaVersion": 1,
  "registryVersion": "2026.07.0",
  "ecus": [
    {"ecuId":"BMS","partNumber":"PN-BMS"},
    {"ecuId":"IDCM","swVersion":"4.5.6"}
  ]
})";
    }

    InventoryConfig config;
    config.mode = InventoryMode::SEED;
    config.seed_path = seed_path;
    config.state_path = state_path;
    auto store = std::make_shared<EcuInventoryStore>(
        config, std::vector<std::string>{"BMS", "IDCM"}, nullptr, false);
    ASSERT_TRUE(store->initialize("2026.07.0").ok());

    VersionCollector seed_collector(registry, router, mock_transport,
                                    mock_doip_transport, vin_store, store);
    const auto snapshot = seed_collector.collectVehicleInventory();

    EXPECT_EQ(snapshot.overall_result, CollectionStatus::PARTIAL);
    ASSERT_EQ(snapshot.ecu_list.size(), 2u);
    EXPECT_EQ(snapshot.ecu_list[0].status, EcuStatus::MISSING);
    EXPECT_EQ(snapshot.ecu_list[0].error_code.value_or(""), "CCU-DIAG-1004");

    std::filesystem::remove(seed_path);
    std::filesystem::remove(state_path);
}

TEST_F(VersionCollectorTest, SeedModeUnavailableThrowsExplicitError) {
    InventoryConfig config;
    config.mode = InventoryMode::SEED;
    config.seed_path = (temp_dir_ / "missing_seed.json").string();
    config.state_path = (temp_dir_ / "missing_state.json").string();
    auto store = std::make_shared<EcuInventoryStore>(
        config, std::vector<std::string>{"BMS"}, nullptr, false);
    ASSERT_FALSE(store->initialize("2026.07.0").ok());

    VersionCollector seed_collector(registry, router, mock_transport,
                                    mock_doip_transport, vin_store, store);
    try {
        seed_collector.collectVehicleInventory();
        FAIL() << "expected DiagException";
    } catch (const DiagException& e) {
        EXPECT_EQ(e.code(), ErrorCode::INVENTORY_SEED_INVALID);
    }
}

} // namespace tests
} // namespace ccu_diag
