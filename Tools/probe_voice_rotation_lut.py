#!/usr/bin/env python3
"""Offline numerical study of linear sine/cosine tables for a voice rotation.

No DSP is changed and no performance is measured. The repeated-rotation metric
is a conservative constant-angle illustration, not a rendered instrument decay.
"""
import argparse
import json
from pathlib import Path
import numpy as np


def study():
    angles = np.linspace(0.0, 0.25, 200001)
    sine, cosine = np.sin(angles), np.cos(angles)
    rows = []
    for intervals in (16, 64, 256, 1024, 4096):
        knots = np.linspace(0.0, 0.25, intervals + 1)
        table_sine, table_cosine = np.sin(knots), np.cos(knots)
        approximate_sine = np.interp(angles, knots, table_sine)
        approximate_cosine = np.interp(angles, knots, table_cosine)
        norm2 = approximate_sine ** 2 + approximate_cosine ** 2
        rows.append({
            "intervals": intervals,
            "table_bytes": int(table_sine.nbytes + table_cosine.nbytes),
            "max_sine_error": float(np.max(np.abs(approximate_sine - sine))),
            "max_cosine_error": float(np.max(np.abs(approximate_cosine - cosine))),
            "max_angle_error_rad": float(np.max(np.abs(np.arctan2(approximate_sine, approximate_cosine) - angles))),
            "min_squared_norm": float(np.min(norm2)),
            "max_squared_norm": float(np.max(norm2)),
            "worst_constant_rotation_extra_energy_db_after_100ms_at_96k": float(10 * 9600 * np.log10(np.min(norm2))),
        })
    return {
        "scope": "Offline numerical lookup-table feasibility only, no timing or DSP edit. Range 0..0.25 radians; outside needs exact fallback.",
        "grid_points": len(angles),
        "method": "Independent linear interpolation of paired double sine/cosine samples; no renormalization.",
        "results": rows,
        "interpretation": "Interpolated unit-circle chords are contractive in exact arithmetic. Repeated use can add damping; floating-point evaluation also needs a norm guard. This does not assess normalized, higher-order, or other function tables.",
    }


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    args.output.write_text(json.dumps(study(), indent=2) + "\n")
