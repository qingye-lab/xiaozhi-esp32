#pragma once

#include "camera.h"
#include "kid_companion_logic.h"

#include <driver/i2c_master.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <array>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>

enum class KidKey { kKey0, kKey1, kKey2, kKey3 };

class KidSensorHub {
public:
    using ExpanderReader = std::function<bool(uint16_t&)>;
    using ButtonCallback = std::function<void(KidKey, kid_companion::ButtonEvent)>;

    KidSensorHub(i2c_master_bus_handle_t bus, ExpanderReader expander_reader,
                 ButtonCallback button_callback);

    void Start();
    bool StabilityAvailable() const;
    bool IsStable() const;
    std::string GetEnvironmentJson() const;

private:
    static void TaskEntry(void* arg);
    void Run();
    bool InitializeAp3216c();
    bool InitializeQma6100p();
    bool ReadAp3216c();
    bool ReadQma6100p();
    bool AddDevice(uint8_t address, i2c_master_dev_handle_t& device);
    static bool WriteRegister(i2c_master_dev_handle_t device, uint8_t reg, uint8_t value);
    static bool ReadRegisters(i2c_master_dev_handle_t device, uint8_t reg, uint8_t* data,
                              size_t length);

    i2c_master_bus_handle_t bus_ = nullptr;
    i2c_master_dev_handle_t ap3216c_ = nullptr;
    i2c_master_dev_handle_t qma6100p_ = nullptr;
    ExpanderReader expander_reader_;
    ButtonCallback button_callback_;
    std::array<kid_companion::ButtonDebouncer, 4> buttons_;
    kid_companion::LightClassifier light_classifier_;
    kid_companion::ProximityClassifier proximity_classifier_;
    kid_companion::MotionClassifier motion_classifier_;

    mutable std::mutex state_mutex_;
    bool ap_available_ = false;
    bool qma_available_ = false;
    uint8_t ap_failures_ = 0;
    uint8_t qma_failures_ = 0;
    bool stable_ = false;
    kid_companion::LightLevel light_ = kid_companion::LightLevel::kNormal;
    bool near_ = false;
};

class ChildSafeCamera : public Camera {
public:
    ChildSafeCamera(Camera* delegate, KidSensorHub* sensors);

    void Arm();
    void Cancel();
    bool IsArmed() const;

    void SetExplainUrl(const std::string& url, const std::string& token) override;
    std::string GetCaptureInstructions() const override;
    bool PrepareCapture(std::string& reason) override;
    bool Capture() override;
    bool SetHMirror(bool enabled) override;
    bool SetVFlip(bool enabled) override;
    bool SetSwapBytes(bool enabled) override;
    std::string Explain(const std::string& question) override;

private:
    static int64_t NowMs();

    Camera* delegate_;
    KidSensorHub* sensors_;
    mutable std::mutex mutex_;
    kid_companion::CameraConsentState consent_;
};

class ChildProfile {
public:
    std::string GetJson() const;
    bool Remember(const std::string& category, const std::string& value, std::string& reason);
    bool Forget(const std::string& category, const std::string& value, std::string& reason);
    void ClearAll();

private:
    static bool IsSafeValue(const std::string& value, size_t max_bytes, std::string& reason);
    static std::string Normalize(const std::string& value);
    static std::string LoadList(const char* key);
    static bool StoreList(const char* key, const std::string& value, size_t max_items, bool remove,
                          std::string& reason);

    mutable std::mutex mutex_;
};
