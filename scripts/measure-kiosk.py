#!/usr/bin/env python3
"""Bounded, read-only Pi kiosk sampling for Issue #52 (JSON Lines)."""

import argparse
import json
import os
from pathlib import Path
import re
import subprocess
import time
from urllib.request import urlopen

TICKS = os.sysconf("SC_CLK_TCK")
PAGE = os.sysconf("SC_PAGE_SIZE")
NAMES = {"cdplayerd", "cage", "chromium"}


def read(path):
    try:
        return Path(path).read_text()
    except (OSError, UnicodeError):
        return ""


def stat_fields(path):
    raw = read(path)
    end = raw.rfind(")")
    if end < 0:
        return None
    fields = raw[end + 2:].split()
    if len(fields) < 22:
        return None
    return {
        "name": raw[raw.find("(") + 1:end],
        "ppid": int(fields[1]),
        "ticks": int(fields[11]) + int(fields[12]),
        "threads": int(fields[17]),
        "rss_bytes": int(fields[21]) * PAGE,
    }


def system_stats():
    lines = read("/proc/stat").splitlines()
    cpu = list(map(int, lines[0].split()[1:]))
    mem = {}
    for line in read("/proc/meminfo").splitlines():
        if line.startswith(("MemAvailable:", "SwapFree:", "SwapTotal:")):
            key, value, *_ = line.split()
            mem[key[:-1] + "_kib"] = int(value)
    vm = {}
    for line in read("/proc/vmstat").splitlines():
        if line.startswith(("pswpin ", "pswpout ")):
            key, value = line.split()
            vm[key] = int(value)
    frequency = {}
    for path in Path("/sys/devices/system/cpu").glob("cpu[0-9]*/cpufreq/scaling_cur_freq"):
        value = read(path).strip()
        if value.isdigit():
            frequency[path.parts[5]] = int(value)
    return {"cpu_ticks": cpu, "memory": mem, "swap_io": vm,
            "frequency_khz": frequency, "loadavg": read("/proc/loadavg").strip()}


def processes():
    result = {}
    for path in Path("/proc").iterdir():
        if not path.name.isdigit():
            continue
        stat = stat_fields(path / "stat")
        if stat is None or stat["name"] not in NAMES:
            continue
        role = ""
        if stat["name"] == "chromium":
            # Keep only the process type; never record URLs, keys, or full args.
            args = read(path / "cmdline").split("\0")
            # Chromium may put process-title text (including --type) in argv[0].
            match = re.search(r"(?:^|\s)--type=([a-z-]+)", " ".join(args))
            role = match.group(1) if match else "browser"
        tasks = {}
        try:
            for task in (path / "task").iterdir():
                if task.name.isdigit():
                    thread = stat_fields(task / "stat")
                    if thread:
                        tasks[task.name] = {"name": thread["name"], "ticks": thread["ticks"]}
        except OSError:
            pass
        result[path.name] = {**stat, "role": role, "tasks": tasks}
    return result


def firmware():
    result = {}
    for command, key in (("measure_temp", "temp_c"), ("get_throttled", "throttled")):
        try:
            value = subprocess.run(["vcgencmd", command], check=True, capture_output=True,
                                   text=True, timeout=2).stdout.strip().split("=", 1)[1]
            result[key] = float(value.removesuffix("'C")) if key == "temp_c" else int(value, 16)
        except (OSError, ValueError, IndexError, subprocess.SubprocessError):
            result[key] = None
    return result


def player_position():
    try:
        with urlopen("http://127.0.0.1:8080/api/state", timeout=1) as response:
            state = json.load(response).get("player", {})
            return {"state": state.get("state"), "track": state.get("track_number"),
                    "position_frames": state.get("position_frames")}
    except (OSError, ValueError, KeyError):
        return None


def sample(previous, current, elapsed):
    previous_system, previous_processes = previous
    system, procs = current
    total = sum(system["cpu_ticks"]) - sum(previous_system["cpu_ticks"])
    idle = sum(system["cpu_ticks"][3:5]) - sum(previous_system["cpu_ticks"][3:5])
    entries = []
    for pid, proc in procs.items():
        prior = previous_processes.get(pid)
        ticks = max(0, proc["ticks"] - prior["ticks"]) if prior else None
        threads = []
        for tid, task in proc["tasks"].items():
            older = prior["tasks"].get(tid) if prior else None
            if older:
                threads.append({"tid": int(tid), "name": task["name"],
                                "cpu_pct_one_core": round(max(0, task["ticks"] - older["ticks"])
                                                          / TICKS / elapsed * 100, 2)})
        entries.append({"pid": int(pid), "ppid": proc["ppid"], "name": proc["name"],
                        "role": proc["role"], "rss_bytes": proc["rss_bytes"],
                        "threads": proc["threads"],
                        "cpu_pct_one_core": round(ticks / TICKS / elapsed * 100, 2) if ticks is not None else None,
                        "top_threads": sorted(threads, key=lambda item: item["cpu_pct_one_core"], reverse=True)[:5]})
    return {"system_cpu_pct_all_cores": round((total - idle) / total * 100, 2) if total > 0 else None,
            "memory": system["memory"], "swap_io": system["swap_io"],
            "frequency_khz": system["frequency_khz"], "loadavg": system["loadavg"],
            "processes": entries}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--condition", required=True)
    parser.add_argument("--seconds", type=int, default=300)
    parser.add_argument("--interval", type=int, default=5)
    parser.add_argument("--max-temp", type=float, default=78)
    parser.add_argument("--require-player-state", choices=("PLAYING", "STOPPED"))
    args = parser.parse_args()
    if not 1 <= args.interval <= 30 or not 1 <= args.seconds <= 3600:
        parser.error("interval and seconds out of bounds")
    if not 60 <= args.max_temp <= 85:
        parser.error("max-temp must be 60..85 C")
    prior = (system_stats(), processes())
    last = time.monotonic()
    stop_at = last + args.seconds
    while last < stop_at:
        time.sleep(min(args.interval, max(0, stop_at - time.monotonic())))
        now = time.monotonic()
        current = (system_stats(), processes())
        power = firmware()
        player = player_position() if args.require_player_state else None
        print(json.dumps({"condition": args.condition, "time_utc": time.time(),
                          "monotonic": now, "elapsed_s": round(now - last, 3),
                          **sample(prior, current, now - last), **power,
                          **({"player": player} if args.require_player_state else {})}), flush=True)
        if args.require_player_state and (not player or player["state"] != args.require_player_state):
            raise SystemExit("Stopped sampling because playback state changed")
        if power["temp_c"] is not None and power["temp_c"] >= args.max_temp:
            raise SystemExit(f"Stopped sampling at {args.max_temp} C; inspect cooling before continuing")
        if power["throttled"] is not None and power["throttled"] & 0xF:
            raise SystemExit("Stopped sampling after current power or thermal throttling")
        prior, last = current, now


if __name__ == "__main__":
    main()
