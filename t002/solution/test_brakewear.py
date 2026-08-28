"""
Test suite for brakewear.py
Run with:  python -m pytest test_brakewear.py -v
"""

import csv
import io
import json
import os
import subprocess
import sys
import tempfile
from pathlib import Path
from unittest.mock import patch

import pytest

# Make sure we can import from the same directory
sys.path.insert(0, str(Path(__file__).parent))

from brakewear import (
    PadRecord,
    VehicleSummary,
    build_summaries,
    classify,
    parse_csv,
)

# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

def _csv_file(rows: list[list[str]]) -> str:
    """Write rows to a temp CSV file, return its path."""
    tf = tempfile.NamedTemporaryFile(
        mode="w", suffix=".csv", delete=False, newline="", encoding="utf-8"
    )
    writer = csv.writer(tf)
    writer.writerows(rows)
    tf.close()
    return tf.name


def _run_cli(*args) -> subprocess.CompletedProcess:
    """Run brakewear.py as a subprocess and return CompletedProcess."""
    script = str(Path(__file__).parent / "brakewear.py")
    return subprocess.run(
        [sys.executable, script, *args],
        capture_output=True,
        text=True,
    )


# ---------------------------------------------------------------------------
# classify() – boundary values
# ---------------------------------------------------------------------------

class TestClassify:
    def test_ok_exact_boundary(self):
        assert classify(8.0) == "OK"

    def test_ok_above_boundary(self):
        assert classify(8.1) == "OK"

    def test_warn_just_below_ok(self):
        assert classify(7.9) == "WARN"

    def test_warn_exact_lower_boundary(self):
        assert classify(4.0) == "WARN"

    def test_replace_just_below_warn(self):
        assert classify(3.9) == "REPLACE"

    def test_replace_zero(self):
        assert classify(0.0) == "REPLACE"

    def test_replace_very_small(self):
        assert classify(0.001) == "REPLACE"

    def test_ok_new_pad(self):
        assert classify(20.0) == "OK"

    def test_ok_midrange(self):
        assert classify(15.0) == "OK"

    def test_warn_midrange(self):
        assert classify(6.0) == "WARN"


# ---------------------------------------------------------------------------
# parse_csv() – various file shapes
# ---------------------------------------------------------------------------

HEADER = ["vin", "axle", "side", "pad_thickness_mm", "measured_at"]


class TestParseCsv:
    def test_empty_file(self):
        tf = tempfile.NamedTemporaryFile(
            mode="w", suffix=".csv", delete=False, encoding="utf-8"
        )
        tf.close()
        records, skipped = parse_csv(tf.name)
        os.unlink(tf.name)
        assert records == []
        assert skipped == 0

    def test_header_only(self):
        path = _csv_file([HEADER])
        records, skipped = parse_csv(path)
        os.unlink(path)
        assert records == []
        assert skipped == 0

    def test_valid_row(self):
        path = _csv_file([HEADER, ["VIN1", "1", "left", "10.0", "2026-01-01"]])
        records, skipped = parse_csv(path)
        os.unlink(path)
        assert len(records) == 1
        assert records[0].thickness == 10.0
        assert skipped == 0

    def test_non_numeric_thickness_skipped(self, capsys):
        path = _csv_file([HEADER, ["VIN1", "1", "left", "abc", "2026-01-01"]])
        records, skipped = parse_csv(path)
        os.unlink(path)
        assert records == []
        assert skipped == 1
        captured = capsys.readouterr()
        assert "WARNING" in captured.err
        assert "line 2" in captured.err

    def test_negative_thickness_skipped(self, capsys):
        path = _csv_file([HEADER, ["VIN1", "1", "left", "-1.0", "2026-01-01"]])
        records, skipped = parse_csv(path)
        os.unlink(path)
        assert records == []
        assert skipped == 1

    def test_thickness_over_max_skipped(self, capsys):
        path = _csv_file([HEADER, ["VIN1", "1", "left", "25.0", "2026-01-01"]])
        records, skipped = parse_csv(path)
        os.unlink(path)
        assert records == []
        assert skipped == 1

    def test_missing_column_skipped(self, capsys):
        path = _csv_file([HEADER, ["VIN1", "1", "left", "10.0"]])  # only 4 columns
        records, skipped = parse_csv(path)
        os.unlink(path)
        assert records == []
        assert skipped == 1

    def test_mixed_valid_and_malformed(self, capsys):
        path = _csv_file([
            HEADER,
            ["VIN1", "1", "left", "10.0", "2026-01-01"],   # valid
            ["VIN2", "1", "left", "bad",  "2026-01-01"],   # non-numeric
            ["VIN3", "1", "left", "-5.0", "2026-01-01"],   # negative
            ["VIN4", "2", "right", "8.5", "2026-01-01"],   # valid
        ])
        records, skipped = parse_csv(path)
        os.unlink(path)
        assert len(records) == 2
        assert skipped == 2

    def test_missing_file_exits(self):
        with pytest.raises(SystemExit) as exc_info:
            parse_csv("/nonexistent/path/file.csv")
        assert exc_info.value.code == 2


# ---------------------------------------------------------------------------
# build_summaries() – sorting and aggregation
# ---------------------------------------------------------------------------

class TestBuildSummaries:
    def _make_record(self, vin, axle, side, thickness):
        return PadRecord(vin, axle, side, thickness, "2026-01-01")

    def test_single_vehicle_ok(self):
        records = [self._make_record("VIN1", 1, "left", 15.0)]
        summaries = build_summaries(records)
        assert len(summaries) == 1
        assert summaries[0].worst_status == "OK"

    def test_worst_pad_chosen(self):
        records = [
            self._make_record("VIN1", 1, "left", 15.0),
            self._make_record("VIN1", 1, "right", 3.5),
        ]
        s = build_summaries(records)[0]
        assert s.worst_status == "REPLACE"
        assert s.worst_thickness == 3.5
        assert s.worst_side == "right"

    def test_sorting_replace_before_warn_before_ok(self):
        records = [
            self._make_record("OK_VIN",      1, "left", 12.0),
            self._make_record("WARN_VIN",    1, "left", 5.0),
            self._make_record("REPLACE_VIN", 1, "left", 2.0),
        ]
        summaries = build_summaries(records)
        assert summaries[0].worst_status == "REPLACE"
        assert summaries[1].worst_status == "WARN"
        assert summaries[2].worst_status == "OK"

    def test_pad_count(self):
        records = [self._make_record("VIN1", i, "left", 10.0) for i in range(1, 5)]
        s = build_summaries(records)[0]
        assert s.pad_count == 4

    def test_vehicles_not_contiguous(self):
        """Rows for different vehicles interleaved – must still aggregate correctly."""
        records = [
            self._make_record("A", 1, "left",  9.0),
            self._make_record("B", 1, "left",  3.0),
            self._make_record("A", 1, "right", 8.5),
            self._make_record("B", 1, "right", 2.5),
        ]
        summaries = build_summaries(records)
        assert summaries[0].vin == "B"
        assert summaries[0].worst_status == "REPLACE"
        assert summaries[1].vin == "A"
        assert summaries[1].worst_status == "OK"


# ---------------------------------------------------------------------------
# CLI integration – full tool against provided t002_brake_wear.csv
# ---------------------------------------------------------------------------

CSV_PATH = str(
    Path(__file__).parent.parent / "t002_brake_wear.csv"
)

EXPECTED_STATUSES = {
    "WDB9634031L123456": "REPLACE",
    "WMA06XZZ7BM654321": "REPLACE",
    "VF3XXXXXXXX778899": "REPLACE",
    "YS2R4X20005399401": "OK",
    "XLRTE47MS0E123987": "WARN",
    "ZFA25000002654311": "OK",
}
EXPECTED_SKIPPED = 4
EXPECTED_EXIT_CODE = 1  # at least one REPLACE


class TestIntegration:
    def test_exit_code_on_provided_csv(self):
        result = _run_cli(CSV_PATH)
        assert result.returncode == EXPECTED_EXIT_CODE

    def test_skipped_row_count(self):
        result = _run_cli(CSV_PATH)
        assert f"{EXPECTED_SKIPPED} malformed row(s) skipped" in result.stderr

    def test_per_vehicle_statuses_json(self):
        result = _run_cli(CSV_PATH, "--json")
        assert result.returncode == EXPECTED_EXIT_CODE
        data = json.loads(result.stdout)
        actual = {entry["vin"]: entry["worst_status"] for entry in data}
        assert actual == EXPECTED_STATUSES

    def test_status_filter_replace(self):
        result = _run_cli(CSV_PATH, "--status", "REPLACE")
        data_lines = [l for l in result.stdout.splitlines() if l.strip() and "---" not in l and "VIN" not in l]
        assert len(data_lines) == 3  # three REPLACE vehicles

    def test_status_filter_ok(self):
        result = _run_cli(CSV_PATH, "--status", "OK")
        data_lines = [l for l in result.stdout.splitlines() if l.strip() and "---" not in l and "VIN" not in l]
        assert len(data_lines) == 2  # two OK vehicles

    def test_status_filter_warn(self):
        result = _run_cli(CSV_PATH, "--status", "WARN")
        data_lines = [l for l in result.stdout.splitlines() if l.strip() and "---" not in l and "VIN" not in l]
        assert len(data_lines) == 1  # one WARN vehicle

    def test_replace_sorted_first_in_text(self):
        result = _run_cli(CSV_PATH)
        lines = [l for l in result.stdout.splitlines() if "REPLACE" in l or "WARN" in l or "OK" in l]
        statuses = [l.split()[2] for l in lines]
        replace_indices = [i for i, s in enumerate(statuses) if s == "REPLACE"]
        warn_indices   = [i for i, s in enumerate(statuses) if s == "WARN"]
        ok_indices     = [i for i, s in enumerate(statuses) if s == "OK"]
        assert max(replace_indices) < min(warn_indices)
        assert max(warn_indices) < min(ok_indices)

    def test_missing_file_no_traceback(self):
        result = _run_cli("no_such_file.csv")
        assert result.returncode == 2
        assert "Traceback" not in result.stderr
        assert "ERROR" in result.stderr

    def test_json_structure(self):
        result = _run_cli(CSV_PATH, "--json")
        data = json.loads(result.stdout)
        for entry in data:
            assert "vin" in entry
            assert "pads_measured" in entry
            assert "worst_status" in entry
            assert "worst_pad" in entry
            assert "axle" in entry["worst_pad"]
            assert "side" in entry["worst_pad"]
            assert "thickness_mm" in entry["worst_pad"]

