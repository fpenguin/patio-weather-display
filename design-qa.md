# Dashboard — native render QA

Compared the approved rainy-day mockup with the ESP32-S3 firmware’s LVGL 8 renderer at 1024 × 600, RGB565. The reference and simulator use sample readings. The production build has fixture mode disabled.

[Rainy dashboard](screenshots/dashboard/rain.png) · [Snow, thunderstorm, high UV and calm weather](screenshots/dashboard/conditions.png) · [Mixed alerts](screenshots/dashboard/mixed-alerts.png) · [Overlapping alert details](screenshots/dashboard/mixed-alert-details.png)

### 1.0.31 hourly alert blocks

The native mixed-day example shows yellow UV caution (10 AM–1 PM), orange high UV (1–3 PM), then a red thunderstorm overlapping heavy rain and strong wind (4–6 PM). A simulated touch on **+2 more** opens the rain and wind measurements for those same hours. On the snow example the single-hour snowflake and **Heavy** fit above the longer **Strong** wind block. The calm holiday example has no alert strip.

The host tests check severity changes, coincident hazards, the UV preference and touch targets. They also check that refreshes keep the start of an ongoing window, expired blocks disappear, missing hours aren't bridged, and quarter-hour offsets stay aligned. The ESP32-S3 image was built and checked; these example screens were verified in the simulator.

| Check | Finding and correction |
| --- | --- |
| Header | The first render unnecessarily stepped the clock down to the smaller digit font. Measuring the suffix and tightening digit spacing restored the large clock and common baseline. The divider, daily temperatures and AQI/AQHI badge occupy separate space. |
| Timeline | Compact amount labels fit adjacent hours. A label close to the current minute lifts just while the marker crosses it; time captions always stay in place. Solar stems stop before text and bars. In a dense chart the labels can use multiple rows, including when both rain and snow fall in the same hour. |
| Wind and warnings | A one-hour storm segment initially had color but insufficient space for its uppercase label. It now reads “Storm”. Ribbon transitions and solar events use their true data times; they need not match the mockup’s illustrative x positions. The support line includes AM/PM where the range crosses noon. |
| Banner and states | Confirmed one subtitle line, the long-holiday row, below-zero temperatures, missing values, stale data, active alerts and the pre-6-AM state. Ordinary dry days show clothing guidance; urgent hazards and data recovery remain actionable. |

All seven host suites passed, including 33 core cases. Native pointer tests cover the cog, wake tap, direct bulletin navigation and retry control. Chart tests include minute updates, midnight rollover, extreme values and 36 labels across 18 mixed rain/snow hours. The production parser accepted a live Seoul forecast. The ESP-IDF 5.5.2 ESP32-S3 image passed checksum, validation-hash, partition-size and compiler stack-frame checks.

On November 15, 2026, the ESP32-S3 was updated from 1.0.24 to 1.0.28. The USB boot log confirmed version 1.0.28, restored cached data, and Wi-Fi reconnection. Touch wake initialized with the saved 30-second timeout. For release 1.0.29 the rainy-day screenshot was rendered again and matched the previous PNG byte for byte. The new wake logic was exercised on the host; physical verification of this image remains open.

### 1.0.30 dashboard and connection retries

Compared the 1024 × 600 native frames side by side with the approved examples: [sun with high UV](screenshots/dashboard/sunny-uv.png), [overcast BC holiday](screenshots/dashboard/cloudy-holiday.png), [rain](screenshots/dashboard/rain.png), [snow and wind](screenshots/dashboard/snow-wind.png), and [thunderstorm](screenshots/dashboard/storm.png).

- The sunny and cloudy cases reclaim the space below the rail. The UV 7 band runs exactly from noon to 3 PM and the advice is orange. On the cloudy holiday the calendar line fits “National Day for Truth and Reconciliation” and “in BC, Canada” without wrapping or sharing space with air quality. The jacket recommendation stays neutral.
- The rain and snow charts retain hourly quantities. A strong-wind segment from 2–5 PM uses dark lettering on orange and disappears at 5 PM, even before the next forecast fetch. On the board, adjacent one-hour wind segments initially showed identical pictograms; tighter text spacing now distinguishes “Strong” from “Breezy” in 49 pixels. A red storm segment remains visible with light winds. Missing wind coverage is not labeled as calm.
- Native tests exercise the yellow/orange/red thresholds, official alert colors, adjacent rain and snow labels, single-line advice, and time changes. Retry tests cover the 30/60/120/300-second progression, a normal 15-minute poll after success, reconnect/manual requests, numeric and dated provider waits, and the three-hour cache-age boundary. These are controlled failure tests, not a claim of a live provider outage.
- All seven host suites passed, including 38 core cases. The live-mode ESP32-S3 image passed checksum, validation-hash and partition checks, with 58% of the smallest app slot free. The compiler checked 311 project stack frames with no violations.
- On the connected Waveshare 7B, the partition table matched the build. Only the active application at `0x30000` was flashed; the transfer hash verified, and the boot log reported `1.0.29-dashboard-dev`. Settings and the source cache were outside the written range. A USB framebuffer capture showed a live rain forecast, distinct adjacent “Strong” and “Breezy” segments, and a yellow umbrella advisory. A startup capture also exposed a clock-sync caption crossing the rail; its position now follows the rail, and a native test checks both the caption and the nearby stale badge for overlap.

Final simulator and firmware build result: passed.
