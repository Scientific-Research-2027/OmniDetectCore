#!/usr/bin/env python3
"""Chạy soak OmniDetectCore trên Linux và thu CPU/RSS/nhiệt độ, không cần package ngoài."""

from __future__ import annotations

import argparse
import csv
import json
import os
import signal
import subprocess
import sys
import threading
import time
from pathlib import Path


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Soak test đa camera OmniDetectCore trên Linux")
    parser.add_argument("--duration-hours", type=float, default=8.0)
    parser.add_argument("--interval-seconds", type=float, default=10.0)
    parser.add_argument("--output-dir", type=Path, default=Path("soak-report"))
    parser.add_argument("command", nargs=argparse.REMAINDER)
    args = parser.parse_args()
    if args.command and args.command[0] == "--":
        args.command = args.command[1:]
    if not args.command:
        parser.error("thiếu lệnh sau --")
    if args.duration_hours <= 0 or args.interval_seconds <= 0:
        parser.error("duration/interval phải dương")
    return args


def process_ticks(pid: int) -> int | None:
    try:
        text = Path(f"/proc/{pid}/stat").read_text(encoding="utf-8")
        fields = text[text.rfind(")") + 2 :].split()
        return int(fields[11]) + int(fields[12])
    except (OSError, ValueError, IndexError):
        return None


def resident_kib(pid: int) -> int | None:
    try:
        for line in Path(f"/proc/{pid}/status").read_text(encoding="utf-8").splitlines():
            if line.startswith("VmRSS:"):
                return int(line.split()[1])
    except (OSError, ValueError, IndexError):
        pass
    return None


def temperature_celsius() -> float | None:
    candidates = sorted(Path("/sys/class/thermal").glob("thermal_zone*/temp"))
    for path in candidates:
        try:
            value = float(path.read_text(encoding="utf-8").strip())
            return value / 1000.0 if value > 200.0 else value
        except (OSError, ValueError):
            continue
    return None


def parse_key_values(line: str) -> dict[str, str]:
    result: dict[str, str] = {}
    for field in line.split()[1:]:
        if "=" in field:
            key, value = field.split("=", 1)
            result[key] = value
    return result


def main() -> int:
    if not sys.platform.startswith("linux"):
        print("Công cụ soak này cần Linux (/proc và thermal sysfs).", file=sys.stderr)
        return 2
    args = parse_args()
    args.output_dir.mkdir(parents=True, exist_ok=True)
    log_path = args.output_dir / "omnidetect.log"
    csv_path = args.output_dir / "samples.csv"
    application_csv_path = args.output_dir / "application_metrics.csv"
    report_path = args.output_dir / "report.json"
    lines: list[str] = []
    line_lock = threading.Lock()
    started_wall = time.time()
    started_monotonic = time.monotonic()
    process = subprocess.Popen(
        args.command,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        bufsize=1,
        start_new_session=True,
    )

    def read_output() -> None:
        assert process.stdout is not None
        with log_path.open("w", encoding="utf-8") as log_file:
            for line in process.stdout:
                print(line, end="")
                log_file.write(line)
                log_file.flush()
                with line_lock:
                    lines.append(line.rstrip("\n"))

    reader = threading.Thread(target=read_output, name="soak-log-reader", daemon=True)
    reader.start()
    ticks_per_second = os.sysconf("SC_CLK_TCK")
    previous_ticks = process_ticks(process.pid)
    previous_time = time.monotonic()
    samples: list[dict[str, object]] = []
    deadline = started_monotonic + args.duration_hours * 3600.0

    try:
        while process.poll() is None and time.monotonic() < deadline:
            time.sleep(min(args.interval_seconds, max(0.0, deadline - time.monotonic())))
            now = time.monotonic()
            ticks = process_ticks(process.pid)
            cpu_percent = None
            if ticks is not None and previous_ticks is not None and now > previous_time:
                cpu_percent = 100.0 * (ticks - previous_ticks) / ticks_per_second / (now - previous_time)
            sample = {
                "elapsed_seconds": round(now - started_monotonic, 3),
                "cpu_percent_one_core_100": None if cpu_percent is None else round(cpu_percent, 3),
                "rss_kib": resident_kib(process.pid),
                "temperature_c": temperature_celsius(),
            }
            samples.append(sample)
            previous_ticks = ticks
            previous_time = now
    finally:
        if process.poll() is None:
            os.killpg(process.pid, signal.SIGTERM)
            try:
                process.wait(timeout=30)
            except subprocess.TimeoutExpired:
                os.killpg(process.pid, signal.SIGKILL)
                process.wait(timeout=5)
        reader.join(timeout=5)

    with csv_path.open("w", newline="", encoding="utf-8") as output:
        writer = csv.DictWriter(output, fieldnames=[
            "elapsed_seconds", "cpu_percent_one_core_100", "rss_kib", "temperature_c"
        ])
        writer.writeheader()
        writer.writerows(samples)

    with line_lock:
        captured_lines = list(lines)
    summaries = [parse_key_values(line) for line in captured_lines if line.startswith("summary ")]
    camera_summaries = [parse_key_values(line) for line in captured_lines if line.startswith("camera_summary ")]
    application_rows: list[dict[str, str]] = []
    for line in captured_lines:
        if line.startswith("health ") or line.startswith("camera_health "):
            row = parse_key_values(line)
            row["kind"] = "camera" if line.startswith("camera_health ") else "global"
            application_rows.append(row)
    fieldnames = ["kind"] + sorted({key for row in application_rows for key in row if key != "kind"})
    with application_csv_path.open("w", newline="", encoding="utf-8") as output:
        writer = csv.DictWriter(output, fieldnames=fieldnames)
        writer.writeheader()
        writer.writerows(application_rows)
    report = {
        "command": args.command,
        "started_unix": started_wall,
        "duration_seconds": round(time.monotonic() - started_monotonic, 3),
        "exit_code": process.returncode,
        "sample_count": len(samples),
        "maximum_rss_kib": max((sample["rss_kib"] for sample in samples if sample["rss_kib"] is not None), default=None),
        "maximum_temperature_c": max((sample["temperature_c"] for sample in samples if sample["temperature_c"] is not None), default=None),
        "disconnect_messages": sum("disconnected" in line.lower() for line in captured_lines),
        "recovery_messages": sum("recovered" in line.lower() for line in captured_lines),
        "final_summary": summaries[-1] if summaries else None,
        "camera_summaries": camera_summaries,
        "application_metric_rows": len(application_rows),
        "verdict": "manual-review-required",
    }
    report_path.write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding="utf-8")
    print(f"Đã ghi {csv_path}, {application_csv_path}, {log_path} và {report_path}")
    return 0 if process.returncode == 0 else process.returncode


if __name__ == "__main__":
    raise SystemExit(main())
