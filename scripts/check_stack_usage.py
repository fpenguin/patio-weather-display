#!/usr/bin/env python3
"""Check compiler-measured frames in project code (not vendor libraries).

This is a regression gate for oversized local objects, not a proof of the
complete task high-water mark: nested calls, vendor TLS, interrupt frames and
dynamic runtime behavior still require on-device measurements.
"""
import argparse
from pathlib import Path
import sys

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("build", type=Path)
parser.add_argument("--maximum-frame", type=int, default=4096)
parser.add_argument("--all", action="store_true", help="print every project frame")
args = parser.parse_args()
root = Path(__file__).resolve().parent.parent
prefixes = [str(root / "main") + "/", str(root / "components") + "/"]
frames = []
for report in args.build.rglob("*.su"):
    for line in report.read_text(errors="replace").splitlines():
        columns = line.rsplit("\t", 2)
        if len(columns) != 3 or not columns[1].isdigit():
            continue
        location, size, kind = columns
        if not any(location.startswith(prefix) for prefix in prefixes):
            continue
        frames.append((int(size), location.removeprefix(str(root) + "/"), kind))
if not frames:
    sys.exit("No project .su records found. Build with -fstack-usage first.")
frames.sort(reverse=True)
failures = [frame for frame in frames
            if frame[0] > args.maximum_frame or ("dynamic" in frame[2] and "bounded" not in frame[2])]
for size, function, kind in frames if args.all else frames[:20]:
    print(f"{size:5} B  {kind:15} {function}")
print(f"{len(frames)} project frames; maximum {frames[0][0]} B; "
      f"limit {args.maximum_frame} B; {len(failures)} violations")
sys.exit(bool(failures))
