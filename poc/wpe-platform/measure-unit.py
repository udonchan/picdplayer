#!/usr/bin/env python3
"""Read-only short CPU/PSS sample of a systemd unit's MainPID process tree."""

import json
import os
import pathlib
import subprocess
import sys
import time


def descendants(root):
    table = subprocess.check_output(["ps", "-eo", "pid=,ppid="], text=True)
    children = {}
    for line in table.splitlines():
        pid, parent = map(int, line.split())
        children.setdefault(parent, []).append(pid)
    result = [root]
    for pid in result:
        result.extend(children.get(pid, ()))
    return result


def sample(root):
    pids = descendants(root)
    ticks = 0
    pss = 0
    for pid in pids:
        try:
            proc = pathlib.Path(f"/proc/{pid}")
            fields = proc.joinpath("stat").read_text().rsplit(")", 1)[1].split()
            ticks += int(fields[11]) + int(fields[12])
            rollup = proc.joinpath("smaps_rollup").read_text()
            pss += next(int(line.split()[1]) for line in rollup.splitlines()
                        if line.startswith("Pss:"))
        except (FileNotFoundError, PermissionError, ProcessLookupError, StopIteration):
            pass
    return ticks, pids, pss


if len(sys.argv) not in (2, 3):
    sys.exit(f"usage: {sys.argv[0]} SYSTEMD_UNIT [SECONDS]")

unit = sys.argv[1]
duration = float(sys.argv[2]) if len(sys.argv) == 3 else 10.0
if not 1 <= duration <= 60:
    sys.exit("SECONDS must be between 1 and 60")
root = int(subprocess.check_output(
    ["systemctl", "show", "-P", "MainPID", unit], text=True).strip())
if not root:
    sys.exit(f"unit has no MainPID: {unit}")
start_time = time.monotonic()
start_cpu, start_pids, start_pss = sample(root)
time.sleep(duration)
end_cpu, end_pids, end_pss = sample(root)
elapsed = time.monotonic() - start_time
print(json.dumps({
    "unit": unit,
    "elapsed_seconds": round(elapsed, 3),
    "cpu_percent_one_core": round(100 * (end_cpu - start_cpu) /
                                  (elapsed * os.sysconf("SC_CLK_TCK")), 2),
    "process_count_start": len(start_pids),
    "process_count_end": len(end_pids),
    "pss_kib_start": start_pss,
    "pss_kib_end": end_pss,
}))
