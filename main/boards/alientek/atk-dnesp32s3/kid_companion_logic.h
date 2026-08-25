#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <string>

namespace kid_companion {

inline constexpr int kDefaultOutputVolume = 45;
inline constexpr int kMaximumOutputVolume = 70;
inline constexpr int kCriticalI2cMaxAttempts = 3;
inline constexpr int kCriticalI2cRetryDelayMs = 50;

inline int ClampOutputVolume(int volume) { return std::clamp(volume, 0, kMaximumOutputVolume); }

enum class LightLevel { kDark, kNormal, kBright };

inline const char* LightLevelName(LightLevel level) {
    switch (level) {
        case LightLevel::kDark:
            return "dark";
        case LightLevel::kBright:
            return "bright";
        case LightLevel::kNormal:
        default:
            return "normal";
    }
}

inline const char* EnvironmentLightName(bool available, LightLevel level) {
    return available ? LightLevelName(level) : "unknown";
}

class LightClassifier {
public:
    LightLevel Update(uint16_t ambient) {
        switch (level_) {
            case LightLevel::kDark:
                if (ambient > 160) {
                    level_ = LightLevel::kNormal;
                }
                break;
            case LightLevel::kBright:
                if (ambient < 1800) {
                    level_ = LightLevel::kNormal;
                }
                break;
            case LightLevel::kNormal:
                if (ambient < 80) {
                    level_ = LightLevel::kDark;
                } else if (ambient > 2500) {
                    level_ = LightLevel::kBright;
                }
                break;
        }
        return level_;
    }

    LightLevel level() const { return level_; }

private:
    LightLevel level_ = LightLevel::kNormal;
};

class ProximityClassifier {
public:
    bool Update(uint16_t proximity) {
        if (near_) {
            if (proximity < 200) {
                near_ = false;
            }
        } else if (proximity > 300) {
            near_ = true;
        }
        return near_;
    }

    bool near() const { return near_; }

private:
    bool near_ = false;
};

class MotionClassifier {
public:
    bool Update(int16_t x, int16_t y, int16_t z) {
        if (!initialized_) {
            previous_ = {x, y, z};
            initialized_ = true;
            return false;
        }

        int32_t delta = std::abs(static_cast<int32_t>(x) - previous_[0]) +
                        std::abs(static_cast<int32_t>(y) - previous_[1]) +
                        std::abs(static_cast<int32_t>(z) - previous_[2]);
        previous_ = {x, y, z};
        if (delta > 120) {
            stable_samples_ = 0;
            stable_ = false;
        } else {
            stable_samples_ = std::min<uint8_t>(stable_samples_ + 1, 10);
            stable_ = stable_samples_ >= 10;
        }
        return stable_;
    }

    bool stable() const { return stable_; }

private:
    std::array<int16_t, 3> previous_ = {};
    uint8_t stable_samples_ = 0;
    bool initialized_ = false;
    bool stable_ = false;
};

class CameraConsentState {
public:
    static constexpr int64_t kRequestWindowMs = 60'000;
    static constexpr int64_t kAuthorizationWindowMs = 20'000;

    void Request(int64_t now_ms) {
        request_until_ms_ = now_ms + kRequestWindowMs;
        armed_until_ms_ = 0;
    }

    bool Arm(int64_t now_ms, std::string& reason) {
        if (!IsRequestPending(now_ms)) {
            request_until_ms_ = 0;
            reason = "请先用语音告诉小芽想拍什么。";
            return false;
        }
        request_until_ms_ = 0;
        armed_until_ms_ = now_ms + kAuthorizationWindowMs;
        reason.clear();
        return true;
    }

    void Cancel() {
        request_until_ms_ = 0;
        armed_until_ms_ = 0;
    }

    void Consume() { armed_until_ms_ = 0; }

    bool IsRequestPending(int64_t now_ms) const {
        return request_until_ms_ > 0 && now_ms <= request_until_ms_;
    }

    bool IsArmed(int64_t now_ms) const { return armed_until_ms_ > 0 && now_ms <= armed_until_ms_; }

    bool Prepare(int64_t now_ms, bool stability_available, bool stable, std::string& reason) {
        if (!IsArmed(now_ms)) {
            Cancel();
            reason = "请先告诉孩子按一下相机确认键 KEY2，听到提示后再说‘拍吧’。";
            return false;
        }
        if (stability_available && !stable) {
            reason = "设备还在移动，请放稳后再次说‘拍吧’，无需重复按键。";
            return false;
        }
        reason.clear();
        return true;
    }

private:
    int64_t request_until_ms_ = 0;
    int64_t armed_until_ms_ = 0;
};

enum class ButtonEvent { kNone, kClick, kLongPress };

class ButtonDebouncer {
public:
    ButtonEvent Update(bool raw_pressed, int64_t now_ms) {
        if (raw_pressed != raw_state_) {
            raw_state_ = raw_pressed;
            raw_changed_ms_ = now_ms;
        }
        if (raw_state_ != stable_state_ && now_ms - raw_changed_ms_ >= kDebounceMs) {
            stable_state_ = raw_state_;
            if (stable_state_) {
                pressed_ms_ = now_ms;
                long_fired_ = false;
            } else if (!long_fired_) {
                return ButtonEvent::kClick;
            }
        }
        if (stable_state_ && !long_fired_ && now_ms - pressed_ms_ >= kLongPressMs) {
            long_fired_ = true;
            return ButtonEvent::kLongPress;
        }
        return ButtonEvent::kNone;
    }

private:
    static constexpr int64_t kDebounceMs = 30;
    static constexpr int64_t kLongPressMs = 1500;
    bool raw_state_ = false;
    bool stable_state_ = false;
    bool long_fired_ = false;
    int64_t raw_changed_ms_ = 0;
    int64_t pressed_ms_ = 0;
};

}  // namespace kid_companion
