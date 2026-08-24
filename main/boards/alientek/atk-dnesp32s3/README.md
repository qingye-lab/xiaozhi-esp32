# ATK-DNESP32S3 camera variants

This board definition keeps the original OV2640 firmware and the OV5640
firmware as separate build variants. They share the Alientek board wiring but
use different sensor timing and OTA-reported names.

| Build name | Camera | XCLK | Sensor output |
| --- | --- | --- | --- |
| `atk-dnesp32s3` | OV2640 | 20 MHz | 240x240 YUV422 at 25 fps |
| `atk-dnesp32s3-ov5640` | OV5640 | 24 MHz | 800x600 UYVY at 10 fps |

Build the OV5640 firmware with ESP-IDF 6.0.2:

```bash
python scripts/build.py alientek/atk-dnesp32s3 \
  --name atk-dnesp32s3-ov5640
```

The OV5640 variant reports the unique firmware name
`atk-dnesp32s3-ov5640` while retaining the compatible board type
`atk-dnesp32s3`. A TF card is not required by XiaoZhi; settings and assets use
the ESP32-S3 module's internal flash partitions.
