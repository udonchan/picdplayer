"""Exercise the real loopback API and saved policy across daemon restarts, without a drive."""

import json
import pathlib
import socket
import subprocess
import sys
import tempfile
import time
import urllib.error
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


def run_daemon(path, check, extra_args=()):
    port = free_port()
    args = [binary, "--player", "/nonexistent", "--cdda-reader", "direct",
            "--audio-device", "null", "--no-cec", "--api-port", str(port)]
    if path is not None:
        args += ["--settings-file", str(path)]
    args += list(extra_args)
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
    repeat = {"mode": "repeat", "region_frames": 75, "required_matches": 2,
              "maximum_attempts": 3, "time_budget_ms": 5000}

    def post_policy(port, value):
        request = urllib.request.Request(
            f"http://127.0.0.1:{port}/api/read-policy",
            data=json.dumps(value).encode(), method="POST",
            headers={"Content-Type": "application/json"})
        with opener.open(request, timeout=2) as response:
            return response.status

    def save(port, policy):
        assert policy["persistence_configured"] is True
        assert policy["requested"]["mode"] == "SINGLE"
        assert policy["requested_source"] == policy["effective_source"] == "startup"
        assert post_policy(port, repeat) == 204
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

    def saved_value_overrides_cli(port, policy):
        assert policy["requested"]["mode"] == "REPEAT"
        assert policy["requested_source"] == "restored"

    run_daemon(settings, saved_value_overrides_cli, ("--read-verification", "single"))

    missing_parent = pathlib.Path(tmp) / "missing" / "settings.json"

    def failed_save_keeps_current_state(port, initial):
        assert initial["requested"]["mode"] == "SINGLE"
        try:
            post_policy(port, repeat)
            raise AssertionError("saving without a parent directory unexpectedly succeeded")
        except urllib.error.HTTPError as error:
            assert error.code == 409
        after = get_policy(port)
        assert after == initial

    run_daemon(missing_parent, failed_save_keeps_current_state)
    settings.write_text("{broken")

    def fallback(_port, policy):
        assert policy["requested"]["mode"] == "SINGLE"
        assert policy["effective"]["mode"] == "SINGLE"
        assert policy["requested_source"] == policy["effective_source"] == "startup"

    run_daemon(settings, fallback)

    def session_only(port, policy):
        assert policy["persistence_configured"] is False
        assert post_policy(port, repeat) == 204
        current = get_policy(port)
        assert current["requested_source"] == current["effective_source"] == "session"

    run_daemon(None, session_only)

print("PASS: API read policy persistence, precedence, and failed-save rollback")
