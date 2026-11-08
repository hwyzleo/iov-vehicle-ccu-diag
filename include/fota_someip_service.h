#pragma once

#include "someip_service.h"
#include "fota_facade.h"
#include "someip_types.h"
#include <memory>

namespace ccu_diag {

class FotaSomeIpService {
public:
    FotaSomeIpService(std::shared_ptr<FotaFacade> facade,
                      std::unique_ptr<SomeIpService> service);

    bool initialize(const SomeIpServiceConfig& config);
    bool start();
    void stop();
    bool isRunning() const;

    bool offerService();
    void stopOfferService();

    uint16_t getBoundPort() const;

    SomeIpMessage handleReadVin(const SomeIpMessage& request);

private:
    std::shared_ptr<FotaFacade> facade_;
    std::unique_ptr<SomeIpService> service_;

    SomeIpMessage handleCollectVehicleInventory(const SomeIpMessage& request);
    SomeIpMessage handleGetEcuVersion(const SomeIpMessage& request);
    SomeIpMessage handleGetRegistryVersion(const SomeIpMessage& request);
    SomeIpMessage handleRecordInstalledVersions(const SomeIpMessage& request);

    void registerMethodHandlers();
};

} // namespace ccu_diag
