# Claude audit corrections — 2026-11-14

Reviewed the supplied audit against this checkout and corrected the confirmed
findings. Firmware version 1.0.1, screenshots and package manifests were rebuilt
from the corrected source. Hardware-only conclusions remain pending because no
7B display was attached.

| Audit finding | Correction and evidence |
|---|---|
| P1-1: task stack overflow | Large snapshots, reset temporaries, URL buffers, location request and controller command moved off task stacks into PSRAM. Parsers allocate checked transactional scratch. In-place reconstruction preserves defaults. Global `-fstack-usage`, release gate and runtime high-water logging added. Final `app_main` frame is 240 B; no project frame exceeds 4 KB. |
| P1-2: inaccessible Wi-Fi networks | All 20 retained unique scan entries are reachable through a scrollable list; strongest duplicate retained and empty SSIDs dropped. Manual/hidden SSID entry includes security. Native LVGL events select the 20th entry, enter credentials and submit a hidden network. |
| P1-3: permanent reconnect failure | Six fast retry delays are followed by indefinite three-minute retries, respecting intentional disconnect. Retry policy is host-tested. Correcting credentials cancels an unfinished attempt before IDF `set_config`; actual radio recovery still needs hardware. |
| P1-4: incorrect umbrella times | Uses the next contiguous rain window, stopping at dry/missing gaps. Both endpoints carry AM/PM; noon and midnight have explicit labels. Tests compare exact wording for morning, crossing noon, midnight and separate windows. Uses `to` because the bundled font lacks an en dash. |
| P2-1: false dry advice during rain | In-progress hourly rain is considered before proration; only the remaining total is prorated. Tests cover 4:50 PM in a 1 mm rainy hour and its end boundary. |
| P2-2: rain label clipping | Draws all bars first, then measured/staggered labels with collision checks. Extreme heights are clamped before coordinate conversion. Baseline, all-day rain and large-value layouts are checked by native UI tests. |
| P2-3: internal RAM budget | 128 KB LVGL pool, 12 KB LVGL task stack and command queue payload now live in PSRAM. Static DIRAM drops to 148,459 B; old internal pool symbol is absent. Pre-registration largest-block logs added. All 38 renders and UI interactions pass with a 128 KB simulator pool. Actual runtime heap margin remains a device measurement. |
| P2-4: missing regression evidence | Expanded to seven suites: 20 core cases, provider parsing, native UI events, Wi-Fi policy, production persistence against NVS doubles, cache fault recovery, and production SNTP logic against a callback double. Tests cover midnight/time-zone change and resync. Synthetic fixture provenance is explicit. Full end-to-end device location/reboot/clock flows remain on the hardware checklist. |
| P3-1: Linux integer mismatch | Explicit `std::int64_t` min/max types remove the `long`/`long long` deduction conflict. Host build/tests pass on macOS; no Linux toolchain run is claimed. |
| P3-2: negative zero | Rounded integer temperature formatting produces `0°C` and `Feels like 0°`; native label assertions cover −0.4°C. |
| P3-3: credentials/authentication | Removed the unsupported encryption claim. Selected security reaches IDF's minimum auth threshold; a secured selection cannot fall back to open. Existing saved passwords with an open minimum are migrated to a secured minimum. Open networks are explicit; WEP/enterprise are rejected by this flow. |
| P3-4: distant AQHI | Nearest selection has a 50 km default cap, configurable at the provider API/source-policy level. This is a project policy, not an official coverage guarantee. Community labels disclose distance; rejection detail is visible. Missing or legacy mappings are automatically re-resolved/retried, without requiring location setup again. |
| P3-5: holiday preview | Preview derives the effective calendar that save will use, including federal Canada and independently retained choices. Calendar changes invalidate an older location preview. |
| P3-6: blank first-format screen | Initializes display and presents a loading message before mounting/formatting SPIFFS. Actual first-format latency still needs the device. |
| P3-7: cache power-loss gap | Replaced unlink/rename with CRC-checked A/B generations; previous valid snapshot remains while inactive slot is written. Recovers complete legacy `.bin`/`.tmp` files. Tests truncate writes at seven positions, corrupt headers/payloads, verify backwards-clock handling and preserve output when all copies are invalid. |

The review also corrected an extreme-positive AQHI rounding overflow and
retained the existing generation guards that discard results from an obsolete
location. No weather values or dates from the visual references were made live
defaults.

## Verification and remaining device work

`BUILD_REPORT.md` records the final binary sizes, stack measurements, commands
and package verification. `firmware/` contains the corrected release; the old
bundle is retained only under `work/original-firmware/`.

Before relying on the display, use the README's hardware checklist: observe
boot/high-water and largest-block logs, test real Wi-Fi loss and password
correction, touch/keyboard alignment, first-format duration, warm/cold restarts,
physical power-cut recovery, SNTP, live provider responses and a 24-hour run
crossing local midnight. None of those physical checks is claimed by host tests.
