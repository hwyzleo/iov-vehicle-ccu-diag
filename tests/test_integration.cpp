#include <gtest/gtest.h>
#include "config_loader.h"
#include "did_registry.h"
#include "uds_client.h"
#include "diagnostic_router.h"
#include "do_can_transport.h"
#include "doip_transport.h"
#include "version_collector.h"
#include "fota_facade.h"
#include "error_codes.h"
#include "mock_transport.h"
#include "fota_someip_service.h"
#include "fota_someip_proxy.h"
#include "someip_service.h"
#include "someip_client_proxy.h"
#include "vin_store.h"
#include <fstream>
#include <memory>
#include <filesystem>
#include <algorithm>
#include <nlohmann/json.hpp>

namespace cgw_diag {
namespace tests {

class IntegrationTest : public ::testing::Test {
protected:
    void SetUp() override {
        temp_dir_ = std::filesystem::temp_directory_path();
        config_path_ = (temp_dir_ / "test_integration.yaml").string();
        createTestConfig();

        registry = std::make_shared<DidRegistry>(config_path_);

        mock_docan = std::make_shared<MockTransport>();
        mock_docan->setTransportType("DoCAN");

        mock_doip = std::make_shared<MockTransport>();
        mock_doip->setTransportType("DoIP");

        DiagConfig diag_config;
        diag_config.max_retry_count = 2;
        diag_config.request_timeout_ms = 2000;
        diag_config.p2_server_max_ms = 50;
        diag_config.p2_star_server_max_ms = 5000;

        uds_client = std::make_shared<UdsClient>(mock_docan, diag_config);

        router = std::make_shared<DiagnosticRouter>(
            registry, uds_client, mock_docan, mock_doip);

        vin_store_path_ = (temp_dir_ / "test_vin_store.dat").string();
        vin_store = std::shared_ptr<VinStore>(createVinStore(vin_store_path_).release());

        collector = std::make_shared<VersionCollector>(
            registry, router, mock_docan, mock_doip, vin_store);

        facade = std::make_unique<FotaFacade>(collector, registry, vin_store);
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
      - did: 0xF193
        field: "hwVersion"
        encoding: "ASCII"
        length: 8
  - ecuId: "VCU"
    logicalAddress: 0x21
    transport: "DoCAN"
    readMethod: "UDS_0x22"
    securityRequired: false
    dids:
      - did: 0xF195
        field: "swVersion"
        encoding: "ASCII"
        length: 16
      - did: 0xF188
        field: "ecuSoftwareNumber"
        encoding: "ASCII"
        length: 11
      - did: 0xF193
        field: "hwVersion"
        encoding: "ASCII"
        length: 8
)";
        file.close();
    }

    std::filesystem::path temp_dir_;
    std::string config_path_;
    std::string vin_store_path_;
    std::shared_ptr<DidRegistry> registry;
    std::shared_ptr<MockTransport> mock_docan;
    std::shared_ptr<MockTransport> mock_doip;
    std::shared_ptr<UdsClient> uds_client;
    std::shared_ptr<DiagnosticRouter> router;
    std::shared_ptr<VinStore> vin_store;
    std::shared_ptr<VersionCollector> collector;
    std::unique_ptr<FotaFacade> facade;
};

TEST_F(IntegrationTest, FullFlowAllEcusOk) {
    // UdsClient is bound to mock_docan, so all UDS responses come from docan handler.
    // DoIP transport is used only for reachability checks by the router.
    mock_docan->setRequestHandler([](uint16_t addr, const std::vector<uint8_t>& request) {
        TransportResponse resp;
        resp.success = true;
        uint16_t did = (request[1] << 8) | request[2];
        resp.data = {0x62, request[1], request[2]};
        if (addr == 0x1A) {
            if (did == 0xF195) {
                resp.data.insert(resp.data.end(), {'1', '.', '0', '.', '0', '\0'});
            } else if (did == 0xF187) {
                resp.data.insert(resp.data.end(), {'P', 'A', 'R', 'T', '-', 'B', 'M', 'S', '\0'});
            }
        } else if (addr == 0x20) {
            if (did == 0xF195) {
                resp.data.insert(resp.data.end(), {'2', '.', '0', '.', '0', '\0'});
            } else if (did == 0xF193) {
                resp.data.insert(resp.data.end(), {'H', 'W', '-', 'I', 'D', 'C', 'M', '\0'});
            }
        } else if (addr == 0x21) {
            if (did == 0xF195) {
                resp.data.insert(resp.data.end(), {'3', '.', '1', '.', '0', '\0'});
            } else if (did == 0xF188) {
                resp.data.insert(resp.data.end(), {'S', 'W', 'N', '-', 'V', 'C', 'U', '\0'});
            } else if (did == 0xF193) {
                resp.data.insert(resp.data.end(), {'H', 'W', '-', 'V', '1', '\0'});
            }
        }
        return resp;
    });

    collector->setBaselineInfo("BASELINE_V1.0", BaselineSource::FACTORY);

    vin_store->writeVin("1HGBH41JXMN109186");

    auto snapshot = facade->collectVehicleInventory();

    EXPECT_EQ(snapshot.vin, "1HGBH41JXMN109186");
    EXPECT_EQ(snapshot.vin_source, VinSource::PROVISIONED);
    EXPECT_EQ(snapshot.baseline_id, "BASELINE_V1.0");
    EXPECT_EQ(snapshot.baseline_source, BaselineSource::FACTORY);
    EXPECT_EQ(snapshot.registry_version, "2026.07.0");
    EXPECT_FALSE(snapshot.collected_at.empty());
    EXPECT_EQ(snapshot.overall_result, CollectionStatus::ALL_OK);

    ASSERT_EQ(snapshot.ecu_list.size(), 3);

    auto bms_it = std::find_if(snapshot.ecu_list.begin(), snapshot.ecu_list.end(),
                               [](const EcuVersionEntry& e) { return e.ecu_id == "BMS"; });
    ASSERT_NE(bms_it, snapshot.ecu_list.end());
    EXPECT_EQ(bms_it->status, EcuStatus::OK);
    EXPECT_EQ(bms_it->source, ReadMethod::UDS_0x22);
    EXPECT_TRUE(bms_it->sw_version.has_value());
    EXPECT_EQ(bms_it->sw_version.value(), "1.0.0");
    EXPECT_TRUE(bms_it->part_number.has_value());
    EXPECT_EQ(bms_it->part_number.value(), "PART-BMS");

    auto idcm_it = std::find_if(snapshot.ecu_list.begin(), snapshot.ecu_list.end(),
                                [](const EcuVersionEntry& e) { return e.ecu_id == "IDCM"; });
    ASSERT_NE(idcm_it, snapshot.ecu_list.end());
    EXPECT_EQ(idcm_it->status, EcuStatus::OK);
    EXPECT_EQ(idcm_it->source, ReadMethod::UDS_0x22);
    EXPECT_TRUE(idcm_it->sw_version.has_value());
    EXPECT_EQ(idcm_it->sw_version.value(), "2.0.0");
    EXPECT_TRUE(idcm_it->hw_version.has_value());
    EXPECT_EQ(idcm_it->hw_version.value(), "HW-IDCM");

    auto vcu_it = std::find_if(snapshot.ecu_list.begin(), snapshot.ecu_list.end(),
                               [](const EcuVersionEntry& e) { return e.ecu_id == "VCU"; });
    ASSERT_NE(vcu_it, snapshot.ecu_list.end());
    EXPECT_EQ(vcu_it->status, EcuStatus::OK);
    EXPECT_TRUE(vcu_it->sw_version.has_value());
    EXPECT_EQ(vcu_it->sw_version.value(), "3.1.0");
    EXPECT_TRUE(vcu_it->hw_version.has_value());
    EXPECT_EQ(vcu_it->hw_version.value(), "HW-V1");
}

TEST_F(IntegrationTest, FullFlowPartialSuccess) {
    mock_docan->setRequestHandler([](uint16_t addr, const std::vector<uint8_t>& request) {
        TransportResponse resp;
        if (addr == 0x1A) {
            resp.success = true;
            resp.data = {0x62, request[1], request[2], '1', '.', '0', '\0'};
        } else {
            resp.success = false;
            resp.error_message = "VCU unreachable";
        }
        return resp;
    });

    mock_doip->setRequestHandler([](uint16_t, const std::vector<uint8_t>&) {
        TransportResponse resp;
        resp.success = false;
        resp.error_message = "DoIP timeout";
        return resp;
    });

    auto snapshot = facade->collectVehicleInventory();

    EXPECT_EQ(snapshot.overall_result, CollectionStatus::PARTIAL);
    EXPECT_EQ(snapshot.ecu_list.size(), 3);

    auto bms_it = std::find_if(snapshot.ecu_list.begin(), snapshot.ecu_list.end(),
                               [](const EcuVersionEntry& e) { return e.ecu_id == "BMS"; });
    ASSERT_NE(bms_it, snapshot.ecu_list.end());
    EXPECT_EQ(bms_it->status, EcuStatus::OK);

    auto idcm_it = std::find_if(snapshot.ecu_list.begin(), snapshot.ecu_list.end(),
                                [](const EcuVersionEntry& e) { return e.ecu_id == "IDCM"; });
    ASSERT_NE(idcm_it, snapshot.ecu_list.end());
    EXPECT_EQ(idcm_it->status, EcuStatus::TIMEOUT);

    auto vcu_it = std::find_if(snapshot.ecu_list.begin(), snapshot.ecu_list.end(),
                               [](const EcuVersionEntry& e) { return e.ecu_id == "VCU"; });
    ASSERT_NE(vcu_it, snapshot.ecu_list.end());
    EXPECT_EQ(vcu_it->status, EcuStatus::TIMEOUT);
}

TEST_F(IntegrationTest, FullFlowAllFailed) {
    mock_docan->setRequestHandler([](uint16_t, const std::vector<uint8_t>&) {
        TransportResponse resp;
        resp.success = false;
        resp.error_message = "CAN bus error";
        return resp;
    });

    mock_doip->setRequestHandler([](uint16_t, const std::vector<uint8_t>&) {
        TransportResponse resp;
        resp.success = false;
        resp.error_message = "Connection refused";
        return resp;
    });

    auto snapshot = facade->collectVehicleInventory();

    EXPECT_EQ(snapshot.overall_result, CollectionStatus::FAILED);
    for (const auto& entry : snapshot.ecu_list) {
        EXPECT_NE(entry.status, EcuStatus::OK);
    }
}

TEST_F(IntegrationTest, EcuVersionSingleEcu) {
    mock_docan->setRequestHandler([](uint16_t, const std::vector<uint8_t>& request) {
        TransportResponse resp;
        resp.success = true;
        uint16_t did = (request[1] << 8) | request[2];
        resp.data = {0x62, request[1], request[2]};
        if (did == 0xF195) {
            resp.data.insert(resp.data.end(), {'5', '.', '0', '.', '0', '\0'});
        } else if (did == 0xF187) {
            resp.data.insert(resp.data.end(), {'P', 'N', '-', 'B', 'M', 'S', '-', '0', '0', '1', '\0'});
        }
        return resp;
    });

    auto entry = facade->getEcuVersion("BMS");

    EXPECT_EQ(entry.ecu_id, "BMS");
    EXPECT_EQ(entry.status, EcuStatus::OK);
    EXPECT_EQ(entry.source, ReadMethod::UDS_0x22);
    EXPECT_TRUE(entry.sw_version.has_value());
    EXPECT_EQ(entry.sw_version.value(), "5.0.0");
    EXPECT_TRUE(entry.part_number.has_value());
    EXPECT_EQ(entry.part_number.value(), "PN-BMS-001");
}

TEST_F(IntegrationTest, EcuVersionNotRegistered) {
    auto entry = facade->getEcuVersion("NONEXISTENT");

    EXPECT_EQ(entry.ecu_id, "NONEXISTENT");
    EXPECT_EQ(entry.status, EcuStatus::MISSING);
    EXPECT_TRUE(entry.error_code.has_value());
    EXPECT_EQ(entry.error_code.value(),
              ErrorCodeMapper::toDiagCode(ErrorCode::ECU_NOT_REGISTERED));
}

TEST_F(IntegrationTest, RegistryVersionAccess) {
    auto version = facade->getRegistryVersion();
    EXPECT_EQ(version, "2026.07.0");
}

TEST_F(IntegrationTest, BaselineInfoPropagation) {
    facade->setBaselineInfo("OTA_V2.0", BaselineSource::LAST_OTA);

    mock_docan->setRequestHandler([](uint16_t, const std::vector<uint8_t>& request) {
        TransportResponse resp;
        resp.success = true;
        resp.data = {0x62, request[1], request[2], '1', '.', '0', '\0'};
        return resp;
    });

    mock_doip->setRequestHandler([](uint16_t, const std::vector<uint8_t>& request) {
        TransportResponse resp;
        resp.success = true;
        resp.data = {0x62, request[1], request[2], '2', '.', '0', '\0'};
        return resp;
    });

    auto snapshot = facade->collectVehicleInventory();

    EXPECT_EQ(snapshot.baseline_id, "OTA_V2.0");
    EXPECT_EQ(snapshot.baseline_source, BaselineSource::LAST_OTA);
}

TEST_F(IntegrationTest, NegativeResponseHandling) {
    mock_docan->setRequestHandler([](uint16_t, const std::vector<uint8_t>&) {
        TransportResponse resp;
        resp.success = true;
        resp.data = {0x7F, 0x22, 0x31};  // NRC 0x31 RequestOutOfRange
        return resp;
    });

    mock_doip->setRequestHandler([](uint16_t, const std::vector<uint8_t>&) {
        TransportResponse resp;
        resp.success = true;
        resp.data = {0x7F, 0x22, 0x31};
        return resp;
    });

    auto snapshot = facade->collectVehicleInventory();

    EXPECT_EQ(snapshot.overall_result, CollectionStatus::FAILED);
    for (const auto& entry : snapshot.ecu_list) {
        EXPECT_EQ(entry.status, EcuStatus::NRC);
        EXPECT_TRUE(entry.error_code.has_value());
    }
}

TEST_F(IntegrationTest, TransportSelectionDocan) {
    mock_docan->setRequestHandler([](uint16_t, const std::vector<uint8_t>& request) {
        TransportResponse resp;
        resp.success = true;
        resp.data = {0x62, request[1], request[2], '1', '.', '0', '\0'};
        return resp;
    });

    facade->getEcuVersion("BMS");

    ASSERT_GE(mock_docan->captured_requests.size(), 1);
    EXPECT_EQ(mock_docan->captured_requests[0].logical_address, 0x1A);
}

TEST_F(IntegrationTest, TransportSelectionDoip) {
    // DoIP transport is used for reachability checks by the router.
    // UDS requests go through the UdsClient's transport (docan).
    mock_doip->setReachable(true);

    EXPECT_TRUE(router->isEcuReachable("IDCM"));

    auto transport = router->getTransportForEcu("IDCM");
    ASSERT_NE(transport, nullptr);
    EXPECT_EQ(transport->getTransportType(), "DoIP");
}

TEST_F(IntegrationTest, ConfigLoaderIntegration) {
    DidRegistryConfig did_config = ConfigLoader::loadDidRegistry(config_path_);

    EXPECT_EQ(did_config.registry_version, "2026.07.0");
    EXPECT_EQ(did_config.baseline_dids.size(), 4);
    EXPECT_EQ(did_config.ecus.size(), 3);

    auto bms_it = std::find_if(did_config.ecus.begin(), did_config.ecus.end(),
                               [](const EcuConfig& e) { return e.ecu_id == "BMS"; });
    ASSERT_NE(bms_it, did_config.ecus.end());
    EXPECT_EQ(bms_it->logical_address, 0x1A);
    EXPECT_EQ(bms_it->transport, TransportType::DOCAN);
    EXPECT_EQ(bms_it->read_method, ReadMethod::UDS_0x22);
    EXPECT_EQ(bms_it->dids.size(), 2);

    auto idcm_it = std::find_if(did_config.ecus.begin(), did_config.ecus.end(),
                                [](const EcuConfig& e) { return e.ecu_id == "IDCM"; });
    ASSERT_NE(idcm_it, did_config.ecus.end());
    EXPECT_EQ(idcm_it->transport, TransportType::DOIP);
}

TEST_F(IntegrationTest, EmptyVinHandling) {
    auto snapshot = facade->collectVehicleInventory();

    EXPECT_EQ(snapshot.vin.empty(), true);
    EXPECT_EQ(snapshot.vin_source, VinSource::UNKNOWN);
    EXPECT_EQ(snapshot.ecu_list.size(), 3);
}

TEST_F(IntegrationTest, EmptyEcuIdHandling) {
    auto entry = facade->getEcuVersion("");

    EXPECT_EQ(entry.ecu_id.empty(), true);
    EXPECT_EQ(entry.status, EcuStatus::MISSING);
}

TEST_F(IntegrationTest, DidRegistryReload) {
    mock_docan->setRequestHandler([](uint16_t, const std::vector<uint8_t>& request) {
        TransportResponse resp;
        resp.success = true;
        resp.data = {0x62, request[1], request[2], '1', '.', '0', '\0'};
        return resp;
    });

    auto entry1 = facade->getEcuVersion("BMS");
    EXPECT_EQ(entry1.status, EcuStatus::OK);

    std::ofstream file(config_path_);
    file << R"(
registryVersion: "2026.08.0"
baselineDids: [0xF195]
ecus:
  - ecuId: "NEW_ECU"
    logicalAddress: 0x50
    transport: "DoCAN"
    readMethod: "UDS_0x22"
    securityRequired: false
    dids:
      - did: 0xF195
        field: "swVersion"
        encoding: "ASCII"
        length: 16
)";
    file.close();

    registry->reload();

    EXPECT_FALSE(registry->isEcuRegistered("BMS"));
    EXPECT_TRUE(registry->isEcuRegistered("NEW_ECU"));
    EXPECT_EQ(registry->getRegistryVersion(), "2026.08.0");
}

class SomeIpIntegrationTest : public ::testing::Test {
protected:
    void SetUp() override {
        temp_dir_ = std::filesystem::temp_directory_path();
        config_path_ = (temp_dir_ / "test_someip_integration.yaml").string();
        createTestConfig();

        service_config_.service_id = 0x1234;
        service_config_.instance_id = 0x0001;
        service_config_.major_version = 1;
        service_config_.minor_version = 0;
        service_config_.ip_address = "127.0.0.1";
        service_config_.port = 30501;
    }

    void TearDown() override {
        std::filesystem::remove(config_path_);
    }

    void createTestConfig() {
        std::ofstream file(config_path_);
        file << R"(
registryVersion: "2026.07.0"
baselineDids: [0xF195]
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
)";
        file.close();
    }

    std::filesystem::path temp_dir_;
    std::string config_path_;
    SomeIpServiceConfig service_config_;
};

TEST_F(SomeIpIntegrationTest, ServiceAndProxyCommunication) {
    auto registry = std::make_shared<DidRegistry>(config_path_);
    DiagConfig diag_config;
    auto mock_transport = std::make_shared<MockTransport>();
    auto uds_client = std::make_shared<UdsClient>(mock_transport, diag_config);
    auto router = std::make_shared<DiagnosticRouter>(registry, uds_client, mock_transport, mock_transport);
    auto vin_store = std::shared_ptr<VinStore>(createVinStore().release());
    auto collector = std::make_shared<VersionCollector>(registry, router, mock_transport, mock_transport, vin_store);
    auto facade = std::make_shared<FotaFacade>(collector, registry, vin_store);

    auto service = createSomeIpService();
    FotaSomeIpService fota_service(facade, std::move(service));
    ASSERT_TRUE(fota_service.initialize(service_config_));
    ASSERT_TRUE(fota_service.start());
    ASSERT_TRUE(fota_service.offerService());

    auto proxy = createSomeIpClientProxy();
    FotaSomeIpProxy fota_proxy(std::move(proxy));
    ASSERT_TRUE(fota_proxy.initialize(service_config_));
    ASSERT_TRUE(fota_proxy.connect());

    // Real TCP mock now forwards requests to handler which returns actual registry version
    auto version = fota_proxy.getRegistryVersion();
    EXPECT_EQ(version, "2026.07.0");

    fota_proxy.disconnect();
    fota_service.stop();
}

TEST_F(SomeIpIntegrationTest, ServiceLifecycle) {
    auto registry = std::make_shared<DidRegistry>(config_path_);
    DiagConfig diag_config;
    auto mock_transport = std::make_shared<MockTransport>();
    auto uds_client = std::make_shared<UdsClient>(mock_transport, diag_config);
    auto router = std::make_shared<DiagnosticRouter>(registry, uds_client, mock_transport, mock_transport);
    auto vin_store = std::shared_ptr<VinStore>(createVinStore().release());
    auto collector = std::make_shared<VersionCollector>(registry, router, mock_transport, mock_transport, vin_store);
    auto facade = std::make_shared<FotaFacade>(collector, registry, vin_store);

    auto service = createSomeIpService();
    FotaSomeIpService fota_service(facade, std::move(service));

    EXPECT_FALSE(fota_service.isRunning());

    ASSERT_TRUE(fota_service.initialize(service_config_));
    ASSERT_TRUE(fota_service.start());
    EXPECT_TRUE(fota_service.isRunning());

    ASSERT_TRUE(fota_service.offerService());

    fota_service.stopOfferService();
    fota_service.stop();
    EXPECT_FALSE(fota_service.isRunning());
}

TEST_F(SomeIpIntegrationTest, ProxyReconnection) {
    auto registry = std::make_shared<DidRegistry>(config_path_);
    DiagConfig diag_config;
    auto mock_transport = std::make_shared<MockTransport>();
    auto uds_client = std::make_shared<UdsClient>(mock_transport, diag_config);
    auto router = std::make_shared<DiagnosticRouter>(registry, uds_client, mock_transport, mock_transport);
    auto vin_store = std::shared_ptr<VinStore>(createVinStore().release());
    auto collector = std::make_shared<VersionCollector>(registry, router, mock_transport, mock_transport, vin_store);
    auto facade = std::make_shared<FotaFacade>(collector, registry, vin_store);

    auto service = createSomeIpService();
    FotaSomeIpService fota_service(facade, std::move(service));
    ASSERT_TRUE(fota_service.initialize(service_config_));
    ASSERT_TRUE(fota_service.start());
    ASSERT_TRUE(fota_service.offerService());

    auto proxy = createSomeIpClientProxy();
    FotaSomeIpProxy fota_proxy(std::move(proxy));
    ASSERT_TRUE(fota_proxy.initialize(service_config_));

    ASSERT_TRUE(fota_proxy.connect());
    EXPECT_TRUE(fota_proxy.isConnected());

    fota_proxy.disconnect();
    EXPECT_FALSE(fota_proxy.isConnected());

    ASSERT_TRUE(fota_proxy.connect());
    EXPECT_TRUE(fota_proxy.isConnected());

    fota_proxy.disconnect();
    fota_service.stop();
}

TEST_F(SomeIpIntegrationTest, ProxyGetEcuVersion) {
    auto registry = std::make_shared<DidRegistry>(config_path_);
    DiagConfig diag_config;
    auto mock_transport = std::make_shared<MockTransport>();
    mock_transport->setRequestHandler([](uint16_t, const std::vector<uint8_t>& request) {
        TransportResponse resp;
        resp.success = true;
        resp.data = {0x62, request[1], request[2], '1', '.', '0', '.', '0', '\0'};
        return resp;
    });
    auto uds_client = std::make_shared<UdsClient>(mock_transport, diag_config);
    auto router = std::make_shared<DiagnosticRouter>(registry, uds_client, mock_transport, mock_transport);
    auto vin_store = std::shared_ptr<VinStore>(createVinStore().release());
    auto collector = std::make_shared<VersionCollector>(registry, router, mock_transport, mock_transport, vin_store);
    auto facade = std::make_shared<FotaFacade>(collector, registry, vin_store);

    auto service = createSomeIpService();
    FotaSomeIpService fota_service(facade, std::move(service));
    ASSERT_TRUE(fota_service.initialize(service_config_));
    ASSERT_TRUE(fota_service.start());
    ASSERT_TRUE(fota_service.offerService());

    auto proxy = createSomeIpClientProxy();
    FotaSomeIpProxy fota_proxy(std::move(proxy));
    ASSERT_TRUE(fota_proxy.initialize(service_config_));
    ASSERT_TRUE(fota_proxy.connect());

    auto entry = fota_proxy.getEcuVersion("BMS");
    EXPECT_EQ(entry.ecu_id, "BMS");

    fota_proxy.disconnect();
    fota_service.stop();
}

} // namespace tests
} // namespace cgw_diag
