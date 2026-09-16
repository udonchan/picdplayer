"""No hardware: persistent player ignores stdin EOF; interactive player does not."""

import signal
import select
import subprocess
import sys
import time


binary = sys.argv[1]
base = [
    binary,
    "--player", "/nonexistent",
    "--cdda-reader", "direct",
    "--audio-device", "null",
    "--no-cec",
]

persistent = subprocess.Popen(
    base,
    stdin=subprocess.DEVNULL,
    stdout=subprocess.PIPE,
    stderr=subprocess.PIPE,
    text=True,
)
try:
    # Wait for startup output before sending a signal. A fixed startup sleep
    # can kill a slow process before its signalfd handler has been installed.
    ready, _, _ = select.select([persistent.stdout], [], [], 3)
    assert ready, "player did not report startup"
    startup = persistent.stdout.readline() + persistent.stdout.readline()
    assert "stdin_commands=disabled" in startup, startup
    time.sleep(0.3)
    assert persistent.poll() is None, persistent.communicate(timeout=1)
    persistent.send_signal(signal.SIGTERM)
    out, err = persistent.communicate(timeout=3)
    out = startup + out
    assert persistent.returncode == 0, (out, err)
    assert "state=NO_DISC" in out, out
    assert f"shutdown signal={int(signal.SIGTERM)}" in out, out
finally:
    if persistent.poll() is None:
        persistent.kill()
        persistent.wait()

interactive = subprocess.run(
    [*base, "--interactive"],
    stdin=subprocess.DEVNULL,
    capture_output=True,
    text=True,
    timeout=3,
)
assert interactive.returncode == 0, interactive
assert "stdin_commands=enabled" in interactive.stdout, interactive.stdout
assert "output stopped" in interactive.stdout, interactive.stdout

print("PASS: daemon ignores stdin EOF; interactive mode exits on EOF")
