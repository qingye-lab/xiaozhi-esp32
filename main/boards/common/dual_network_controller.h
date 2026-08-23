#ifndef DUAL_NETWORK_CONTROLLER_H
#define DUAL_NETWORK_CONTROLLER_H

#include <atomic>
#include <mutex>

#include <freertos/FreeRTOS.h>
#include <freertos/event_groups.h>
#include <freertos/task.h>

#include "network_controller.h"
#include "network_policy.h"

class Ml307Board;
class WifiBoard;

class DualNetworkController final : public NetworkController {
public:
    DualNetworkController(WifiBoard& wifi, Ml307Board& cellular);
    ~DualNetworkController() override;

    void Start() override;
    void Stop() override;
    bool SetMode(NetworkMode mode) override;
    NetworkMode GetMode() const override;
    NetworkStatusSnapshot GetStatus() const override;
    NetworkInterface* GetNetwork() const override;
    const char* GetNetworkStateIcon() const override;
    void SetPowerSaveLevel(PowerSaveLevel level) override;

    void SetNetworkEventCallback(NetworkEventCallback callback) override;
    void SetSwitchRequestCallback(SwitchRequestCallback callback) override;
    void SetCellularPowerControl(std::function<bool(bool enabled)> callback) override;
    void SetExternalPowerProvider(std::function<bool()> callback) override;
    void RefreshPowerPolicy() override;

    void CommitSwitch(NetworkTransport target, NetworkSwitchReason reason) override;
    void CancelPendingSwitch() override;
    void ReportProtocolConnected() override;
    void ReportProtocolFailure() override;

private:
    struct ProbeContext {
        DualNetworkController* controller;
        NetworkTransport transport;
        uint32_t generation;
    };

    WifiBoard& wifi_;
    Ml307Board& cellular_;
    mutable std::mutex mutex_;
    NetworkPolicy policy_;
    NetworkEventCallback network_event_callback_;
    SwitchRequestCallback switch_request_callback_;
    std::function<bool(bool)> cellular_power_control_;
    std::function<bool()> external_power_provider_;
    std::string health_check_url_;
    std::atomic<bool> running_{false};
    EventGroupHandle_t lifecycle_events_ = nullptr;
    TaskHandle_t worker_task_ = nullptr;
    uint32_t wifi_generation_ = 0;
    uint32_t cellular_generation_ = 0;
    bool wifi_started_ = false;
    bool cellular_started_ = false;
    bool cellular_powered_ = false;
    bool wifi_probe_in_progress_ = false;
    bool cellular_probe_in_progress_ = false;
    bool switch_request_pending_ = false;

    static void WorkerTaskEntry(void* arg);
    void WorkerTask();
    static void ProbeTaskEntry(void* arg);
    void Probe(NetworkTransport transport, uint32_t generation);

    void StartWifi();
    void StartCellular();
    void StopCellularAndPowerOff();
    void OnTransportEvent(NetworkTransport transport, uint32_t generation, NetworkEvent event,
                          const std::string& data);
    void ReportHealth(NetworkTransport transport, NetworkHealth health);
    void EvaluatePolicy();
    void ScheduleProbe(NetworkTransport transport);
    void ApplyPowerPolicy();
    void NotifyNetworkEvent(NetworkEvent event, const std::string& data = "");
    void LoadAndMigrateSettings();
    void SaveMode(NetworkMode mode);
};

#endif  // DUAL_NETWORK_CONTROLLER_H
