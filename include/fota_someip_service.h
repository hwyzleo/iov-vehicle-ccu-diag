#pragma once

#include "someip_service.h"
#include "fota_facade.h"
#include "someip_types.h"
#include <memory>

namespace cgw_diag {

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

private:
    std::shared_ptr<FotaFacade> facade_;
    std::unique_ptr<SomeIpService> service_;

    SomeIpMessage handleCollectVehicleInventory(const SomeIpMessage& request);
    SomeIpMessage handleGetEcuVersion(const SomeIpMessage& request);
    SomeIpMessage handleGetRegistryVersion(const SomeIpMessage& request);

    void registerMethodHandlers();
};

} // namespace cgw_diag
