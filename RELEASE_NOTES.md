# Patio Weather Display 1.0.26

This is the first public firmware release for the Waveshare ESP32-S3-Touch-LCD-7B.

## Highlights

- Native 1024 × 600 LVGL weather dashboard with touch settings, Wi-Fi onboarding, local time, hourly precipitation, air quality, alerts, and deterministic weather advice.
- First-run Seoul City Hall preset plus city, postal/ZIP, coordinate, and optional network-location setup.
- Offline holiday calendars for Canadian federal workplaces, all 10 provinces, all 3 territories, the United States, and South Korea for 2026–2036.
- Touch wake, configurable screen timeout, optional VL53L0X proximity wake, and real-time diagnostics.
- Canadian ECCC AQHI and official alerts where available; modelled US AQI elsewhere.

## Install

Use the board's **USB TO UART** port. The safest method is ESP-IDF 5.5.2 with the included `flash_args`. For a full service image, write `patio-weather-display-1.0.26-merged.bin` at address `0x0`. For an application-only update that preserves the existing partition table and settings, write `patio_weather_display-1.0.26.bin` at `0x30000` only when the installed layout matches this release.

Verify downloads using `SHA256SUMS.txt`. See the README for full flashing instructions and hardware limitations.

## Calendar notes

Holiday calendars are offline snapshots. Workplace, school, municipal, and observed-day rules can differ. Review the bundled coverage notes before relying on a date as a closure notice.

## Validation

Seven host suites pass, including calendar, persistence, provider, Wi-Fi policy, and native LVGL interaction tests. The ESP32-S3 target build and packaged image verification pass. The 1.0.26 binary was not flashed to the physical board during release QA because the USB serial device was disconnected.
