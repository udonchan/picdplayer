"""Exercise the real loopback API and saved policy across daemon restarts, without a drive."""

import json
import pathlib
import socket
import subprocess
import sys
import tempfile
import time
import urllib.request


binary = str(pathlib.Path(sys.argv[1]).resolve())
opener = urllib.request.build_opener(urllib.request.ProxyHandler({}))


def free_port():
    with socket.socket() as sock:
        sock.bind(("127.0.0.1", 0))
        return sock.getsockname()[1]


def get_policy(port):
    with opener.open(f"http://127.0.0.1:{port}/api/read-policy", timeout=1) as response:
        assert response.status == 200
        return json.load(response)


def run_daemon(path, check):
    port = free_port()
    args = [binary, "--player", "/nonexistent", "--cdda-reader", "direct",
            "--audio-device", "null", "--no-cec", "--api-port", str(port)]
    if path is not None:
        args += ["--settings-file", str(path)]
    with tempfile.TemporaryFile() as log:
        proc = subprocess.Popen(args, stdout=log, stderr=log)
        try:
            deadline = time.monotonic() + 6
            while True:
                try:
                    policy = get_policy(port)
                    break
                except OSError:
                    if proc.poll() is not None or time.monotonic() > deadline:
                        log.seek(0)
                        raise AssertionError(log.read().decode())
                    time.sleep(0.05)
            check(port, policy)
        finally:
            proc.terminate()
            try:
                proc.wait(timeout=5)
            except subprocess.TimeoutExpired:
                proc.kill()
                proc.wait()
                raise
        assert proc.returncode == 0, proc.returncode


with tempfile.TemporaryDirectory(prefix="picdplayer-policy-persistence-") as tmp:
    settings = pathlib.Path(tmp) / "settings.json"

    def save(port, policy):
        assert policy["persistence_configured"] is True
        assert policy["requested"]["mode"] == "SINGLE"
        assert policy["requested_source"] == policy["effective_source"] == "startup"
        requested = {"mode": "repeat", "region_frames": 75, "required_matches": 2,
                     "maximum_attempts": 3, "time_budget_ms": 5000}
        request = urllib.request.Request(
            f"http://127.0.0.1:{port}/api/read-policy",
            data=json.dumps(requested).encode(), method="POST",
            headers={"Content-Type": "application/json"})
        with opener.open(request, timeout=2) as response:
            assert response.status == 204
        current = get_policy(port)
        assert current["requested"] == current["effective"]
        assert current["requested"]["mode"] == "REPEAT"
        assert current["pending"] is False
        assert current["requested_source"] == current["effective_source"] == "saved"

    run_daemon(settings, save)
    saved = json.loads(settings.read_text())
    assert saved["schema_version"] == 1
    assert saved["read_policy"]["mode"] == "repeat"

    def restored(_port, policy):
        assert policy["requested"]["mode"] == "REPEAT"
        assert policy["effective"]["mode"] == "REPEAT"
        assert policy["pending"] is False
        assert policy["requested_source"] == policy["effective_source"] == "restored"

    run_daemon(settings, restored)
    settings.write_text("{broken")

    def fallback(_port, policy):
        assert policy["requested"]["mode"] == "SINGLE"
        assert policy["effective"]["mode"] == "SINGLE"
        assert policy["requested_source"] == policy["effective_source"] == "startup"

    run_daemon(settings, fallback)

    def session_only(port, policy):
        assert policy["persistence_configured"] is False
        requested = {"mode": "repeat", "region_frames": 75, "required_matches": 2,
                     "maximum_attempts": 3, "time_budget_ms": 5000}
        request = urllib.request.Request(
            f"http://127.0.0.1:{port}/api/read-policy",
            data=json.dumps(requested).encode(), method="POST",
            headers={"Content-Type": "application/json"})
        with opener.open(request, timeout=2) as response:
            assert response.status == 204
        current = get_policy(port)
        assert current["requested_source"] == current["effective_source"] == "session"

    run_daemon(None, session_only)

print("PASS: API read policy persists across restart and rejects corrupt saved data")
