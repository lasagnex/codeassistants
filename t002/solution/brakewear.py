#!/usr/bin/env python3
"""
brakewear – Brake Pad Wear CLI Tool
Usage: python brakewear.py <input.csv> [--status OK|WARN|REPLACE] [--json]
"""

import argparse
import csv
import json
import sys
from dataclasses import dataclass, field
from typing import Optional

# Thresholds (inclusive lower bound)
REPLACE_THRESHOLD = 4.0
WARN_THRESHOLD = 8.0
MAX_THICKNESS = 20.0

STATUS_ORDER = {"REPLACE": 0, "WARN": 1, "OK": 2}


def classify(thickness: float) -> str:
    if thickness < REPLACE_THRESHOLD:
        return "REPLACE"
    if thickness < WARN_THRESHOLD:
        return "WARN"
    return "OK"


@dataclass
class PadRecord:
    vin: str
    axle: int
    side: str
    thickness: float
    measured_at: str


@dataclass
class VehicleSummary:
    vin: str
    pad_count: int
    worst_status: str
    worst_axle: int
    worst_side: str
    worst_thickness: float


def parse_csv(path: str) -> tuple[list[PadRecord], int]:
    """
    Parse CSV file. Returns (records, skipped_count).
    Prints warnings to stderr for skipped rows.
    """
    records: list[PadRecord] = []
    skipped = 0

    try:
        fh = open(path, newline="", encoding="utf-8")
    except OSError as exc:
        print(f"ERROR: Cannot open file '{path}': {exc.strerror}", file=sys.stderr)
        sys.exit(2)

    with fh:
        reader = csv.reader(fh)
        for lineno, row in enumerate(reader, start=1):
            if lineno == 1:
                # header row – skip
                continue
            if not any(cell.strip() for cell in row):
                # blank line – skip silently
                continue

            if len(row) < 5:
                print(
                    f"WARNING: line {lineno}: missing columns (got {len(row)}, need 5) – skipped",
                    file=sys.stderr,
                )
                skipped += 1
                continue

            vin, axle_str, side, thickness_str, measured_at = (
                row[0].strip(),
                row[1].strip(),
                row[2].strip(),
                row[3].strip(),
                row[4].strip(),
            )

            try:
                thickness = float(thickness_str)
            except ValueError:
                print(
                    f"WARNING: line {lineno}: non-numeric thickness '{thickness_str}' – skipped",
                    file=sys.stderr,
                )
                skipped += 1
                continue

            if thickness < 0:
                print(
                    f"WARNING: line {lineno}: negative thickness {thickness} – skipped",
                    file=sys.stderr,
                )
                skipped += 1
                continue

            if thickness > MAX_THICKNESS:
                print(
                    f"WARNING: line {lineno}: thickness {thickness} exceeds maximum {MAX_THICKNESS} – skipped",
                    file=sys.stderr,
                )
                skipped += 1
                continue

            try:
                axle = int(axle_str)
            except ValueError:
                print(
                    f"WARNING: line {lineno}: non-integer axle '{axle_str}' – skipped",
                    file=sys.stderr,
                )
                skipped += 1
                continue

            records.append(PadRecord(vin, axle, side, thickness, measured_at))

    return records, skipped


def build_summaries(records: list[PadRecord]) -> list[VehicleSummary]:
    """Aggregate pad records into per-vehicle summaries."""
    vehicles: dict[str, list[PadRecord]] = {}
    for rec in records:
        vehicles.setdefault(rec.vin, []).append(rec)

    summaries: list[VehicleSummary] = []
    for vin, pads in vehicles.items():
        worst_pad = min(pads, key=lambda p: p.thickness)
        worst_status = classify(worst_pad.thickness)
        summaries.append(
            VehicleSummary(
                vin=vin,
                pad_count=len(pads),
                worst_status=worst_status,
                worst_axle=worst_pad.axle,
                worst_side=worst_pad.side,
                worst_thickness=worst_pad.thickness,
            )
        )

    # Sort: REPLACE < WARN < OK, then by VIN for stability
    summaries.sort(key=lambda s: (STATUS_ORDER[s.worst_status], s.vin))
    return summaries


def print_table(summaries: list[VehicleSummary]) -> None:
    """Print human-readable table."""
    if not summaries:
        print("No vehicles to report.")
        return

    header = f"{'VIN':<22} {'PADS':>4}  {'STATUS':<8}  {'WORST PAD'}"
    print(header)
    print("-" * len(header))
    for s in summaries:
        worst = f"axle {s.worst_axle} {s.worst_side} {s.worst_thickness:.1f} mm"
        print(f"{s.vin:<22} {s.pad_count:>4}  {s.worst_status:<8}  {worst}")


def print_json(summaries: list[VehicleSummary]) -> None:
    """Print JSON output."""
    data = [
        {
            "vin": s.vin,
            "pads_measured": s.pad_count,
            "worst_status": s.worst_status,
            "worst_pad": {
                "axle": s.worst_axle,
                "side": s.worst_side,
                "thickness_mm": s.worst_thickness,
            },
        }
        for s in summaries
    ]
    print(json.dumps(data, indent=2))


def main() -> None:
    parser = argparse.ArgumentParser(
        description="Report brake pad wear status from a CSV export."
    )
    parser.add_argument("input", help="Path to the CSV input file")
    parser.add_argument(
        "--status",
        choices=["OK", "WARN", "REPLACE"],
        help="Filter output to vehicles with this worst status",
    )
    parser.add_argument(
        "--json", action="store_true", dest="as_json", help="Emit JSON output"
    )
    args = parser.parse_args()

    records, skipped = parse_csv(args.input)

    if skipped:
        print(f"INFO: {skipped} malformed row(s) skipped.", file=sys.stderr)

    summaries = build_summaries(records)

    if args.status:
        summaries = [s for s in summaries if s.worst_status == args.status]

    if args.as_json:
        print_json(summaries)
    else:
        print_table(summaries)

    # Exit 1 if any vehicle (in the full unfiltered list) needs REPLACE
    all_summaries = build_summaries(records)
    has_replace = any(s.worst_status == "REPLACE" for s in all_summaries)
    sys.exit(1 if has_replace else 0)


if __name__ == "__main__":
    main()

