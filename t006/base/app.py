"""
EBS Diagnostic Dashboard -- legacy single-file Flask application.

This is the *starting point* for T006. It is deliberately written the way the
internal tool actually grew: everything in one module, in-memory state, Jinja
templates rendered server side and a jQuery poll every 2 seconds.

Run:
    pip install -r requirements.txt
    python app.py
    -> http://127.0.0.1:5000
"""

import random
import threading
import time
from datetime import datetime, timedelta

from flask import Flask, jsonify, redirect, render_template, url_for, abort

app = Flask(__name__)

# --------------------------------------------------------------------------
# In-memory "database"
# --------------------------------------------------------------------------

DTC_CATALOG = {
    "B1204": "Wheel speed sensor FL - no signal",
    "B1207": "Wheel speed sensor RR - implausible signal",
    "C0045": "Brake pressure sensor front - short to ground",
    "C1095": "EBS reservoir pressure below minimum",
    "U0121": "Lost communication with ABS control module",
    "P0571": "Brake switch A circuit malfunction",
    "C1234": "Pad wear sensor rear axle - open circuit",
}

_NOW = datetime(2026, 3, 17, 8, 30, 0)


def _dtc(code, status, count, first_offset_min, last_offset_min):
    return {
        "code": code,
        "description": DTC_CATALOG[code],
        "status": status,  # "active" | "stored"
        "count": count,
        "first_seen": (_NOW - timedelta(minutes=first_offset_min)).isoformat(timespec="seconds"),
        "last_seen": (_NOW - timedelta(minutes=last_offset_min)).isoformat(timespec="seconds"),
    }


VEHICLES = {
    "WDB9634031L123456": {
        "vin": "WDB9634031L123456",
        "model": "Actros 1845",
        "online": True,
        "signals": {
            "brake_pressure_front_bar": 0.0,
            "brake_pressure_rear_bar": 0.0,
            "reservoir_pressure_bar": 8.2,
            "pad_wear_front_pct": 34.0,
            "pad_wear_rear_pct": 51.0,
            "ambient_temp_c": 11.5,
        },
        "dtcs": [
            _dtc("B1204", "active", 12, 4300, 3),
            _dtc("C1095", "stored", 2, 8800, 1400),
        ],
    },
    "WMA06XZZ7BM654321": {
        "vin": "WMA06XZZ7BM654321",
        "model": "TGX 18.510",
        "online": True,
        "signals": {
            "brake_pressure_front_bar": 0.0,
            "brake_pressure_rear_bar": 0.0,
            "reservoir_pressure_bar": 7.4,
            "pad_wear_front_pct": 12.0,
            "pad_wear_rear_pct": 19.0,
            "ambient_temp_c": 9.8,
        },
        "dtcs": [
            _dtc("C0045", "active", 3, 220, 6),
            _dtc("U0121", "active", 41, 12000, 2),
            _dtc("P0571", "stored", 1, 30000, 29000),
        ],
    },
    "VF3XXXXXXXX778899": {
        "vin": "VF3XXXXXXXX778899",
        "model": "Stralis AS440",
        "online": False,
        "signals": {
            "brake_pressure_front_bar": 0.0,
            "brake_pressure_rear_bar": 0.0,
            "reservoir_pressure_bar": 6.1,
            "pad_wear_front_pct": 78.0,
            "pad_wear_rear_pct": 66.0,
            "ambient_temp_c": 14.2,
        },
        "dtcs": [
            _dtc("C1095", "active", 7, 900, 30),
        ],
    },
    "YS2R4X20005399401": {
        "vin": "YS2R4X20005399401",
        "model": "R 500 6x2",
        "online": True,
        "signals": {
            "brake_pressure_front_bar": 0.0,
            "brake_pressure_rear_bar": 0.0,
            "reservoir_pressure_bar": 9.0,
            "pad_wear_front_pct": 55.0,
            "pad_wear_rear_pct": 48.0,
            "ambient_temp_c": 12.0,
        },
        "dtcs": [
            _dtc("B1207", "stored", 5, 5000, 2100),
            _dtc("C1234", "stored", 2, 6100, 5900),
        ],
    },
}

_lock = threading.Lock()


# --------------------------------------------------------------------------
# Signal simulator -- mutates the in-memory state once per second
# --------------------------------------------------------------------------

def _simulate_once():
    with _lock:
        for v in VEHICLES.values():
            if not v["online"]:
                continue
            s = v["signals"]
            # braking comes in bursts; ~25 % of the ticks are a brake application
            if random.random() < 0.25:
                front = random.uniform(1.5, 7.5)
                s["brake_pressure_front_bar"] = round(front, 2)
                s["brake_pressure_rear_bar"] = round(front * random.uniform(0.80, 1.05), 2)
                s["reservoir_pressure_bar"] = round(
                    max(5.5, s["reservoir_pressure_bar"] - random.uniform(0.05, 0.25)), 2
                )
            else:
                s["brake_pressure_front_bar"] = 0.0
                s["brake_pressure_rear_bar"] = 0.0
                s["reservoir_pressure_bar"] = round(
                    min(10.0, s["reservoir_pressure_bar"] + random.uniform(0.02, 0.12)), 2
                )
            s["pad_wear_front_pct"] = round(max(0.0, s["pad_wear_front_pct"] - 0.001), 3)
            s["pad_wear_rear_pct"] = round(max(0.0, s["pad_wear_rear_pct"] - 0.001), 3)
            s["ambient_temp_c"] = round(
                min(40.0, max(-20.0, s["ambient_temp_c"] + random.uniform(-0.1, 0.1))), 2
            )


def _simulator_loop():
    while True:
        _simulate_once()
        time.sleep(1.0)


# --------------------------------------------------------------------------
# Routes
# --------------------------------------------------------------------------

@app.route("/")
def index():
    rows = []
    with _lock:
        for v in VEHICLES.values():
            rows.append(
                {
                    "vin": v["vin"],
                    "model": v["model"],
                    "online": v["online"],
                    "active_dtcs": sum(1 for d in v["dtcs"] if d["status"] == "active"),
                }
            )
    return render_template("index.html", vehicles=rows)


@app.route("/vehicle/<vin>")
def vehicle_detail(vin):
    with _lock:
        v = VEHICLES.get(vin)
        if v is None:
            abort(404)
        return render_template("vehicle.html", vehicle=v, signals=dict(v["signals"]))


@app.route("/vehicle/<vin>/clear_dtc", methods=["POST"])
def clear_dtc(vin):
    with _lock:
        v = VEHICLES.get(vin)
        if v is None:
            abort(404)
        # only *stored* DTCs are cleared; active faults come straight back
        v["dtcs"] = [d for d in v["dtcs"] if d["status"] == "active"]
    return redirect(url_for("vehicle_detail", vin=vin))


@app.route("/api/vehicle/<vin>/signals")
def api_signals(vin):
    with _lock:
        v = VEHICLES.get(vin)
        if v is None:
            return jsonify({"error": "unknown vin"}), 404
        return jsonify(
            {
                "vin": vin,
                "online": v["online"],
                "signals": dict(v["signals"]),
                "ts": time.time(),
            }
        )


if __name__ == "__main__":
    threading.Thread(target=_simulator_loop, daemon=True).start()
    app.run(host="127.0.0.1", port=5000, debug=False)
