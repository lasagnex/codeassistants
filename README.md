# AI Coding Tool Benchmark

Task set for comparing AI coding tools.
Each task lives in its own subfolder as a self-contained `.md` brief.

| Task | Title | Difficulty | Category | Starting material                 |
|------|-------|------------|----------|-----------------------------------|
| [t001](t001/t001.md) | Web App Design (Newton's Second Law calculator) | Easy | Greenfield web app | —                                 |
| [t002](t002/t002.md) | Brake Wear Log CLI | Easy | CLI tool, data parsing | `t002_brake_wear.csv`             |
| [t003](t003/t003.md) | Debug the Wheel Speed Filter | Easy | Debugging, embedded C | [`t003/base/`](t003/base/)        |
| [t004](t004/t004.md) | Braking Measurement Data Analysis | Medium | Data analysis, signal processing | `t004_vehicle_data.xlsx`          |
| [t005](t005/t005.md) | Refactor a Legacy Brake Force Distribution Script | Medium | Refactoring, test harness | [`t005/base/`](t005/base/)        |
| [t006](t006/t006.md) | Framework Migration: Diagnostic Dashboard | Medium | Flask → FastAPI + SPA | [`t006/base/`](t006/base/)        |
| [t007](t007/t007.md) | ABS Controller with Vehicle Simulation | Hard | Control software, fixed point | —                                 |
| [t008](t008/t008.md) | Hunt an Intermittent Fault in Brake ECU Firmware | Hard | Concurrency debugging, RTOS | [`t008/base/`](t008/base/)        |
| [t009](t009/t009.md) | Port a Brake Diagnostics Module from C to Rust | Hard | Language port, UDS protocol | [`t009/base/`](t009/base/)        |
| [t010](t010/t010.md) | Add an `xdd`/`.xdc` Format to `canmatrix` | Hard | Large open-source repo integration | pinned commit of `canmatrix` repo |
| [t011](t011/t011.md) | Brake Pad Measurement Intake Pipeline | Hard | Data pipeline / ETL automation | [`t011/base/`](t011/base/)        |

## Working on a task
1. Read the task `.md`. Work in a fresh working directory containing the task brief and its
   starting material (the data file, or everything inside `base/`).
2. Deliver everything listed under *Requirements*, *Testing* and *Expected Deliverables*.
