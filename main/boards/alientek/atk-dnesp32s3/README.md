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

## Xiaoya child companion

This branch replaces the shared emoji set on this board with the original
176x176 Xiaoya character. The 21 protocol emotion names are board-local GIFs,
plus a `robot_2` alias used only while the application starts. They are
generated from the transparent masters under `artwork/xiaoya` by:

```bash
python scripts/build_xiaoya_assets.py \
  main/boards/alientek/atk-dnesp32s3/artwork/xiaoya \
  main/boards/alientek/atk-dnesp32s3/assets/xiaoya
```

`KID_COMPANION_NAME` in `config.h` is the single firmware identity setting.
The runtime collection is limited to 2.5MB. The complete generated
`assets.bin`, which also contains fonts and the wake-word model, must remain
below 80% of the 8MB assets partition.

The v1 hardware scope is deliberately small:

- AP3216C exposes only `dark`, `normal`, `bright`, and a coarse near-hand bit.
- QMA6100P exposes only `stable` or `moving`, and blocks a blurred photo while
  preserving an unexpired authorization.
- XL9555 keys provide volume, camera confirmation, cancel, and local memory
  clearing. ES8388 starts at 45% on a new device and is capped at 70%.
- TF, infrared, buzzer, and absent temperature/humidity hardware are not used.
  Failure to find AP3216C or QMA6100P disables only that enhancement.

Pin assignments, sensor addresses, and the expansion-board boundary follow
[Alientek's official ATK-DNESP32S3 material](https://github.com/openedv/ATK-DNESP32S3-Board).

See [the complete usage, privacy, and console prompt guide](../../../../docs/atk-kid-companion.md).
