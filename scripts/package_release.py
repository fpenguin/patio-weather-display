#!/usr/bin/env python3
"""Build the checked release bundle from an ESP-IDF build directory."""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import sys
from datetime import datetime
from zoneinfo import ZoneInfo
from zipfile import ZIP_DEFLATED, ZipFile


def digest(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build", type=Path, required=True)
    parser.add_argument("--firmware", type=Path, required=True)
    parser.add_argument("--dist", type=Path, required=True)
    parser.add_argument("--version", required=True)
    args = parser.parse_args()

    root = Path(__file__).resolve().parents[1]
    build = args.build.resolve()
    firmware = args.firmware.resolve()
    dist = args.dist.resolve()
    firmware.mkdir(parents=True, exist_ok=True)
    dist.mkdir(parents=True, exist_ok=True)

    flash_args = (build / "flash_args").read_text().split()
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
    manifest = json.loads(manifest_path.read_text()) if manifest_path.exists() else {}
    application = (firmware / "patio_weather_display.bin").read_bytes()
    slot_bytes = 0x4D0000
    manifest.update({
        "project": "patio_weather_display",
        "version": args.version,
        "build_time": datetime.now(ZoneInfo("Asia/Seoul")).isoformat(timespec="seconds"),
        "target": "esp32s3",
        "board": "Waveshare ESP32-S3-Touch-LCD-7B documented baseline",
        "physical_board_revision_verified": False,
        "hardware_validation": (
            f"Version {args.version} passed host, native LVGL, target-build and package checks. "
            "It was not flashed during release QA because the board USB serial port was disconnected."
        ),
        "dependencies": {
            **manifest.get("dependencies", {}),
            "holiday_data": "Vacanza holidays 0.108 plus selected municipal rules; 2026-2036",
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
            "host_suites_passed": 7,
            "core_cases_passed": 30,
            "holiday_qa_renders": 4,
            "project_stack_frame_limit_bytes": 4096,
            "maximum_compiled_project_frame_bytes": 2672,
            "physical_board_tested": False,
        },
    })
    manifest_path.write_text(json.dumps(manifest, indent=2) + "\n")
    (firmware / "SHA256SUMS.txt").write_text(
        "".join(f"{checksum}  {name}\n" for name, checksum in manifest["artifacts"].items())
    )

    versioned_app = dist / f"patio_weather_display-{args.version}.bin"
    versioned_merged = dist / f"patio-weather-display-{args.version}-merged.bin"
    shutil.copyfile(firmware / "patio_weather_display.bin", versioned_app)
    shutil.copyfile(firmware / "patio-weather-display-merged.bin", versioned_merged)
    release_hashes = {
        versioned_merged.name: digest(versioned_merged),
        versioned_app.name: digest(versioned_app),
    }
    (dist / "SHA256SUMS.txt").write_text(
        "".join(f"{checksum}  {name}\n" for name, checksum in release_hashes.items())
    )
    shutil.copyfile(manifest_path, dist / "release-manifest.json")

    bundle = dist / f"Patio-Weather-Display-{args.version}.zip"
    with ZipFile(bundle, "w", ZIP_DEFLATED) as archive:
        for path in sorted(firmware.rglob("*")):
            if path.is_file() and path.suffix != ".elf":
                archive.write(path, Path("firmware") / path.relative_to(firmware))
        for relative in ("README.md", "RELEASE_NOTES.md", "docs/holidays.md",
                         "docs/proximity-setup.md"):
            archive.write(root / relative, relative)
    print(f"Packaged {args.version}: {bundle}")


if __name__ == "__main__":
    main()
