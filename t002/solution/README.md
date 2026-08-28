# brakewear – Brake Pad Wear CLI Tool

Reads a CSV export from a diagnostic tester and reports which vehicle brake pads need attention.

## Requirements

- Python 3.10+ (uses `match`-free code, works on 3.8+ actually)
- `pytest` for the test suite (`pip install pytest`)

No third-party runtime dependencies beyond the Python standard library.

## Setup

```bash
# (optional) create a virtual environment
python -m venv .venv
.venv\Scripts\activate          # Windows
# source .venv/bin/activate     # Linux / macOS

pip install pytest
```

## Run

```bash
python brakewear.py <input.csv>
```

### Options

| Flag | Description |
|------|-------------|
| `--status OK\|WARN\|REPLACE` | Filter output to vehicles with that worst status |
| `--json` | Emit machine-readable JSON instead of the text table |

### Examples

```bash
# Text table (all vehicles)
python brakewear.py ..\t002_brake_wear.csv

# JSON output
python brakewear.py ..\t002_brake_wear.csv --json

# Only vehicles that need immediate replacement
python brakewear.py ..\t002_brake_wear.csv --status REPLACE

# Filter + JSON
python brakewear.py ..\t002_brake_wear.csv --status WARN --json
```

## Exit Codes

| Code | Meaning |
|------|---------|
| `0`  | All vehicles are `OK` or `WARN` – no immediate replacement required |
| `1`  | At least one vehicle has a pad in `REPLACE` status |
| `2`  | Input file is missing or cannot be read |

The exit code is always based on the **full** dataset, regardless of any `--status` filter.

## Classification Rules

New pads are 20.0 mm thick.

| Remaining thickness | Status    |
|---------------------|-----------|
| `>= 8.0 mm`         | `OK`      |
| `>= 4.0 and < 8.0`  | `WARN`    |
| `< 4.0 mm`          | `REPLACE` |

## Malformed Rows

The following rows are silently skipped with a `WARNING` printed to stderr:

- fewer than 5 columns
- non-numeric thickness
- negative thickness
- thickness > 20.0 mm

A summary line (`INFO: N malformed row(s) skipped.`) is printed to stderr when at least one row is skipped.
A missing or unreadable file produces a clear `ERROR:` message (no stack trace) and exits with code `2`.

## Sample Output on `t002_brake_wear.csv`

**Text (stderr + stdout):**

```
WARNING: line 17: non-numeric thickness 'abc' – skipped
WARNING: line 19: negative thickness -2.0 – skipped
WARNING: line 22: thickness 22.5 exceeds maximum 20.0 – skipped
WARNING: line 23: missing columns (got 4, need 5) – skipped
INFO: 4 malformed row(s) skipped.
VIN                    PADS  STATUS    WORST PAD
------------------------------------------------
VF3XXXXXXXX778899         6  REPLACE   axle 3 left 3.9 mm
WDB9634031L123456         6  REPLACE   axle 2 right 3.1 mm
WMA06XZZ7BM654321         6  REPLACE   axle 3 right 1.8 mm
XLRTE47MS0E123987         2  WARN      axle 1 left 4.0 mm
YS2R4X20005399401         2  OK        axle 2 left 14.8 mm
ZFA25000002654311         4  OK        axle 2 right 18.5 mm
```

**Exit code:** `1`

**JSON (`--json` flag, stdout only):**

```json
[
  {
    "vin": "VF3XXXXXXXX778899",
    "pads_measured": 6,
    "worst_status": "REPLACE",
    "worst_pad": { "axle": 3, "side": "left", "thickness_mm": 3.9 }
  },
  {
    "vin": "WDB9634031L123456",
    "pads_measured": 6,
    "worst_status": "REPLACE",
    "worst_pad": { "axle": 2, "side": "right", "thickness_mm": 3.1 }
  },
  {
    "vin": "WMA06XZZ7BM654321",
    "pads_measured": 6,
    "worst_status": "REPLACE",
    "worst_pad": { "axle": 3, "side": "right", "thickness_mm": 1.8 }
  },
  {
    "vin": "XLRTE47MS0E123987",
    "pads_measured": 2,
    "worst_status": "WARN",
    "worst_pad": { "axle": 1, "side": "left", "thickness_mm": 4.0 }
  },
  {
    "vin": "YS2R4X20005399401",
    "pads_measured": 2,
    "worst_status": "OK",
    "worst_pad": { "axle": 2, "side": "left", "thickness_mm": 14.8 }
  },
  {
    "vin": "ZFA25000002654311",
    "pads_measured": 4,
    "worst_status": "OK",
    "worst_pad": { "axle": 2, "side": "right", "thickness_mm": 18.5 }
  }
]
```

**Exit code:** `1`

## Tests

```bash
python -m pytest test_brakewear.py -v
```

33 tests covering:
- `classify()` boundary values (4.0, 8.0, 0.0, 20.0, …)
- `parse_csv()`: empty file, header-only, non-numeric, negative, over-max, missing columns, mixed
- `build_summaries()`: sorting order, worst-pad selection, non-contiguous rows
- Integration tests against `t002_brake_wear.csv`: exit code, skipped count, per-vehicle statuses, `--status` filter, `--json` structure, missing file handling

