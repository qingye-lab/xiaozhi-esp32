#ifndef NETWORK_CONTROLLER_H
#define NETWORK_CONTROLLER_H

#include <functional>
#include <string>

#include "board.h"

class NetworkController {
public:
    using SwitchRequestCallback =
        std::function<void(NetworkTransport target, NetworkSwitchReason reason)>;

    virtual ~NetworkController() = default;

    virtual void Start() = 0;
    virtual void Stop() = 0;
    virtual bool SetMode(NetworkMode mode) = 0;
    virtual NetworkMode GetMode() const = 0;
    virtual NetworkStatusSnapshot GetStatus() const = 0;
    virtual NetworkInterface* GetNetwork() const = 0;
    virtual const char* GetNetworkStateIcon() const = 0;
    virtual void SetPowerSaveLevel(PowerSaveLevel level) = 0;

    virtual void SetNetworkEventCallback(NetworkEventCallback callback) = 0;
    virtual void SetSwitchRequestCallback(SwitchRequestCallback callback) = 0;
    virtual void SetCellularPowerControl(std::function<bool(bool enabled)> callback) = 0;
    virtual void SetExternalPowerProvider(std::function<bool()> callback) = 0;
    virtual void RefreshPowerPolicy() = 0;

    virtual void CommitSwitch(NetworkTransport target, NetworkSwitchReason reason) = 0;
    virtual void CancelPendingSwitch() = 0;
    virtual void ReportProtocolConnected() = 0;
    virtual void ReportProtocolFailure() = 0;
};

#endif  // NETWORK_CONTROLLER_H
