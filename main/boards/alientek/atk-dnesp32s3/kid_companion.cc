#include "kid_companion.h"

#include "settings.h"

#include <esp_log.h>
#include <esp_timer.h>
#include <cJSON.h>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <utility>
#include <vector>

namespace {

constexpr const char* kTag = "KidCompanion";
constexpr uint8_t kAp3216cAddress = 0x1E;
constexpr uint8_t kQma6100pAddress = 0x12;
constexpr uint8_t kQma6100pChipId = 0x90;
constexpr int kI2cTimeoutMs = 30;
constexpr int kSensorPeriodMs = 20;

constexpr const char* kProfileNamespace = "kid_profile";

std::vector<std::string> ParseStringList(const std::string& serialized) {
    std::vector<std::string> values;
    cJSON* array = cJSON_Parse(serialized.c_str());
    if (cJSON_IsArray(array)) {
        cJSON* item = nullptr;
        cJSON_ArrayForEach (item, array) {
            if (cJSON_IsString(item) && item->valuestring != nullptr) {
                values.emplace_back(item->valuestring);
            }
        }
    }
    cJSON_Delete(array);
    return values;
}

std::string SerializeStringList(const std::vector<std::string>& values) {
    cJSON* array = cJSON_CreateArray();
    for (const auto& value : values) {
        cJSON_AddItemToArray(array, cJSON_CreateString(value.c_str()));
    }
    char* text = cJSON_PrintUnformatted(array);
    std::string serialized = text == nullptr ? "[]" : text;
    cJSON_free(text);
    cJSON_Delete(array);
    return serialized;
}

}  // namespace

KidSensorHub::KidSensorHub(i2c_master_bus_handle_t bus, ExpanderReader expander_reader,
                           ButtonCallback button_callback)
    : bus_(bus),
      expander_reader_(std::move(expander_reader)),
      button_callback_(std::move(button_callback)) {
    ap_available_ = AddDevice(kAp3216cAddress, ap3216c_) && InitializeAp3216c();
    qma_available_ = AddDevice(kQma6100pAddress, qma6100p_) && InitializeQma6100p();
    ESP_LOGI(kTag, "optional sensors: AP3216C=%s QMA6100P=%s",
             ap_available_ ? "ready" : "unavailable", qma_available_ ? "ready" : "unavailable");
}

bool KidSensorHub::AddDevice(uint8_t address, i2c_master_dev_handle_t& device) {
    i2c_device_config_t config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = address,
        .scl_speed_hz = 400 * 1000,
        .scl_wait_us = 0,
        .flags = {.disable_ack_check = 0},
    };
    esp_err_t error = i2c_master_bus_add_device(bus_, &config, &device);
    if (error != ESP_OK) {
        ESP_LOGW(kTag, "failed to add optional I2C device 0x%02x: %s", address,
                 esp_err_to_name(error));
        device = nullptr;
        return false;
    }
    return true;
}

bool KidSensorHub::WriteRegister(i2c_master_dev_handle_t device, uint8_t reg, uint8_t value) {
    if (device == nullptr) {
        return false;
    }
    uint8_t data[] = {reg, value};
    return i2c_master_transmit(device, data, sizeof(data), kI2cTimeoutMs) == ESP_OK;
}

bool KidSensorHub::ReadRegisters(i2c_master_dev_handle_t device, uint8_t reg, uint8_t* data,
                                 size_t length) {
    if (device == nullptr) {
        return false;
    }
    return i2c_master_transmit_receive(device, &reg, 1, data, length, kI2cTimeoutMs) == ESP_OK;
}

bool KidSensorHub::InitializeAp3216c() {
    // The official board example waits 50 RTOS ticks at 100 Hz after reset,
    // which is 500 ms rather than the 50 ms stated in its comment. Keep the
    // retry count finite so an absent optional sensor never blocks startup.
    for (int attempt = 1; attempt <= 2; ++attempt) {
        uint8_t mode = 0;
        bool configured = WriteRegister(ap3216c_, 0x00, 0x04);
        if (configured) {
            vTaskDelay(pdMS_TO_TICKS(500));
            configured = WriteRegister(ap3216c_, 0x00, 0x03);
        }
        if (configured) {
            vTaskDelay(pdMS_TO_TICKS(20));
            configured = ReadRegisters(ap3216c_, 0x00, &mode, 1) && mode == 0x03;
        }
        if (configured) {
            return true;
        }
        ESP_LOGW(kTag, "AP3216C initialization attempt %d failed (mode=0x%02x)", attempt, mode);
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    return false;
}

bool KidSensorHub::InitializeQma6100p() {
    uint8_t chip_id = 0;
    if (!ReadRegisters(qma6100p_, 0x00, &chip_id, 1) || chip_id != kQma6100pChipId) {
        return false;
    }
    if (!WriteRegister(qma6100p_, 0x36, 0xB6)) {
        return false;
    }
    vTaskDelay(pdMS_TO_TICKS(5));
    if (!WriteRegister(qma6100p_, 0x36, 0x00)) {
        return false;
    }
    vTaskDelay(pdMS_TO_TICKS(10));

    // Clean-room initialization from the public register contract: active digital
    // mode, 8 g range, and a modest 25 Hz bandwidth for stability detection.
    const std::array<std::pair<uint8_t, uint8_t>, 9> setup = {{
        {0x11, 0x80},
        {0x11, 0x84},
        {0x4A, 0x20},
        {0x56, 0x01},
        {0x5F, 0x80},
        {0x5F, 0x00},
        {0x0F, 0x04},
        {0x10, 0x06},
        {0x11, 0x84},
    }};
    for (const auto& [reg, value] : setup) {
        if (!WriteRegister(qma6100p_, reg, value)) {
            return false;
        }
        if (reg == 0x5F) {
            vTaskDelay(pdMS_TO_TICKS(value == 0x80 ? 1 : 10));
        }
    }
    return ReadRegisters(qma6100p_, 0x00, &chip_id, 1) && chip_id == kQma6100pChipId;
}

bool KidSensorHub::ReadAp3216c() {
    uint8_t data[6] = {};
    if (!ReadRegisters(ap3216c_, 0x0A, data, sizeof(data))) {
        return false;
    }
    uint16_t ambient = (static_cast<uint16_t>(data[3]) << 8) | data[2];
    uint16_t proximity = 0;
    if ((data[4] & 0x40) == 0) {
        proximity = (static_cast<uint16_t>(data[5] & 0x3F) << 4) | (data[4] & 0x0F);
    }

    std::lock_guard<std::mutex> lock(state_mutex_);
    light_ = light_classifier_.Update(ambient);
    near_ = proximity_classifier_.Update(proximity);
    return true;
}

bool KidSensorHub::ReadQma6100p() {
    uint8_t data[6] = {};
    if (!ReadRegisters(qma6100p_, 0x01, data, sizeof(data))) {
        return false;
    }
    auto decode = [&data](size_t offset) {
        int16_t packed =
            static_cast<int16_t>((static_cast<uint16_t>(data[offset + 1]) << 8) | data[offset]);
        return static_cast<int16_t>(packed >> 2);
    };

    std::lock_guard<std::mutex> lock(state_mutex_);
    stable_ = motion_classifier_.Update(decode(0), decode(2), decode(4));
    return true;
}

void KidSensorHub::Start() {
    BaseType_t result = xTaskCreate(TaskEntry, "kid_sensor_hub", 4096, this, 1, nullptr);
    if (result != pdPASS) {
        ESP_LOGE(kTag, "failed to start sensor hub task");
    }
}

void KidSensorHub::TaskEntry(void* arg) { static_cast<KidSensorHub*>(arg)->Run(); }

void KidSensorHub::Run() {
    TickType_t last_wake = xTaskGetTickCount();
    uint32_t tick = 0;
    while (true) {
        int64_t now_ms = esp_timer_get_time() / 1000;
        uint16_t inputs = 0xFFFF;
        if (expander_reader_ && expander_reader_(inputs)) {
            constexpr std::array<uint16_t, 4> masks = {0x8000, 0x4000, 0x2000, 0x1000};
            constexpr std::array<KidKey, 4> keys = {KidKey::kKey0, KidKey::kKey1, KidKey::kKey2,
                                                    KidKey::kKey3};
            for (size_t index = 0; index < keys.size(); ++index) {
                auto event = buttons_[index].Update((inputs & masks[index]) == 0, now_ms);
                if (event != kid_companion::ButtonEvent::kNone && button_callback_) {
                    button_callback_(keys[index], event);
                }
            }
        }

        if (qma_available_ && tick % 2 == 0) {
            if (ReadQma6100p()) {
                qma_failures_ = 0;
            } else if (++qma_failures_ >= 3) {
                std::lock_guard<std::mutex> lock(state_mutex_);
                qma_available_ = false;
                stable_ = false;
                ESP_LOGW(kTag, "QMA6100P disabled after repeated read failures");
            }
        }
        if (ap_available_ && tick % 10 == 0) {
            if (ReadAp3216c()) {
                ap_failures_ = 0;
            } else if (++ap_failures_ >= 3) {
                std::lock_guard<std::mutex> lock(state_mutex_);
                ap_available_ = false;
                ESP_LOGW(kTag, "AP3216C disabled after repeated read failures");
            }
        }
        ++tick;
        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(kSensorPeriodMs));
    }
}

bool KidSensorHub::StabilityAvailable() const {
    std::lock_guard<std::mutex> lock(state_mutex_);
    return qma_available_;
}

bool KidSensorHub::IsStable() const {
    std::lock_guard<std::mutex> lock(state_mutex_);
    return stable_;
}

std::string KidSensorHub::GetEnvironmentJson() const {
    std::lock_guard<std::mutex> lock(state_mutex_);
    cJSON* root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "light",
                            kid_companion::EnvironmentLightName(ap_available_, light_));
    if (ap_available_) {
        cJSON_AddBoolToObject(root, "near", near_);
    } else {
        cJSON_AddNullToObject(root, "near");
    }
    cJSON_AddStringToObject(root, "motion",
                            qma_available_ ? (stable_ ? "stable" : "moving") : "unknown");
    cJSON* available = cJSON_CreateObject();
    cJSON_AddBoolToObject(available, "ap3216c", ap_available_);
    cJSON_AddBoolToObject(available, "qma6100p", qma_available_);
    cJSON_AddItemToObject(root, "available", available);
    char* text = cJSON_PrintUnformatted(root);
    std::string result = text == nullptr ? "{}" : text;
    cJSON_free(text);
    cJSON_Delete(root);
    return result;
}

ChildSafeCamera::ChildSafeCamera(Camera* delegate, KidSensorHub* sensors)
    : delegate_(delegate), sensors_(sensors) {}

int64_t ChildSafeCamera::NowMs() { return esp_timer_get_time() / 1000; }

void ChildSafeCamera::Request() {
    std::lock_guard<std::mutex> lock(mutex_);
    consent_.Request(NowMs());
}

bool ChildSafeCamera::Arm(std::string& reason) {
    std::lock_guard<std::mutex> lock(mutex_);
    return consent_.Arm(NowMs(), reason);
}

void ChildSafeCamera::Cancel() {
    std::lock_guard<std::mutex> lock(mutex_);
    consent_.Cancel();
}

bool ChildSafeCamera::IsArmed() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return consent_.IsArmed(NowMs());
}

bool ChildSafeCamera::IsRequestPending() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return consent_.IsRequestPending(NowMs());
}

void ChildSafeCamera::SetExplainUrl(const std::string& url, const std::string& token) {
    delegate_->SetExplainUrl(url, token);
}

std::string ChildSafeCamera::GetCaptureInstructions() const {
    return "孩子先用语音提出拍照请求；调用 self.camera.request_photo 登记本次请求后，再请孩子"
           "按 KEY2。按键确认仅在 20 秒内对一张照片有效，随后孩子说‘拍吧’才能调用本工具。";
}

bool ChildSafeCamera::PrepareCapture(std::string& reason) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (capture_in_progress_) {
        reason = "相机正在处理上一张照片，请稍等。";
        return false;
    }
    bool available = sensors_ != nullptr && sensors_->StabilityAvailable();
    bool stable = sensors_ == nullptr || sensors_->IsStable();
    return consent_.Prepare(NowMs(), available, stable, reason);
}

bool ChildSafeCamera::Capture() {
    std::string reason;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (capture_in_progress_) {
            return false;
        }
        bool available = sensors_ != nullptr && sensors_->StabilityAvailable();
        bool stable = sensors_ == nullptr || sensors_->IsStable();
        if (!consent_.Prepare(NowMs(), available, stable, reason)) {
            return false;
        }
        capture_in_progress_ = true;
    }
    bool captured = delegate_->Capture();
    {
        std::lock_guard<std::mutex> lock(mutex_);
        capture_in_progress_ = false;
        if (captured) {
            consent_.Consume();
        }
    }
    return captured;
}

bool ChildSafeCamera::SetHMirror(bool enabled) { return delegate_->SetHMirror(enabled); }

bool ChildSafeCamera::SetVFlip(bool enabled) { return delegate_->SetVFlip(enabled); }

bool ChildSafeCamera::SetSwapBytes(bool enabled) { return delegate_->SetSwapBytes(enabled); }

std::string ChildSafeCamera::Explain(const std::string& question) {
    return delegate_->Explain(question);
}

std::string ChildProfile::Normalize(const std::string& value) {
    size_t first = 0;
    while (first < value.size() && std::isspace(static_cast<unsigned char>(value[first]))) {
        ++first;
    }
    size_t last = value.size();
    while (last > first && std::isspace(static_cast<unsigned char>(value[last - 1]))) {
        --last;
    }
    return value.substr(first, last - first);
}

bool ChildProfile::IsSafeValue(const std::string& value, size_t max_bytes, std::string& reason) {
    if (value.empty() || value.size() > max_bytes) {
        reason = "内容为空或过长，未保存。";
        return false;
    }
    size_t digit_count = std::count_if(value.begin(), value.end(),
                                       [](unsigned char ch) { return std::isdigit(ch) != 0; });
    constexpr std::array<const char*, 12> blocked = {
        "学校", "小学", "中学", "住址", "地址", "电话",
        "手机", "密码", "账号", "秘密", "照片", "http",
    };
    if (digit_count >= 6 || value.find('@') != std::string::npos ||
        std::any_of(blocked.begin(), blocked.end(),
                    [&value](const char* word) { return value.find(word) != std::string::npos; })) {
        reason = "这类内容可能包含学校、地址、联系方式或秘密，不能写入成长记忆。";
        return false;
    }
    return true;
}

std::string ChildProfile::LoadList(const char* key) {
    Settings settings(kProfileNamespace, false);
    return settings.GetString(key, "[]");
}

bool ChildProfile::StoreList(const char* key, const std::string& value, size_t max_items,
                             bool remove, std::string& reason) {
    auto values = ParseStringList(LoadList(key));
    auto existing = std::find(values.begin(), values.end(), value);
    if (remove) {
        if (value.empty()) {
            values.clear();
        } else if (existing != values.end()) {
            values.erase(existing);
        }
    } else if (existing == values.end()) {
        if (values.size() >= max_items) {
            reason = "成长记忆的这一类已经达到上限，请先删除旧内容。";
            return false;
        }
        values.push_back(value);
    }
    Settings settings(kProfileNamespace, true);
    settings.SetString(key, SerializeStringList(values));
    settings.SetInt("schema", 1);
    return true;
}

std::string ChildProfile::GetJson() const {
    std::lock_guard<std::mutex> lock(mutex_);
    Settings settings(kProfileNamespace, false);
    cJSON* root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "nickname", settings.GetString("nickname").c_str());

    auto add_list = [root](const char* name, const std::string& serialized) {
        cJSON* array = cJSON_Parse(serialized.c_str());
        if (!cJSON_IsArray(array)) {
            cJSON_Delete(array);
            array = cJSON_CreateArray();
        }
        cJSON_AddItemToObject(root, name, array);
    };
    add_list("interests", settings.GetString("interests", "[]"));
    add_list("learned_topics", settings.GetString("topics", "[]"));
    cJSON_AddStringToObject(root, "encouragement", settings.GetString("encourage", "温和").c_str());

    char* text = cJSON_PrintUnformatted(root);
    std::string result = text == nullptr ? "{}" : text;
    cJSON_free(text);
    cJSON_Delete(root);
    return result;
}

bool ChildProfile::Remember(const std::string& category, const std::string& raw_value,
                            std::string& reason) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::string value = Normalize(raw_value);
    size_t max_bytes = category == "nickname" ? 24 : 60;
    if (!IsSafeValue(value, max_bytes, reason)) {
        return false;
    }

    if (category == "nickname") {
        Settings settings(kProfileNamespace, true);
        settings.SetString("nickname", value);
        settings.SetInt("schema", 1);
        return true;
    }
    if (category == "interest") {
        return StoreList("interests", value, 5, false, reason);
    }
    if (category == "learned_topic") {
        return StoreList("topics", value, 20, false, reason);
    }
    if (category == "encouragement") {
        if (value != "温和" && value != "简短" && value != "挑战式") {
            reason = "鼓励方式只能是：温和、简短或挑战式。";
            return false;
        }
        Settings settings(kProfileNamespace, true);
        settings.SetString("encourage", value);
        settings.SetInt("schema", 1);
        return true;
    }
    reason = "不支持的记忆类别。";
    return false;
}

bool ChildProfile::Forget(const std::string& category, const std::string& raw_value,
                          std::string& reason) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::string value = Normalize(raw_value);
    if (category == "all") {
        Settings settings(kProfileNamespace, true);
        settings.EraseAll();
        return true;
    }
    if (category == "nickname" || category == "encouragement") {
        Settings settings(kProfileNamespace, true);
        settings.EraseKey(category == "nickname" ? "nickname" : "encourage");
        return true;
    }
    if (category == "interest") {
        return StoreList("interests", value, 5, true, reason);
    }
    if (category == "learned_topic") {
        return StoreList("topics", value, 20, true, reason);
    }
    reason = "不支持的记忆类别。";
    return false;
}

void ChildProfile::ClearAll() {
    std::lock_guard<std::mutex> lock(mutex_);
    Settings settings(kProfileNamespace, true);
    settings.EraseAll();
}
