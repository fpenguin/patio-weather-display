# Verification record

Original baseline verification: 2026-11-13. Audit corrections and rebuilt
release: 2026-11-14. Host: macOS, Apple Silicon. No physical display board was
connected. The current results are in `BUILD_REPORT.md` and `AUDIT_FIXES.md`;
the vendor baseline and endpoint notes below describe the original work.

## Official board baseline

The official repository was checked out at detached commit:

```text
https://github.com/waveshareteam/ESP32-S3-Touch-LCD-7B
c652c902db607f7ffb376257393cfd7657aa6428
examples/ESP-IDF/13_lvgl_v8_demo
```

The tracked tree was unmodified. With ESP-IDF v5.5.2:

```sh
idf.py set-target esp32s3
idf.py build
```

Result: 1583/1583 build steps completed. The vendor `lvgl_porting.bin` was `0x9fca0` bytes and had SHA-256 `0e9342d83a51deccce6f2d2e27886a31fc63306f2818866983f149e91420be78`.

The vendor manifest uses `^0.5.2`, which resolved to `esp_lvgl_adapter` 0.5.3 on the verification date. This project deliberately pins 0.5.2 and its board component separately compiled and linked cleanly against LVGL 8.4.0, adapter 0.5.2, GT911 1.2.1, and ESP-IDF v5.5.2.

The target's physical PCB revision remains unverified. Neither the official documentation nor schematic exposes a reliable user-facing PCB revision string, so component-file `V1.0` comments were not treated as a board revision.

## Live endpoint/schema checks

The following HTTPS endpoints were checked during the original implementation.
The JSON files under `fixtures/` are curated deterministic parser samples, not
preserved live responses. In particular the ECCC alert fixture is synthetic.
See `fixtures/README.md` for provenance. These fixture tests do not establish
current endpoint availability or current schema compatibility.

### Open-Meteo

Forecast endpoint:

```text
https://api.open-meteo.com/v1/forecast
```

Verified requested fields:

- `current`: `temperature_2m`, `apparent_temperature`, `weather_code`, `wind_speed_10m`, `wind_gusts_10m`
- `hourly`: `rain`, `showers`, `snowfall`, `precipitation_probability`, `weather_code`, `temperature_2m`, `apparent_temperature`, `wind_speed_10m`, `wind_gusts_10m`, `uv_index`
- `timezone=auto`, `timeformat=unixtime`, `past_days=1`, `forecast_days=2`
- Celsius, km/h, millimetres

Geocoding endpoint:

```text
https://geocoding-api.open-meteo.com/v1/search
```

City lookup returned multiple Vancouver-like candidates and named IANA zones. Open-Meteo was not treated as a reverse-geocoder. A zero-day forecast metadata request was separately verified for coordinate-to-zone resolution.

### Nominatim

Verified on explicit user actions only:

```text
https://nominatim.openstreetmap.org/search
https://nominatim.openstreetmap.org/reverse
```

The implementation requires an identifying User-Agent, performs no
autocomplete/background polling, bounds results, caches the last successful
exact query in RAM for the current boot, and enforces a one-request-per-second
minimum interval for this public endpoint.

### Environment and Climate Change Canada GeoMet

Base endpoint:

```text
https://api.weather.gc.ca
```

Verified collections:

- `aqhi-stations`
- `aqhi-observations-realtime`
- `aqhi-forecasts-realtime`
- `weather-alerts`

The Vancouver-area station query returned multiple records; the parser computed great-circle distance across all returned records and explicitly selected the nearest retained community rather than trusting response order. The observation parser requires that selected community ID and chooses the newest applicable observation.

The forecast collection was checked for schema/provider verification; this
release's runtime display uses the current observation collection and does not
present an AQHI forecast as a current reading.

Alert lookup was verified with a CQL point-intersection filter. The parser preserves collection/feature IDs, alert code/type/name/text, risk colour, impact, confidence, status, publication/validity/end/expiry timestamps, and deduplicates updated records transactionally.

## Time zones and calendars

The generated zone table uses IANA tzdata 2026f with upstream archive SHA-256:

```text
01b4a91cec5167ed48c11e8ab78f2048cead78ce94b0cafcad0ae8c5a3044d3d
```

Tests verify:

- `America/Vancouver` is UTC-07 on 2026-11-12 with no former November rollback.
- `America/Los_Angeles` has the expected 2026 spring gap, autumn fold, and 23/25-hour local days.
- `Asia/Kathmandu` converts with a +05:45 offset.

BC statutory dates were checked against the current BC Employment Standards source. A distinct Canada Labour Code federal calendar is implemented so federal Boxing Day does not make Boxing Day a BC statutory day off.

## Host verification

The core and provider suites are built from the same production sources used by the ESP32 target. The exact rerunnable commands are in `README.md`. A native LVGL 8.4 headless simulator renders the production UI at 1024 x 600; it is not a browser mockup.

See `BUILD_REPORT.md` for the final project build, binary hashes, CTest result, simulator render result, and the checks explicitly not run on hardware.
