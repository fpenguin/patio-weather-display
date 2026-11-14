# Dashboard — native render QA

Compared the approved rainy-day mockup with the ESP32-S3 firmware’s LVGL 8 renderer at 1024 × 600, RGB565. The reference and simulator use sample readings. The production build has fixture mode disabled.

[Rainy dashboard](screenshots/dashboard/rain.png) · [Snow, thunderstorm, high UV and calm weather](screenshots/dashboard/conditions.png)

| Check | Finding and correction |
| --- | --- |
| Header | The first render unnecessarily stepped the clock down to the smaller digit font. Measuring the suffix and tightening digit spacing restored the large clock and common baseline. The divider, daily temperatures and AQI/AQHI badge occupy separate space. |
| Timeline | Compact amount labels fit adjacent hours. A label close to the current minute lifts just while the marker crosses it; time captions always stay in place. Solar stems stop before text and bars. In a dense chart the labels can use multiple rows, including when both rain and snow fall in the same hour. |
| Wind and warnings | A one-hour storm segment initially had color but insufficient space for its uppercase label. It now reads “Storm”. Ribbon transitions and solar events use their true data times; they need not match the mockup’s illustrative x positions. The support line includes AM/PM where the range crosses noon. |
| Banner and states | Confirmed one subtitle line, the long-holiday row, below-zero temperatures, missing values, stale data, active alerts and the pre-6-AM state. Ordinary dry days show clothing guidance; urgent hazards and data recovery remain actionable. |

All seven host suites passed, including 33 core cases. Native pointer tests cover the cog, wake tap, direct bulletin navigation and retry control. Chart tests include minute updates, midnight rollover, extreme values and 36 labels across 18 mixed rain/snow hours. The production parser accepted a live Seoul forecast. The ESP-IDF 5.5.2 ESP32-S3 image passed checksum, validation-hash, partition-size and compiler stack-frame checks.

On November 15, 2026, the ESP32-S3 was updated from 1.0.24 to 1.0.28. The USB boot log confirmed version 1.0.28, restored cached data, and Wi-Fi reconnection. Touch wake initialized with the saved 30-second timeout. For release 1.0.29 the rainy-day screenshot was rendered again and matched the previous PNG byte for byte. The new wake logic was exercised on the host; physical verification of this image remains open.

final result: passed
