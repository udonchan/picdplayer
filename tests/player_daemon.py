"""No hardware: persistent player ignores stdin EOF; interactive player does not."""

import signal
import select
import subprocess
import sys
import tempfile
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

# An optional background provider credential failure must not prevent playback
# service startup or expose the configured path in logs.
invalid_key_path = "/nonexistent/picdplayer-artist-secret"
optional = subprocess.Popen(
    [*base, "--metadata", "musicbrainz", "--artist-background-key-file", invalid_key_path],
    stdin=subprocess.DEVNULL,
    stdout=subprocess.PIPE,
    stderr=subprocess.PIPE,
    text=True,
)
try:
    ready, _, _ = select.select([optional.stdout], [], [], 3)
    assert ready, "player did not start with a missing optional key"
    time.sleep(0.3)
    assert optional.poll() is None, optional.communicate(timeout=1)
    optional.send_signal(signal.SIGTERM)
    out, err = optional.communicate(timeout=3)
    assert optional.returncode == 0, (out, err)
    assert "disabled invalid_key_file" in err, err
    assert invalid_key_path not in out + err, (out, err)
finally:
    if optional.poll() is None:
        optional.kill()
        optional.wait()

with tempfile.NamedTemporaryFile(mode="w", encoding="ascii") as key_file:
    key_file.write("test-api-key\n")
    key_file.flush()
    configured = subprocess.Popen(
        [*base, "--metadata", "musicbrainz", "--artist-background-key-file", key_file.name],
        stdin=subprocess.DEVNULL,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
    )
    try:
        ready, _, _ = select.select([configured.stdout], [], [], 3)
        assert ready, "player did not start with a valid optional key"
        time.sleep(0.3)
        configured.send_signal(signal.SIGTERM)
        out, err = configured.communicate(timeout=3)
        assert configured.returncode == 0, (out, err)
        assert "disabled invalid_key_file" not in err, err
        assert "test-api-key" not in out + err, (out, err)
    finally:
        if configured.poll() is None:
            configured.kill()
            configured.wait()

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
