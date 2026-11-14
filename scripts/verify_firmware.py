#!/usr/bin/env python3
"""Verify the packaged hashes, generated offsets and merged image contents."""
import hashlib
import json
from pathlib import Path
import shlex
import sys

directory = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(__file__).resolve().parent.parent / "firmware"
manifest = json.loads((directory / "release-manifest.json").read_text())
hashes = {}
for line in (directory / "SHA256SUMS.txt").read_text().splitlines():
    checksum, name = line.split(maxsplit=1)
    hashes[name.lstrip("*")] = checksum
assert hashes == manifest["artifacts"], "manifest and SHA256SUMS disagree"
for name, expected in hashes.items():
    assert hashlib.sha256((directory / name).read_bytes()).hexdigest() == expected, name

args = shlex.split((directory / "flash_args").read_text())
offsets = {args[i + 1]: int(arg, 16) for i, arg in enumerate(args) if arg.startswith("0x")}
assert offsets == {name: int(offset, 16) for name, offset in manifest["flash"]["offsets"].items()}
generated = json.loads((directory / "flasher_args.json").read_text())
assert offsets == {name: int(offset, 16) for offset, name in generated["flash_files"].items()}
for option, field in [("--flash_mode", "mode"), ("--flash_freq", "frequency"), ("--flash_size", "size")]:
    assert args[args.index(option) + 1] == manifest["flash"][field]

merged = (directory / "patio-weather-display-merged.bin").read_bytes()
end = 0
for name, offset in sorted(offsets.items(), key=lambda entry: entry[1]):
    data = (directory / name).read_bytes()
    assert offset >= end, "overlapping images"
    assert all(value == 0xff for value in merged[end:offset]), "non-erased gap"
    assert merged[offset:offset + len(data)] == data, name
    end = offset + len(data)
assert len(merged) == end, "unexpected trailing merged bytes"
app = (directory / "patio_weather_display.bin").read_bytes()
assert len(app) == manifest["application"]["bytes"]
# ESP-IDF 5.5 esp_app_desc_t is the first DROM segment payload at byte 32.
assert int.from_bytes(app[32:36], "little") == 0xabcd5432
assert app[48:80].split(b"\0", 1)[0].decode() == manifest["version"]
# Public packages omit the local debug ELF (DWARF contains builder paths).
elf_hash = manifest["application"]["debug_elf_sha256"]
assert app[176:208].hex() == elf_hash, "app/ELF identity mismatch"
elf = directory / "patio_weather_display.elf"
if elf.exists():
    assert hashlib.sha256(elf.read_bytes()).hexdigest() == elf_hash, "local debug ELF mismatch"
assert len(app) < manifest["application"]["ota_slot_bytes"]
assert manifest["application"]["ota_free_bytes"] == manifest["application"]["ota_slot_bytes"] - len(app)
print(f"PASS: {len(hashes)} SHA-256 hashes, manifests, {len(offsets)} flash segments, erased gaps and OTA fit")
