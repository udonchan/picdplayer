#!/usr/bin/env python3
"""Summarize read-only kiosk JSONL samples without discarding the raw data."""

import argparse
import json
from pathlib import Path
import statistics


def summarize(path):
    rows = [json.loads(line) for line in Path(path).read_text().splitlines() if line]
    if not rows:
        raise ValueError(f"No samples in {path}")
    temperatures = [row["temp_c"] for row in rows if row["temp_c"] is not None]
    cpu = [row["system_cpu_pct_all_cores"] for row in rows if row["system_cpu_pct_all_cores"] is not None]
    current = [row["throttled"] & 0xFFFF for row in rows if row["throttled"] is not None]
    process = {}
    threads = {}
    rss = {}
    roles = {item["name"] + (":" + item["role"] if item["role"] else "")
             for row in rows for item in row["processes"]}
    process = {role: [] for role in roles}
    for row in rows:
        counts = {}
        for item in row["processes"]:
            role = item["name"] + (":" + item["role"] if item["role"] else "")
            counts[role] = counts.get(role, 0) + (item["cpu_pct_one_core"] or 0)
            threads[role] = max(threads.get(role, 0), item["threads"])
            rss[role] = max(rss.get(role, 0), item["rss_bytes"])
        for role in roles:
            process[role].append(counts.get(role, 0))
    frequencies = [value for row in rows for value in row["frequency_khz"].values()]
    return {
        "condition": rows[0]["condition"], "samples": len(rows),
        "duration_s": round(sum(row["elapsed_s"] for row in rows), 1),
        "system_cpu_pct_all_cores": {"mean": round(statistics.mean(cpu), 2), "max": max(cpu)} if cpu else None,
        "temp_c": {"start": temperatures[0], "end": temperatures[-1], "max": max(temperatures)} if temperatures else None,
        "current_throttle_samples": sum(value != 0 for value in current),
        "history_throttle_hex": hex(rows[-1]["throttled"] & ~0xFFFF) if current else None,
        "frequency_khz": {"min": min(frequencies), "max": max(frequencies)} if frequencies else None,
        "mem_available_kib_min": min(row["memory"].get("MemAvailable_kib", 0) for row in rows),
        "swap_in_pages_delta": rows[-1]["swap_io"].get("pswpin", 0) - rows[0]["swap_io"].get("pswpin", 0),
        "swap_out_pages_delta": rows[-1]["swap_io"].get("pswpout", 0) - rows[0]["swap_io"].get("pswpout", 0),
        "process_cpu_pct_one_core": {
            name: {"mean": round(statistics.mean(values), 2), "max": max(values),
                   "max_threads": threads[name], "max_rss_mib": round(rss[name] / 1048576, 1)}
            for name, values in sorted(process.items())},
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("files", nargs="+", type=Path)
    args = parser.parse_args()
    for path in args.files:
        print(json.dumps({"raw_file": str(path), **summarize(path)}, ensure_ascii=False))


if __name__ == "__main__":
    main()
