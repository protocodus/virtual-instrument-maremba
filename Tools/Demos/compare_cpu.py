#!/usr/bin/env python3
"""Order-balanced, serial comparison of two performance_review executables.

Compile the same probe against frozen baseline and candidate DSP trees first.
All measurements (including misses and unsuccessful runs) are retained. This
measures the native engine only, not Reason scheduling or target translation.
"""
import argparse
import hashlib
import json
import platform
import re
import statistics
import subprocess
import time
from pathlib import Path


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("baseline", type=Path)
    parser.add_argument("candidate", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--rounds", type=int, default=2)
    parser.add_argument("--blocks", type=int, default=4096)
    args = parser.parse_args()
    if args.rounds < 2 or args.rounds % 2 or args.blocks < 256:
        parser.error("use an even number of at least two rounds and >=256 blocks")
    args.output.mkdir(parents=True, exist_ok=False)
    binaries = {name: getattr(args, name).resolve() for name in ("baseline", "candidate")}
    # All models and qualities at 48 kHz, the other default rooms, and 96 kHz at 8x.
    cases = [(m, q, 0, 48000) for m in range(4) for q in range(3)]
    cases += [(0, 0, r, 48000) for r in (1, 2)]
    cases += [(m, 2, 0, 96000) for m in range(4)]
    report = {"scope": "native engine only", "platform": platform.platform(),
              "binaries": {name: {"path": str(path), "sha256": digest(path)}
                           for name, path in binaries.items()},
              "rounds": args.rounds, "blocks": args.blocks, "warmup_blocks": 256,
              "frames": 64, "voices": 16, "runs": []}
    destination = args.output / "results.json"
    for round_index in range(args.rounds):
        ordered = cases if round_index % 2 == 0 else list(reversed(cases))
        for case_index, case in enumerate(ordered):
            model, quality, room, rate = case
            labels = ["baseline", "candidate"] if (round_index + cases.index(case)) % 2 == 0 else ["candidate", "baseline"]
            for label in labels:
                name = f"r{round_index}-m{model}-q{quality}-room{room}-{rate}-{label}"
                command = [str(binaries[label]), "--case", *map(str, case), str(args.blocks)]
                start = time.time()
                try:
                    result = subprocess.run(command, capture_output=True, text=True, timeout=300)
                    raw = result.stdout + result.stderr
                    returncode = result.returncode
                except subprocess.TimeoutExpired as error:
                    def as_text(value):
                        return value.decode(errors="replace") if isinstance(value, bytes) else value or ""
                    raw = as_text(error.stdout) + as_text(error.stderr) + "\nTIMED OUT after 300 seconds\n"
                    returncode = 124
                (args.output / f"{name}.txt").write_text(raw)
                run = {"round": round_index, "case": list(case), "label": label,
                       "command": command, "started_unix": start,
                       "elapsed_seconds": time.time() - start, "exit": returncode,
                       "raw": raw}
                for field, pattern in {
                    "wall": r"wall_us p50=([\d.e+\-]+) p95=([\d.e+\-]+) p99=([\d.e+\-]+) max=([\d.e+\-]+)",
                    "cpu": r"thread_cpu_us p50=([\d.e+\-]+) p95=([\d.e+\-]+) p99=([\d.e+\-]+) max=([\d.e+\-]+)",
                    "misses": r"deadline_misses wall=(\d+) cpu=(\d+)",
                    "mean": r"mean_us wall=([\d.e+\-]+) cpu=([\d.e+\-]+)",
                }.items():
                    match = re.search(pattern, raw)
                    if match:
                        keys = ("p50", "p95", "p99", "max") if field in ("wall", "cpu") else ("wall", "cpu")
                        run[field] = dict(zip(keys, map(float, match.groups())))
                report["runs"].append(run)
                destination.write_text(json.dumps(report, indent=2) + "\n")
                if returncode or any(key not in run for key in ("wall", "cpu", "misses", "mean")):
                    raise SystemExit(f"Invalid run retained: {name}")
            print(f"round {round_index + 1}/{args.rounds}, case {case_index + 1}/{len(cases)}: {case}", flush=True)
    summary = []
    for case in cases:
        item = {"case": list(case)}
        for field, key in (("cpu", "p50"), ("mean", "cpu"), ("wall", "p50"), ("cpu", "p95"), ("cpu", "p99")):
            values = {label: statistics.median(run[field][key] for run in report["runs"]
                       if run["case"] == list(case) and run["label"] == label) for label in binaries}
            item[f"{field}_{key}"] = {**values, "reduction_percent": 100 * (1 - values["candidate"] / values["baseline"])}
        summary.append(item)
    report["summary"] = summary
    destination.write_text(json.dumps(report, indent=2) + "\n")
    for item in summary:
        print(item["case"], "CPU median reduction", round(item["cpu_p50"]["reduction_percent"], 2), "%")


if __name__ == "__main__":
    main()
