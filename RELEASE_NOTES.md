# Patio Weather Display 1.0.30

The dashboard now leaves out light wind, gives statutory holidays their own line, and uses yellow, orange, and red to show how much attention a weather warning deserves. This release targets the Waveshare **ESP32-S3-Touch-LCD-7B**.

## Changes since v1.0.29

- **Wind:** When the remaining hours are light, the timeline expands into the wind row. Breezy, strong, severe, storm, and hail intervals still appear at their forecast times. A single hour is wide enough to read “Strong” or “Breezy.” Missing wind data is marked separately.
- **Holidays:** An applicable statutory holiday and its jurisdiction have a full-width line below the clock and temperature, even when a warning is active.
- **Weather advice:** Yellow means caution, orange means warning, and red means danger. Ordinary clothing advice stays neutral. The umbrella message gives the expected amount and time window, for example “4mm of rain expected 11am-5pm. Strong wind 2-5pm.” Snow and storm advice also include timing when available. Official alerts keep the issuing authority’s color when supplied.
- **Forecast retries:** After a temporary connection or server failure, weather retries after 30 seconds, 1 minute, 2 minutes, then every 5 minutes. A successful update restores the normal 15-minute interval. The device honors a longer provider-requested wait. A recent cached forecast reads **Update delayed**; by default **Stale forecast** means the data is more than three hours old.

[Sunny day with a UV warning](https://github.com/fpenguin/patio-weather-display/blob/v1.0.30/screenshots/dashboard/sunny-uv.png) · [Cloudy statutory holiday](https://github.com/fpenguin/patio-weather-display/blob/v1.0.30/screenshots/dashboard/cloudy-holiday.png) · [Rain](https://github.com/fpenguin/patio-weather-display/blob/v1.0.30/screenshots/dashboard/rain.png) · [Snow and wind](https://github.com/fpenguin/patio-weather-display/blob/v1.0.30/screenshots/dashboard/snow-wind.png) · [Thunderstorm](https://github.com/fpenguin/patio-weather-display/blob/v1.0.30/screenshots/dashboard/storm.png)

## Which file to download

| Use | Download |
| --- | --- |
| Update an existing installation and keep settings | [Application image](https://github.com/fpenguin/patio-weather-display/releases/download/v1.0.30/patio_weather_display-1.0.30.bin). With this project’s partition table and default active slot `ota_0`, write the app at **`0x30000`**. If you changed slots, confirm the active app partition first. |
| Set up a new board or restore the full layout | [Merged image](https://github.com/fpenguin/patio-weather-display/releases/download/v1.0.30/patio-weather-display-1.0.30-merged.bin) at **`0x0`**. This overwrites saved settings, including Wi-Fi credentials. |
| Get the flash segments, instructions, screenshots, and notices together | [Firmware ZIP](https://github.com/fpenguin/patio-weather-display/releases/download/v1.0.30/Patio-Weather-Display-1.0.30.zip). Connect via the board’s **USB TO UART** port. |

[SHA-256 checksums](https://github.com/fpenguin/patio-weather-display/releases/download/v1.0.30/SHA256SUMS.txt) · [Flash manifest](https://github.com/fpenguin/patio-weather-display/releases/download/v1.0.30/release-manifest.json)

## Verification

All seven host suites passed, including 38 core cases. Five 1024 × 600 screenshots matched the release simulator exactly. The ESP-IDF 5.5.2 image passed checksum, flash-layout, and OTA-size checks. The preview ran on a physical 7B; the release image’s loadable segments match that preview except for its version, build timestamps, and ELF fingerprint. The separately numbered release was not flashed during packaging.
