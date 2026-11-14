"""Capture the connected display as a PNG. Requires pyserial; no Pillow needed."""
import argparse
import datetime
import pathlib
import struct
import time
import zlib

import serial


def capture(port):
    with serial.Serial(port=None, baudrate=115200, timeout=0.5) as link:
        link.dtr = link.rts = False
        link.port = port
        link.open()
        link.reset_input_buffer()
        link.write(b"\nWEATHER-SCREENSHOT\n")
        deadline = time.monotonic() + 85
        rows = []
        expected_crc = None
        while time.monotonic() < deadline:
            line = link.readline().strip()
            if line.startswith(b"@SHOT ERROR"):
                raise RuntimeError(line.decode())
            if line.startswith(b"@SHOT BEGIN"):
                _, _, w, h, crc = line.split()
                if (int(w), int(h)) != (1024, 600):
                    raise ValueError("unexpected panel dimensions")
                expected_crc = int(crc, 16)
            elif expected_crc is not None and line.startswith(b"@ROW "):
                _, index, pairs = line.split()
                if int(index) != len(rows) or not 8 <= len(pairs) <= 8192 or len(pairs) % 8:
                    raise ValueError("broken scanline")
                row = bytearray()
                for offset in range(0, len(pairs), 8):
                    count = int(pairs[offset:offset + 4], 16)
                    if not 0 < count <= 1024-len(row)//2:
                        raise ValueError("broken pixel run")
                    row.extend(struct.pack("<H", int(pairs[offset+4:offset+8], 16))*count)
                if len(row) != 2048:
                    raise ValueError("incomplete scanline")
                rows.append(row)
            elif line == b"@SHOT END":
                if len(rows) != 600 or zlib.crc32(b"".join(rows)) != expected_crc:
                    raise ValueError("incomplete frame or CRC mismatch")
                return rows
        raise TimeoutError(f"no complete screenshot ({len(rows)} rows received)")


def png(rows):
    def chunk(tag, data):
        return struct.pack(">I", len(data))+tag+data+struct.pack(">I", zlib.crc32(tag+data))
    colors = bytearray(65536*3)
    for n in range(65536):
        r, g, b = n >> 11, (n >> 5) & 63, n & 31
        colors[n*3:n*3+3] = bytes((r<<3|r>>2, g<<2|g>>4, b<<3|b>>2))
    rgb = bytearray()
    for row in rows:
        rgb.append(0)
        for (pixel,) in struct.iter_unpack("<H", row):
            rgb.extend(colors[3*pixel:3*pixel+3])
    hdr = struct.pack(">IIBBBBB", 1024, 600, 8, 2, 0, 0, 0)
    return b"\x89PNG\r\n\x1a\n"+chunk(b"IHDR", hdr)+chunk(b"IDAT", zlib.compress(rgb, 9))+chunk(b"IEND", b"")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("port")
    parser.add_argument("output", nargs="?", type=pathlib.Path)
    args = parser.parse_args()
    rows = capture(args.port)
    out = args.output or pathlib.Path.home()/"Downloads"/datetime.datetime.now().strftime("Weather-display-%Y-%m-%d-%H%M%S.png")
    out.write_bytes(png(rows))
    print(out)
