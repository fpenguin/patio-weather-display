#!/usr/bin/env python3
"""Build the checked release bundle from an ESP-IDF build directory."""

from __future__ import annotations

import argparse
import hashlib
import json
import platform
import re
import shlex
from pathlib import Path
import shutil
import subprocess
import sys
from datetime import datetime
from zoneinfo import ZoneInfo
from zipfile import ZIP_DEFLATED, ZipFile
from xml.etree import ElementTree

import yaml  # Provided by the ESP-IDF environment.


def digest(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build", type=Path, required=True)
    parser.add_argument("--firmware", type=Path, required=True)
    parser.add_argument("--dist", type=Path, required=True)
    parser.add_argument("--version", required=True)
    parser.add_argument("--host-build", type=Path, required=True,
                        help="Host build with Testing/Temporary/LastTest.log")
    parser.add_argument("--ctest-report", type=Path, required=True,
                        help="JUnit XML written by the release CTest run")
    parser.add_argument("--stack-report", type=Path, required=True,
                        help="Output of check_stack_usage.py on the target build")
    parser.add_argument("--hardware-note", default="This release image was not flashed during packaging.")
    args = parser.parse_args()

    root = Path(__file__).resolve().parents[1]
    build = args.build.resolve()
    firmware = args.firmware.resolve()
    dist = args.dist.resolve()
    firmware.mkdir(parents=True, exist_ok=True)
    dist.mkdir(parents=True, exist_ok=True)

    project = json.loads((build / "project_description.json").read_text())
    if (project["project_name"], project["target"], project["project_version"]) != (
            "patio_weather_display", "esp32s3", args.version):
        parser.error("build project, chip or version does not match this release")
    if json.loads((build / "config/sdkconfig.json").read_text()).get("PATIO_FIXTURE_MODE"):
        parser.error("release builds must use live providers")
    suites = ElementTree.parse(args.ctest_report).getroot()
    expected_suites = {"weather_core", "persistence", "clock", "cache", "wifi_policy", "providers", "ui"}
    cases = suites.findall("testcase")
    if ({c.get("name") for c in cases} != expected_suites or len(cases) != len(expected_suites)
        or any(c.get("status") != "run" or any(c.find(x) is not None for x in ("failure","error","skipped"))
               for c in cases)
        or any(int(suites.get(k, 0)) for k in ("failures","errors","disabled","skipped"))):
        parser.error("all seven production host suites must pass")
    core = re.search(r"^(\d+)/(\d+) tests passed$", (args.host_build / "Testing/Temporary/LastTest.log").read_text(), re.M)
    stack = re.search(r"^(\d+) project frames; maximum (\d+) B; limit (\d+) B; 0 violations$",
                      args.stack_report.read_text(), re.M)
    if not core or core[1] != core[2] or not stack:
        parser.error("missing passing core or stack check")

    flash_args = shlex.split((build / "flash_args").read_text())
    merged = build / "patio-weather-display-merged.bin"
    subprocess.run(
        [sys.executable, "-m", "esptool", "--chip", "esp32s3", "merge_bin",
         "-o", str(merged), *flash_args], cwd=build, check=True)

    copied = {
        "bootloader/bootloader.bin": build / "bootloader/bootloader.bin",
        "ota_data_initial.bin": build / "ota_data_initial.bin",
        "partition_table/partition-table.bin": build / "partition_table/partition-table.bin",
        "patio-weather-display-merged.bin": merged,
        "patio_weather_display.bin": build / "patio_weather_display.bin",
    }
    for name, source in copied.items():
        target = firmware / name
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(source, target)
    for name in ("flash_args", "flasher_args.json", "patio_weather_display.elf"):
        shutil.copyfile(build / name, firmware / name)

    manifest_path = firmware / "release-manifest.json"
    application = (firmware / "patio_weather_display.bin").read_bytes()
    if application[48:80].split(b"\0", 1)[0].decode() != args.version:
        parser.error("application descriptor does not match release version")
    flash = json.loads((build / "flasher_args.json").read_text())
    settings = flash["flash_settings"]
    ota_sizes = [int(row.split(",")[4].strip(), 0) for row in (root / "partitions.csv").read_text().splitlines()
                 if row.lstrip().startswith("ota_")]
    slot_bytes = min(ota_sizes)
    idf = Path(project["idf_path"])
    deps = yaml.safe_load((root / "dependencies.lock").read_text())["dependencies"]
    manifest = {
        "project": "patio_weather_display",
        "version": args.version,
        "build_time": datetime.now(ZoneInfo("Asia/Seoul")).isoformat(timespec="seconds"),
        "target": "esp32s3",
        "board": "Waveshare ESP32-S3-Touch-LCD-7B documented baseline",
        "physical_board_revision_verified": False,
        "hardware_validation": args.hardware_note,
        "toolchain": {
            "esp_idf": project["git_revision"],
            "esp_idf_commit": subprocess.check_output(["git", "-C", idf, "rev-parse", "HEAD"], text=True).strip(),
            "python": platform.python_version(),
            "xtensa_esp_elf": subprocess.check_output([project["c_compiler"], "--version"], text=True).splitlines()[0],
        },
        "dependencies": {
            "lvgl": deps["lvgl/lvgl"]["version"],
            "esp_lvgl_adapter": deps["espressif/esp_lvgl_adapter"]["version"],
            "esp_lcd_touch_gt911": deps["espressif/esp_lcd_touch_gt911"]["version"],
            "waveshare_reference_commit": "c652c902db607f7ffb376257393cfd7657aa6428",
            "iana_tzdata": "2026f",
            "pololu_vl53l0x": "1.3.1, 9f3773cb48d4e4e844d689cfc529a06f96d1d264 (ESP-IDF transport)",
            "holiday_data": "Vacanza holidays 0.108 plus selected municipal rules; 2026-2036",
        },
        "flash": {
            "size": settings["flash_size"], "mode": settings["flash_mode"], "frequency": settings["flash_freq"],
            "offsets": {file: offset for offset, file in flash["flash_files"].items()},
            "merged_image_offset": "0x0",
        },
        "application": {
            "bytes": len(application),
            "ota_slot_bytes": slot_bytes,
            "ota_free_bytes": slot_bytes - len(application),
            "ota_free_percent_reported": 100 * (slot_bytes - len(application)) // slot_bytes,
            "embedded_validation_sha256": application[-32:].hex(),
            "debug_elf_sha256": digest(firmware / "patio_weather_display.elf"),
        },
        "artifacts": {name: digest(firmware / name) for name in copied},
        "verification": {
            "host_suites_passed": len(cases), "core_cases_passed": int(core[1]),
            "project_stack_frames_checked": int(stack[1]),
            "project_stack_frame_limit_bytes": int(stack[3]),
            "maximum_compiled_project_frame_bytes": int(stack[2]),
            "physical_board_tested": False,
        },
    }
    manifest_path.write_text(json.dumps(manifest, indent=2) + "\n")
    (firmware / "SHA256SUMS.txt").write_text(
        "".join(f"{checksum}  {name}\n" for name, checksum in manifest["artifacts"].items())
    )

    versioned_app = dist / f"patio_weather_display-{args.version}.bin"
    versioned_merged = dist / f"patio-weather-display-{args.version}-merged.bin"
    shutil.copyfile(firmware / "patio_weather_display.bin", versioned_app)
    shutil.copyfile(firmware / "patio-weather-display-merged.bin", versioned_merged)
    release_manifest = dist / "release-manifest.json"
    shutil.copyfile(manifest_path, release_manifest)

    bundle = dist / f"Patio-Weather-Display-{args.version}.zip"
    with ZipFile(bundle, "w", ZIP_DEFLATED) as archive:
        for name in sorted([*copied, "flash_args", "flasher_args.json", "release-manifest.json", "SHA256SUMS.txt"]):
            archive.write(firmware / name, Path("firmware") / name)
        for relative in ("README.md", "RELEASE_NOTES.md", "THIRD_PARTY_NOTICES.md", "docs/holidays.md",
                         "docs/proximity-setup.md", "third_party/python-holidays/LICENSE",
                         "components/board_waveshare_7b/vl53l0x/LICENSE.txt"):
            archive.write(root / relative, relative)
        for license_file in sorted((root / "third_party/licenses").glob("*.txt")):
            archive.write(license_file, license_file.relative_to(root))
        archive.write(Path(project["build_component_info"]["lvgl__lvgl"]["dir"]) / "LICENCE.txt",
                      "third_party/licenses/LVGL-MIT.txt")
        archive.write(idf / "components/json/cJSON/LICENSE", "third_party/licenses/cJSON-MIT.txt")
        for preview in sorted((root / "screenshots/dashboard").glob("*.png")):
            archive.write(preview, preview.relative_to(root))
    (dist / "SHA256SUMS.txt").write_text("".join(
        f"{digest(path)}  {path.name}\n" for path in (bundle, versioned_merged, versioned_app, release_manifest)))
    print(f"Packaged {args.version}: {bundle}")


if __name__ == "__main__":
    main()
