# Build and test report

## 1.0.31 public release — November 15, 2026

The live-provider ESP32-S3 application is 2,104,496 bytes, leaving 2,941,776 bytes (58%) free in each OTA slot. The ESP-IDF 5.5.2 release build passed the image checksum and SHA-256 validation checks. The compiler checked 316 project stack frames; the largest was 2,672 bytes against a 4,096-byte limit.

All seven host suites passed, including 41 core cases. The release simulator reproduced seven 1024 × 600 previews byte for byte: rain, sunny UV, a calm statutory holiday, snow with wind, a thunderstorm, [mixed hourly alerts](screenshots/dashboard/mixed-alerts.png), and the [overlapping alert details](screenshots/dashboard/mixed-alert-details.png). Simulated touches opened the correct forecast interval. Tests also cover block expiry, missing hours, quarter-hour offsets and disabling UV advice. This release was not flashed to a physical board.

## 1.0.30 public release — November 15, 2026

The live-provider application is 2,098,400 bytes. It has 2,947,872 bytes free (about 58%) in each OTA slot. A clean ESP-IDF 5.5.2 build reported a valid application checksum and SHA-256 validation hash. All seven host suites passed (38 core cases). GCC reported 311 project stack frames, a 2,672-byte maximum, and no violations of the 4,096-byte limit.

The release simulator reproduced the five [dashboard previews](screenshots/dashboard) byte for byte. Coverage includes the full statutory holiday line during a storm, one-hour wind labels, the transition back to a quiet timeline, mixed rain and snow, and retry/backoff behavior with cached data and provider rate limits.

The preview was flashed to a Waveshare 7B, booted, and returned a live dashboard over USB. All six loadable ESP32-S3 segment addresses and lengths match the individually built 1.0.30 release. The only payload bytes that differ are in the application descriptor's version, compilation timestamps, and ELF fingerprint. The final versioned image was not separately flashed. See the [release notes](RELEASE_NOTES.md) for download and installation options.

## 1.0.29 public release — November 15, 2026

The dedicated release build passed all seven host suites (33 core cases), and the 1024 × 600 rainy-day render matched the dashboard preview byte for byte. ESP-IDF 5.5.2 produced a 2,080,112-byte live-provider application with 59% of the smallest OTA slot free. Compiler stack reports covered 302 project frames: maximum 2,672 bytes, zero violations of the 4,096-byte limit. See the [1.0.29 release](https://github.com/fpenguin/patio-weather-display/releases/tag/v1.0.29) for its install files.

The prior 1.0.28 image booted on the board, restored cached data and reconnected to Wi-Fi. The panel later came back on after an intermittent blackout; USB serial was unavailable for that event, so its cause is unknown. For 1.0.29, host tests injected GT911 input without an interrupt, bus and acknowledgement failures, a lost release interrupt, and old settings containing 0% reading brightness. The 1.0.29 image has not been flashed to that board.

Release **1.0.26** adds all 10 Canadian provinces and all 3 territories to the offline holiday picker and automatic location mapping. All seven host suites passed, including 30 core cases. The ESP32-S3 target built with 59% of the OTA slot free; 286 project frames checked, maximum 2672 bytes, zero violations. The release package and merged image passed hash, offset, erased-gap, and OTA-fit verification. The board USB serial device was disconnected, so 1.0.26 was not physically flashed during release QA.

Release **1.0.25** adds [offline holiday calendars](docs/holidays.md) for the US and South Korea. All seven host suites passed, including 29 core cases. The ESP32-S3 target built; 282 project frames checked, maximum 2672 bytes, zero violations. Three native LVGL screenshots were rendered. USB was unavailable before upload, so hardware validation below applies to previous versions.

Release **1.0.24** adds the Seoul City Hall preset, international modelled US AQI, and explicit coverage messages. All seven host suites passed. See [international QA](docs/international-qa.md) for the 12-city results, screenshots and release limits.

## Previous 1.0.23 update

Release **1.0.23** adds a live diagnostics page and selectable proximity wake distances (30–200 cm). All seven host suites passed, including settings migration from schemas 2 and 3, threshold boundaries, debug transitions, and saving the 2 m choice. Physical ranging at 2 m has not been verified.

## Previous 1.0.22 update

Release **1.0.22** adds a bottom-banner warning button to retry data or scan for Wi-Fi. All seven host suites passed, including pointer tests for offline scan, stale data retry, repeat taps, and bulletin navigation.

## Previous 1.0.21 update

Release **1.0.21** opens the tapped dashboard bulletin at the top of Alert details. Back returns to the dashboard. All seven host suites passed, including checks for the visible bulletin, secondary alerts, and taps on the banner text, badge, and icon.

## Previous 1.0.20 update

Release **1.0.20** replaces the dashboard settings cog with three horizontal sliders. The touch target is unchanged. All seven host suites passed.

## Previous 1.0.19 update

Release **1.0.19** aligns the clock and temperature, stacks air quality above
apparent temperature, and gives the holiday its own measured row. The time
badge points at the timeline from below; overlapping tick captions use the
lower row. The elapsed-time background and dashboard fixture watermark are
removed. All seven host suites passed, and all 38 reference screens were rendered.

## Previous 1.0.18 update

Release **1.0.18** keeps advice subtitles to one measured line, with space for
the alert-count badge. A long official bulletin gets a short prompt; the alert
card retains its stored text. All seven host suites passed, including a native
LVGL test that measures the banner and opens the original multiline bulletin.
The USB screenshot command introduced in 1.0.17 is available for verification.

## Previous 1.0.16 update

Release **1.0.16** fixes conflicting idle outputs. On 1.0.15 the connected
board continued reporting 0% after the user observed the dashboard staying
lit. It had left inverted PWM at full drive and the overlay transparent.
Idle now requests zero LED drive (PWM 255), disables EXIO2 and paints an
opaque black layer; hardware shutoff does not wait for a repaint. Full drive
is primed only when waking. The native LVGL test verifies that a dashboard
refresh while idle stays black, two wakes restore the original luminance,
and the wake tap does not open Settings. All seven host suites passed.
In work/off-after.log the board submitted shade 255 after the 30-second
timeout and still requested 0% one minute after boot. The immediate EXIO2
input readback was high, so this does not prove electrical shutoff.

## Previous 1.0.15 update

Release **1.0.15** latches the GT911 interrupt while dark or dim. A dedicated
display task can enable the LEDs without waiting for a weather request, range
probe, coordinate scan or dashboard redraw. A timed dashboard that has woken
at reading brightness holds it until idle shutoff, including if a new approach
extends viewing. All seven host suites passed (26 core cases), including the
old dim-deadline regression. Maximum compiled project frame: 2672 B.

## Previous 1.0.14 update

Release **1.0.14** keeps the dashboard in the RGB buffers while the LEDs
are off. The transparent touch layer captures the first tap. Wake starts the
backlight at full drive before applying the saved reading brightness. The
firmware samples the three RGB buffers and reads the expander input pins
for diagnostics. These checks cannot measure light emitted by the panel.
All seven host suites passed, including repeated idle/wake rendering and
the test that a held wake tap over Settings does not open it. The connected
board booted with its saved 30-second timeout. The first recorded wake showed
640/640 non-black samples in each buffer, the backlight enable pin high and
an applied brightness of 85% (work/led-after.log). The user observed a third-tap wake and a temporary dim; the log also records a later scheduled change to 25%.

## Previous 1.0.13 update

Release **1.0.13** addresses a black panel after a registered wake tap. On
1.0.12, the attached board logged a change from 0% to 85% while the panel
remained visibly black. The controller now waits until LVGL submits the new
frame before writing the backlight. A fresh tap on the dashboard can retry
both steps even at the same brightness. Wake activity comes from validated
GT911 contacts. The host UI test exercises repeated idle/wake cycles, a
dashboard update while black and a wake tap held over Settings. All seven host
suites passed; maximum project stack frame: 2672 bytes (limit 4096).
The app-only flash passed hash verification. On the connected board with the
saved 30-second timeout, two idle/wake cycles submitted the awake frame and
then applied 85% brightness (work/blank-after.log). The user subsequently confirmed the first wake required several taps, and the second remained black.

## Previous 1.0.12 update

Release **1.0.12** shows the highest-priority active official alert in place of
the dry-weather fallback (including when convenience advice is disabled).
Only other active alerts count toward the badge. Advisories and statements
below the urgent threshold use the caution colour. All seven host suites
passed, including 25 core cases. Stack gate: 2672 bytes maximum (limit 4096).

## Previous 1.0.11 update

Release **1.0.11** restores the visible dashboard at normal reading brightness
after a wake tap. Scheduled dimming is rendered relative to the reading level,
idle draws black and gates the backlight, and wake enables the backlight before
applying its reading PWM. Hardware trace on 1.0.9 showed a wake to 25%; the
intermediate 1.0.10 requested 80% but the user still observed a dim panel. The
LVGL framebuffer test verifies that the idle frame is black and that wake
restores the original rendered luminance. All seven host suites passed,
including 24 core cases. The user confirmed black screen → bright dashboard
after one physical tap on 1.0.11. Device log: work/screenwake-boot.log.

## Previous 1.0.9 update

Release **1.0.9** applies the dashboard timeout even when no proximity sensor is
connected. On the attached board, the application-only update passed hash
verification. The boot log showed 1.0.9, no sensor, a 60-second timeout, and a
subsequent idle transition to 0% backlight. All seven host suites passed (23
core cases); native LVGL pointer input verified that a wake tap over the gear
is consumed through release and the next tap opens Settings. Firmware package
verification and the 4096-byte stack gate passed.

## Previous 1.0.8 update

Release **1.0.8** adds optional VL53L0X approach wake and a persistent screen
timeout. All seven host suites passed, including native LVGL timeout selection,
schema-2 settings migration, stationary/repeated approaches, missing-sensor
fallback and the coordinate lookup retry. Target stack gate: 2672 bytes maximum
(limit 4096). USB application-only flash verified; the board reported 1.0.8
and a missing sensor at boot. A physical walk-up test still requires a wired
module. Saved coordinates can now be previewed without HTTPS. A new coordinate
lookup waits for network readiness and can retry a transient failure once per
stage; the original first-tap failure was not captured on hardware.

## Previous 1.0.7 update

Release **1.0.7** makes coordinate defaults editable and preserves the entered
point through reverse lookup. All seven host suites passed, including default
coordinate submission, edits with zero and negative values, saved coordinates,
and a provider result whose map point differs from the query. The live services
returned Vancouver and America/Vancouver from the Mac. The app-only USB flash
verified its hash and the attached board reported version 1.0.7 at boot.

## Previous 1.0.6 update

Release **1.0.6** makes the Vancouver fallback actual editable text, accepts
country names or codes and distinguishes a missing city from an invalid country.
A native touch test submitted the untouched default form. The live geocoding
service returned Vancouver, BC for the province-qualified query with country CA.
All seven host suites, the package verification and the stack gate passed.
An app-only USB update verified its hash; the board reported 1.0.6 at boot.

## Previous 1.0.5 update

Release **1.0.5** adds South Korean air quality on the US AQI scale. The
attached board booted the app-only update with its saved configuration and
received US AQI 146 at 9.8 seconds. All seven host suites passed, including
provider validation and the fresh/stale US AQI badge. Package hashes and the
target stack limit passed.

## Previous 1.0.4 update

Release **1.0.4** saves the verified location preview to NVS without a second
forecast/time-zone request. Initial station discovery now uses the existing
background AQHI worker. Seven host suites passed, including storing an approved
preview and reloading it after a simulated restart without network services.
The package hash/offset checks passed and the attached board booted 1.0.4
after a hash-verified app-only update. Its serial log confirmed the location
was written to NVS 38 seconds after boot.

## Previous 1.0.3 update

Release **1.0.3** adds editable network-based city and country suggestions during
first-time setup. Seven host suites passed, including invalid provider responses,
country-code mapping, a late response during manual entry and a saved-location
guard. All 38 simulator renders passed, with a maximum project stack frame of
2,672 bytes. After an app-only USB update, the board booted 1.0.3 and reported
a successful live suggestion at 8.6 seconds. The firmware uses external PSRAM for
TLS allocations and keeps the full certificate bundle and hostname verification.

## Previous 1.0.2 update

Release **1.0.2** corrects the keyboard placement and enables RGB stream recovery
at vertical blank. All seven host suites passed, including coordinate taps that
select a secured network, type a password, reopen the keyboard, preserve the
entry across status/scan updates, and submit it. The target built with the same
stack limit; the app-only USB flash verified its hash and the attached board
reported version 1.0.2 at boot. See `firmware/release-manifest.json` for the
current artifacts. Hardware confirmation of sustained panel visibility is separate
from these checks.

## Historical 1.0.1 audit build

Release **1.0.1**, verified 2026-11-14 on macOS / Apple Silicon after the Claude
audit corrections. No physical display board was attached.

## Results

- ESP-IDF 5.5.2 target build: **PASS**. Fresh build followed by incremental
  rebuilds after the final source changes; production fixture mode is off.
- Target compiler warnings: **0**; Kconfig warnings: **0**. One CMake developer
  policy notice (CMP0225, IDF's bootloader ExternalProject under the host's newer
  CMake) remains. It is not a compiler or configuration-symbol warning.
- Host CTest: **7/7 PASS** (core, persistence, clock, cache, Wi-Fi policy,
  providers and native LVGL UI). Core suite: **20/20 cases**.
- GCC stack gate: **PASS**. 210 project frame records, maximum 2,672 bytes;
  none above 4,096 bytes and no unbounded dynamic project frame.
- Native LVGL rendering: **38/38 PASS**, each 1024 × 600, using the same
  bounded **128 KB LVGL pool** as the target. Includes 27 home fixtures and
  11 screens, including crowded Wi-Fi, manual SSID and ambiguous location.
- Package verification: **PASS**. Six SHA-256 hashes, matching manifests,
  embedded app/ELF identity, four flash segments, erased gaps and OTA fit.
- Physical boot, touch, radio, flash power cuts and sustained operation:
  **NOT RUN**. See `AUDIT_FIXES.md` and the README device checklist.

## Pinned target environment

```text
ESP-IDF v5.5.2 / 30aaf64524299d3bde422ca9a2848090d1bc5d0f
Python 3.11.18
xtensa-esp-elf 14.2.0 (esp-14.2.0_20251107)
Target esp32s3
LVGL 8.4.0 / esp_lvgl_adapter 0.5.2 / GT911 1.2.1
```

The existing local ESP-IDF 5.5.2 toolchain was reused.
Build output for this checkout is under `work/target-build/`.

Commands, with the pinned IDF environment activated and run from this project:

```sh
idf.py -B work/target-build build size
python3 scripts/check_stack_usage.py work/target-build
cmake -S host -B work/host-build -G Ninja \
  -DPATIO_CJSON_SOURCE_DIR="$IDF_PATH/components/json/cJSON" \
  -DPATIO_BUILD_SIMULATOR=ON \
  -DPATIO_LVGL_SOURCE_DIR="$PWD/managed_components/lvgl__lvgl"
cmake --build work/host-build --parallel 8
ctest --test-dir work/host-build --output-on-failure
python3 scripts/verify_firmware.py
```

Logs: `work/target-final.log`, `work/host-tests.log`, `work/stack-usage.log`,
`work/screenshot-verification.log`, `work/package-verification.log`.
The Linux-specific integer-template mismatch was corrected with explicit
`int64_t` operations. A Linux build was not run in this environment.

## Memory and stack evidence

| Measure | Final value |
|---|---:|
| Application BIN | 1,937,184 B (`0x1d8f20`) |
| OTA slot | 5,046,272 B (`0x4d0000`) |
| OTA free | 3,109,088 B (IDF reports 62%) |
| Bootloader | 22,528 B (`0x5800`) |
| Flash code | 1,046,300 B |
| Flash data | 764,216 B |
| Static DIRAM used | 148,459 / 341,760 B (43.44%) |
| Static DIRAM remaining | 193,301 B |

The former 131,072-byte `work_mem_int` symbol is absent from the final map.
The LVGL pool and 12 KB LVGL task stack now allocate in PSRAM; the UI queue
payload also lives in PSRAM. The adapter's 102,400-byte partial draw buffer
remains internal. Static DIRAM usage fell from the audited 279,567 bytes by
131,108 bytes. Static size is not a measurement of runtime free heap.

Selected GCC-measured project frames:

| Function | Frame |
|---|---:|
| `app_main` | 240 B |
| `controller_task` | 240 B |
| `location_task` | 48 B |
| `handle_location_search` | 864 B |
| `weather_task` | 1,184 B |
| `aqhi_task` | 1,408 B |
| `alerts_task` | 768 B |
| `persistence_save` | 1,584 B |
| `cache_store_load` / `cache_store_save` | 64 / 112 B |

The largest compiled project frame (2,672 B) is fixture construction. Per-frame
measurements do not sum nested/vendor/TLS/interrupt use. The runtime now logs
boot and worker/controller/LVGL stack high-water marks plus internal free heap
and largest blocks; these still need to be observed on the physical board.

## Packaged firmware

```text
--flash_mode dout --flash_freq 80m --flash_size 16MB
0x0     bootloader/bootloader.bin
0x8000  partition_table/partition-table.bin
0x11000 ota_data_initial.bin
0x30000 patio_weather_display.bin
```

The merged image was made from the final build's generated `flash_args` using
`esptool --chip esp32s3 merge_bin`. It is **2,133,792 bytes** and is intended
for offset zero. The standalone application remains an offset-`0x30000` image.
`esptool image_info` reports a valid checksum and validation hash:

```text
fdc41a2cbc6446f0668d54f0443bd6e557d58dea093c13a4fae6fad2401d9306
```

Authoritative artifact hashes are in `firmware/SHA256SUMS.txt` and
`firmware/release-manifest.json`. The verifier also checks the app descriptor
against the packaged ELF and version 1.0.1. The pre-audit firmware bundle was
retained under `work/original-firmware/` for comparison; it is not the release
to flash.

Host doubles exercise production recovery decisions without simulating flash
electronics, RF or real SNTP. The corrected release is ready for the documented
hardware validation, not represented as hardware-tested.
