# Third-party notices

This project contains or derives from the following third-party work. Dependency source packages installed by ESP-IDF Component Manager retain their own upstream license files.

## Waveshare ESP32-S3-Touch-LCD-7B example

Board initialization in `components/board_waveshare_7b` is derived from Waveshare's `examples/ESP-IDF/13_lvgl_v8_demo` at commit `c652c902db607f7ffb376257393cfd7657aa6428`.

Copyright Waveshare. Licensed under the Apache License, Version 2.0. A copy is in `third_party/licenses/Apache-2.0.txt`.

## LVGL

LVGL 8.4.0 is resolved by the ESP-IDF Component Manager and is licensed under the MIT License. Its upstream license is included in the downloaded managed component.

## Espressif components and ESP-IDF

ESP-IDF 5.5.2, `esp_lvgl_adapter` 0.5.2, and `esp_lcd_touch_gt911` 1.2.1 are resolved from their upstream sources. Consult the license files shipped with each source package. ESP-IDF's bundled cJSON is MIT-licensed.

## Montserrat

The two compiled display-font subsets are derived from Montserrat Black. Montserrat is licensed under the SIL Open Font License 1.1. A copy is in `third_party/licenses/Montserrat-OFL.txt`.

## IANA time-zone database

Generated transition data is derived from IANA tzdata 2026f. The upstream attribution/public-domain notice is in `third_party/licenses/IANA-tzdata.txt`.

## Data services

- Open-Meteo weather and geocoding APIs: subject to Open-Meteo's current terms and attribution requirements.
- Nominatim/OpenStreetMap: © OpenStreetMap contributors; database data is available under ODbL and the public endpoint has a separate usage policy.
- Environment and Climate Change Canada AQHI and alert data: subject to the Government of Canada open-data licence and source attribution requirements.

This notice does not replace the providers' current terms. Recheck them before deployment.

The VL53L0X initialization and calibration code in
`components/board_waveshare_7b/vl53l0x` is adapted from Pololu 1.3.1
(commit 9f3773cb48d4e4e844d689cfc529a06f96d1d264). The ESP-IDF port
replaces the Arduino I²C and timing interfaces. Pololu and ST notices are
retained in the adjacent LICENSE.txt.

## Vacanza holidays

The bundled US and South Korean holiday dates were generated from Vacanza holidays 0.108 (MIT). See [the copyright and license](third_party/python-holidays/LICENSE) and [calendar sources](docs/holidays.md).
