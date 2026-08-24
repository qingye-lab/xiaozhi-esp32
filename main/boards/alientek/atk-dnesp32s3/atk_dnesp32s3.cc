#include "application.h"
#include "button.h"
#include "codecs/es8388_audio_codec.h"
#include "config.h"
#include "display/lcd_display.h"
#include "esp_video.h"
#include "i2c_device.h"
#include "kid_companion.h"
#include "led/single_led.h"
#include "mcp_server.h"
#include "settings.h"
#include "wifi_board.h"

#include <driver/i2c_master.h>
#include <driver/spi_common.h>
#include <esp_lcd_panel_vendor.h>
#include <esp_log.h>
#include <esp_timer.h>

#include <algorithm>
#include <atomic>
#include <mutex>

#define TAG "atk_dnesp32s3"

class XL9555 : public I2cDevice {
public:
    XL9555(i2c_master_bus_handle_t i2c_bus, uint8_t addr) : I2cDevice(i2c_bus, addr) {
        WriteReg(0x06, 0x03);
        WriteReg(0x07, 0xF0);
    }

    void SetOutputState(uint8_t bit, uint8_t level) {
        std::lock_guard<std::mutex> lock(mutex_);
        uint16_t data;
        int index = bit;

        if (bit < 8) {
            data = ReadReg(0x02);
        } else {
            data = ReadReg(0x03);
            index -= 8;
        }

        data = (data & ~(1 << index)) | (level << index);

        if (bit < 8) {
            WriteReg(0x02, data);
        } else {
            WriteReg(0x03, data);
        }
    }

    bool ReadInputState(uint16_t& state) {
        std::lock_guard<std::mutex> lock(mutex_);
        state = static_cast<uint16_t>(ReadReg(0x00)) | (static_cast<uint16_t>(ReadReg(0x01)) << 8);
        return true;
    }

private:
    std::mutex mutex_;
};

class ChildSafeEs8388AudioCodec : public Es8388AudioCodec {
public:
    using Es8388AudioCodec::Es8388AudioCodec;

    void Start() override {
        Settings settings("audio", false);
        int stored_volume = settings.GetInt("output_volume", 45);
        output_volume_ = std::clamp(stored_volume, 0, 70);
        if (stored_volume != output_volume_) {
            Settings writable_settings("audio", true);
            writable_settings.SetInt("output_volume", output_volume_);
        }
        ESP_LOGI(TAG, "Child-safe audio codec started at %d%%", output_volume_);
    }

    void SetOutputVolume(int volume) override {
        Es8388AudioCodec::SetOutputVolume(std::clamp(volume, 0, 70));
    }
};

class atk_dnesp32s3 : public WifiBoard {
private:
    i2c_master_bus_handle_t i2c_bus_;
    Button boot_button_;
    LcdDisplay* display_;
    XL9555* xl9555_;
    EspVideo* camera_impl_;
    ChildSafeCamera* camera_;
    KidSensorHub* sensor_hub_;
    ChildProfile child_profile_;
    std::atomic<int64_t> profile_clear_deadline_ms_{0};

    void InitializeI2c() {
        // Initialize I2C peripheral
        i2c_master_bus_config_t i2c_bus_cfg = {
            .i2c_port = (i2c_port_t)I2C_NUM_0,
            .sda_io_num = AUDIO_CODEC_I2C_SDA_PIN,
            .scl_io_num = AUDIO_CODEC_I2C_SCL_PIN,
            .clk_source = I2C_CLK_SRC_DEFAULT,
            .glitch_ignore_cnt = 7,
            .intr_priority = 0,
            .trans_queue_depth = 0,
            .flags =
                {
                .enable_internal_pullup = 1,
            },
        };
        ESP_ERROR_CHECK(i2c_new_master_bus(&i2c_bus_cfg, &i2c_bus_));

        // Initialize XL9555
        xl9555_ = new XL9555(i2c_bus_, 0x20);
    }

    // Initialize spi peripheral
    void InitializeSpi() {
        spi_bus_config_t buscfg = {};
        buscfg.mosi_io_num = LCD_MOSI_PIN;
        buscfg.miso_io_num = GPIO_NUM_NC;
        buscfg.sclk_io_num = LCD_SCLK_PIN;
        buscfg.quadwp_io_num = GPIO_NUM_NC;
        buscfg.quadhd_io_num = GPIO_NUM_NC;
        buscfg.max_transfer_sz = DISPLAY_WIDTH * DISPLAY_HEIGHT * sizeof(uint16_t);
        ESP_ERROR_CHECK(spi_bus_initialize(SPI2_HOST, &buscfg, SPI_DMA_CH_AUTO));
    }

    void InitializeButtons() {
        boot_button_.OnClick([this]() {
            auto& app = Application::GetInstance();
            if (app.GetDeviceState() == kDeviceStateStarting) {
                EnterWifiConfigMode();
                return;
            }
            app.ToggleChatState();
        });
    }

    void HandleKidKey(KidKey key, kid_companion::ButtonEvent event) {
        auto& app = Application::GetInstance();
        auto codec = GetAudioCodec();
        auto display = GetDisplay();
        int64_t now_ms = esp_timer_get_time() / 1000;

        switch (key) {
            case KidKey::kKey0: {
                int volume = event == kid_companion::ButtonEvent::kLongPress
                                 ? 70
                                 : codec->output_volume() + 10;
                codec->SetOutputVolume(volume);
                display->ShowNotification("音量 " + std::to_string(codec->output_volume()) + "%");
                break;
            }
            case KidKey::kKey1: {
                int volume = event == kid_companion::ButtonEvent::kLongPress
                                 ? 0
                                 : codec->output_volume() - 10;
                codec->SetOutputVolume(volume);
                display->ShowNotification(
                    volume == 0 ? "已静音"
                                : "音量 " + std::to_string(codec->output_volume()) + "%");
                break;
            }
            case KidKey::kKey2:
                if (profile_clear_deadline_ms_.load() >= now_ms) {
                    child_profile_.ClearAll();
                    profile_clear_deadline_ms_.store(0);
                    camera_->Cancel();
                    display->ShowNotification("成长记忆已清除");
                } else {
                    profile_clear_deadline_ms_.store(0);
                    camera_->Arm();
                    display->ShowNotification("相机已确认，请说“拍吧”");
                    app.StartListening();
                }
                break;
            case KidKey::kKey3:
                camera_->Cancel();
                if (event == kid_companion::ButtonEvent::kLongPress) {
                    profile_clear_deadline_ms_.store(now_ms + 10'000);
                    display->ShowNotification("清除成长记忆？10 秒内按 KEY2 确认");
                } else {
                    profile_clear_deadline_ms_.store(0);
                    if (app.GetDeviceState() == kDeviceStateSpeaking) {
                        app.AbortSpeaking(kAbortReasonNone);
                    } else {
                        app.StopListening();
                    }
                    display->ShowNotification("已取消");
                }
                break;
        }
    }

    void InitializeKidCompanion() {
        sensor_hub_ = new KidSensorHub(
            i2c_bus_, [this](uint16_t& inputs) { return xl9555_->ReadInputState(inputs); },
            [this](KidKey key, kid_companion::ButtonEvent event) {
                Application::GetInstance().Schedule(
                    [this, key, event]() { HandleKidKey(key, event); });
            });
        camera_ = new ChildSafeCamera(camera_impl_, sensor_hub_);
    }

    void InitializeTools() {
        auto& mcp_server = McpServer::GetInstance();
        mcp_server.AddTool(
            "self.sensors.get_environment",
            "读取设备周围的粗粒度环境状态。用户询问光线、设备是否放稳或是否有手靠近时使用。"
            "只返回 dark/normal/bright、near 和 stable/moving，不用于跟踪儿童。",
            PropertyList(), [this](const PropertyList&) -> ReturnValue {
                return sensor_hub_->GetEnvironmentJson();
            });

        mcp_server.AddTool(
            "self.child_profile.get",
            "读取" KID_COMPANION_NAME "在本机保存的有限成长记忆。开始个性化鼓励前使用。",
            PropertyList(),
            [this](const PropertyList&) -> ReturnValue { return child_profile_.GetJson(); });

        mcp_server.AddTool(
            "self.child_profile.remember",
            "仅保存孩子明确表达的非敏感偏好。category 只能是 nickname、interest、"
            "learned_topic 或 encouragement；禁止保存学校、地址、联系方式、秘密、账号或照片。",
            PropertyList({Property("category", kPropertyTypeString),
                          Property("value", kPropertyTypeString)}),
            [this](const PropertyList& properties) -> ReturnValue {
                std::string reason;
                if (!child_profile_.Remember(properties["category"].value<std::string>(),
                                             properties["value"].value<std::string>(), reason)) {
                    throw std::runtime_error(reason);
                }
                return child_profile_.GetJson();
            });

        mcp_server.AddTool(
            "self.child_profile.forget",
            "按孩子或家长的明确语音要求删除本机成长记忆。category 可为 nickname、interest、"
            "learned_topic、encouragement 或 all；value 留空时清除该类别。",
            PropertyList({Property("category", kPropertyTypeString),
                          Property("value", kPropertyTypeString, std::string(""))}),
            [this](const PropertyList& properties) -> ReturnValue {
                std::string reason;
                if (!child_profile_.Forget(properties["category"].value<std::string>(),
                                           properties["value"].value<std::string>(), reason)) {
                    throw std::runtime_error(reason);
                }
                return child_profile_.GetJson();
            });

        mcp_server.AddTool(
            "self.system.reconfigure_wifi",
            "进入无需互联网的本地 Wi-Fi 配网模式。调用前必须向用户确认；只有用户明确同意后"
            "才把 confirmed 设为 true。进入后请说明连接屏幕显示的 Xiaozhi 热点，并在浏览器"
            "打开屏幕显示的本地地址。",
            PropertyList({Property("confirmed", kPropertyTypeBoolean)}),
            [this](const PropertyList& properties) -> ReturnValue {
                if (!properties["confirmed"].value<bool>()) {
                    throw std::runtime_error("用户尚未确认进入本地配网模式");
                }
                EnterWifiConfigMode();
                return true;
            });
    }

    void InitializeSt7789Display() {
        esp_lcd_panel_io_handle_t panel_io = nullptr;
        esp_lcd_panel_handle_t panel = nullptr;
        ESP_LOGD(TAG, "Install panel IO");
        // 液晶屏控制IO初始化
        esp_lcd_panel_io_spi_config_t io_config = {};
        io_config.cs_gpio_num = LCD_CS_PIN;
        io_config.dc_gpio_num = LCD_DC_PIN;
        io_config.spi_mode = 0;
        io_config.pclk_hz = 20 * 1000 * 1000;
        io_config.trans_queue_depth = 7;
        io_config.lcd_cmd_bits = 8;
        io_config.lcd_param_bits = 8;
        esp_lcd_new_panel_io_spi(SPI2_HOST, &io_config, &panel_io);

        // 初始化液晶屏驱动芯片ST7789
        ESP_LOGD(TAG, "Install LCD driver");
        esp_lcd_panel_dev_config_t panel_config = {};
        panel_config.reset_gpio_num = GPIO_NUM_NC;
        panel_config.rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB;
        panel_config.bits_per_pixel = 16;
        panel_config.data_endian = LCD_RGB_DATA_ENDIAN_BIG,
        esp_lcd_new_panel_st7789(panel_io, &panel_config, &panel);
        
        esp_lcd_panel_reset(panel);
        xl9555_->SetOutputState(8, 1);
        xl9555_->SetOutputState(2, 0);

        esp_lcd_panel_init(panel);
        esp_lcd_panel_invert_color(panel, DISPLAY_BACKLIGHT_OUTPUT_INVERT);
        esp_lcd_panel_swap_xy(panel, DISPLAY_SWAP_XY); 
        esp_lcd_panel_mirror(panel, DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y);
        display_ = new SpiLcdDisplay(panel_io, panel,
                                    DISPLAY_WIDTH, DISPLAY_HEIGHT, DISPLAY_OFFSET_X, DISPLAY_OFFSET_Y, DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y, DISPLAY_SWAP_XY);
    }

    // Initialize the selected DVP camera using Alientek's board wiring.
    void InitializeCamera() {
        xl9555_->SetOutputState(OV_PWDN_IO, 0); // PWDN=低 (上电)
        xl9555_->SetOutputState(OV_RESET_IO, 0); // 确保复位
        vTaskDelay(pdMS_TO_TICKS(50));           // 延长复位保持时间
        xl9555_->SetOutputState(OV_RESET_IO, 1); // 释放复位
        vTaskDelay(pdMS_TO_TICKS(50));           // 延长 50ms

        static esp_cam_ctlr_dvp_pin_config_t dvp_pin_config = {
            .data_width = CAM_CTLR_DATA_WIDTH_8,
            .data_io =
                {
                [0] = CAM_PIN_D0,
                [1] = CAM_PIN_D1,
                [2] = CAM_PIN_D2,
                [3] = CAM_PIN_D3,
                [4] = CAM_PIN_D4,
                [5] = CAM_PIN_D5,
                [6] = CAM_PIN_D6,
                [7] = CAM_PIN_D7,
            },
            .vsync_io = CAM_PIN_VSYNC,
            .de_io = CAM_PIN_HREF,
            .pclk_io = CAM_PIN_PCLK,
            .xclk_io = CAM_PIN_XCLK,
        };

        esp_video_init_sccb_config_t sccb_config = {
            .init_sccb = true,
            .i2c_config =
                {
                .port = 1,
                .scl_pin = CAM_PIN_SIOC,
                .sda_pin = CAM_PIN_SIOD,
            },
            .freq = 100000,
        };

        esp_video_init_dvp_config_t dvp_config = {
            .sccb_config = sccb_config,
            .reset_pin = CAM_PIN_RESET,   // 实际由 XL9555 控制
            .pwdn_pin = CAM_PIN_PWDN,     // 实际由 XL9555 控制
            .dvp_pin = dvp_pin_config,
            .xclk_freq = CAMERA_XCLK_FREQ_HZ,
        };

        esp_video_init_config_t video_config = {
            .dvp = &dvp_config,
        };

        camera_impl_ = new EspVideo(video_config);
    }

public:
    atk_dnesp32s3() : boot_button_(BOOT_BUTTON_GPIO) {
        InitializeI2c();
        InitializeSpi();
        InitializeSt7789Display();
        InitializeButtons();
        InitializeCamera();
        InitializeKidCompanion();
        InitializeTools();
        sensor_hub_->Start();
    }

    virtual Led* GetLed() override {
        static SingleLed led(BUILTIN_LED_GPIO);
        return &led;
    }

    virtual AudioCodec* GetAudioCodec() override {
        static ChildSafeEs8388AudioCodec audio_codec(
            i2c_bus_, 
            I2C_NUM_0, 
            AUDIO_INPUT_SAMPLE_RATE, 
            AUDIO_OUTPUT_SAMPLE_RATE,
            AUDIO_I2S_GPIO_MCLK, 
            AUDIO_I2S_GPIO_BCLK, 
            AUDIO_I2S_GPIO_WS, 
            AUDIO_I2S_GPIO_DOUT, 
            AUDIO_I2S_GPIO_DIN,
            GPIO_NUM_NC, 
            AUDIO_CODEC_ES8388_ADDR
        );
        return &audio_codec;
    }

    virtual Display* GetDisplay() override {
        return display_;
    }
    
    virtual Camera* GetCamera() override {
        return camera_;
    }
};

DECLARE_BOARD(atk_dnesp32s3);
