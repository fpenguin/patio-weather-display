# Patio Weather Display 1.0.29

The dashboard now answers two questions before you head out: **what should I wear, and when will the weather change?** This is a stable release for the Waveshare **ESP32-S3-Touch-LCD-7B**.

## Changes since v1.0.26

- The temperature block shows the outdoor reading, feels-like temperature, daily high/low and regional air quality. A cog opens Settings.
- One daylight timeline shows sunrise, sunset, hourly rain in millimetres and snow in centimetres. A cyan triangle marks the current minute. Dry high-UV periods appear above the rail; wind changes and forecast storm or hail risk appear below it.
- The bottom banner combines clothing advice with relevant times. On a calm day it can suggest a light layer. Official alert details and the red Wi-Fi/data retry control remain available.
- **Settings → Display & units → Orientation** adds 180° landscape mounting and rotates touch with the screen. Existing installations keep their normal orientation unless changed.
- A missed touch interrupt now has a polling fallback that acknowledges only ready GT911 reports. A temporary I²C error is retried without an indefinite wait. An old 0% reading brightness is restored to 80%; scheduled dimming can still be set to 0%. The first wake tap is consumed, and **Settings → Debug mode → System** shows uptime and the last restart reason.

[View the rainy-day dashboard](https://github.com/fpenguin/patio-weather-display/blob/v1.0.29/screenshots/dashboard/rain.png) · [Snow, storms, UV and calm weather](https://github.com/fpenguin/patio-weather-display/blob/v1.0.29/screenshots/dashboard/conditions.png)

## Which file to download

| Use | Download |
| --- | --- |
| Update an existing installation and keep settings | [Application image](https://github.com/fpenguin/patio-weather-display/releases/download/v1.0.29/patio_weather_display-1.0.29.bin). With this project's partition table and its default active slot `ota_0`, write the app at **`0x30000`**. If slot selection has been changed, confirm the active application partition first. |
| Set up a new board or restore the full flash layout | [Merged image](https://github.com/fpenguin/patio-weather-display/releases/download/v1.0.29/patio-weather-display-1.0.29-merged.bin) at **`0x0`**. This overwrites stored settings, including Wi-Fi credentials. |
| Get the individual flash segments, manifest, instructions and notices | [Firmware ZIP](https://github.com/fpenguin/patio-weather-display/releases/download/v1.0.29/Patio-Weather-Display-1.0.29.zip). Use the board's **USB TO UART** port. |

[SHA-256 checksums](https://github.com/fpenguin/patio-weather-display/releases/download/v1.0.29/SHA256SUMS.txt) · [Flash manifest](https://github.com/fpenguin/patio-weather-display/releases/download/v1.0.29/release-manifest.json)

## Verification

The release build passed all seven host suites, including 33 core tests, native LVGL interaction tests, and cache/settings migration tests. Regression tests cover a tap with no interrupt, failed reads and acknowledgements, and recovery of an old zero-brightness setting. The ESP32-S3 image was built with ESP-IDF 5.5.2; the application checksum, flash offsets, package hashes and OTA slot fit were checked.

Hardware boot was verified on 1.0.28: the board reconnected to Wi-Fi and initialized touch wake with a saved 30-second timeout. The 1.0.29 wake changes passed the host tests; they have not been flashed or verified on a physical panel. If investigating an intermittent black screen, check the [wake and power troubleshooting guide](https://github.com/fpenguin/patio-weather-display/blob/v1.0.29/docs/proximity-setup.md#distinguishing-a-timeout-from-a-restart).
