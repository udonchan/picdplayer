"""No hardware: persistent player ignores stdin EOF; interactive player does not."""

import signal
import os
import json
import socket
import select
import subprocess
import sys
import tempfile
import time
import urllib.error
import urllib.request


binary = sys.argv[1]
metadata_enabled = sys.argv[2] == "1"
api_enabled = sys.argv[3] == "1"
os.environ.pop("FANART_TV_API_KEY", None)
opener = urllib.request.build_opener(urllib.request.ProxyHandler({}))


def free_port():
    with socket.socket() as sock:
        sock.bind(("127.0.0.1", 0))
        return sock.getsockname()[1]


def verify_background_api(port, available=False, reason="metadata_disabled"):
    root = f"http://127.0.0.1:{port}/api/"

    def request(path, body=None, method="GET"):
        req = urllib.request.Request(root + path, data=body, method=method,
                                     headers={"Content-Type": "application/json"})
        try:
            with opener.open(req, timeout=1) as response:
                return response.status, response.read()
        except urllib.error.HTTPError as error:
            return error.code, error.read()

    expected = {"requested": False, "effective": False, "available": available,
                "unavailable_reason": None if available else reason}
    deadline = time.monotonic() + 3
    while True:
        try:
            status, body = request("artist-background")
            break
        except OSError:
            assert time.monotonic() < deadline, "background API did not start"
            time.sleep(0.01)
    assert status == 200 and json.loads(body) == expected, (status, body)
    policy_before = request("read-policy")
    assert policy_before[0] == 200
    for payload, expected_status in [(b'{"enabled":true}', 204 if available else 409),
                                      (b'{"enabled":false}', 204),
                                      (b'{"enabled":false}', 204),
                                      (b'{"enabled":0}', 400),
                                      (b'{"enabled":false,"extra":true}', 400),
                                      (b'x' * 513, 413)]:
        status, body = request("artist-background", payload, "POST")
        assert status == expected_status, (status, body)
        if status == 204:
            expected["requested"] = expected["effective"] = json.loads(payload)["enabled"]
        assert json.loads(request("artist-background")[1]) == expected
    assert request("artist-background", method="PUT")[0] == 405
    assert request("read-policy") == policy_before
    state = json.loads(request("state")[1])
    assert state["player"]["state"] == "NO_DISC", state
    assert state["artwork"]["background"] is None, state["artwork"]


base = [
    binary,
    "--player", "/nonexistent",
    "--cdda-reader", "direct",
    "--audio-device", "null",
    "--no-cec",
]

port = free_port()
persistent = subprocess.Popen(
    [*base, *(["--api-port", str(port)] if api_enabled else [])],
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
    if api_enabled:
        verify_background_api(port)
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

if metadata_enabled and api_enabled:
    # Only the native daemon receives the fixture secret; public responses and
    # assets must never contain it, even when a malformed key disables fetching.
    for secret, available in [("fixture-environment-secret-229", True),
                              ("fixture invalid secret-229", False)]:
        port = free_port()
        environment = dict(os.environ, FANART_TV_API_KEY=secret)
        configured = subprocess.Popen(
            [*base, "--metadata", "musicbrainz", "--api-port", str(port)],
            stdin=subprocess.DEVNULL, stdout=subprocess.PIPE,
            stderr=subprocess.PIPE, text=True, env=environment,
        )
        try:
            verify_background_api(port, available=available, reason="key_missing")
            for path in ["/api/state", "/api/artist-background", "/player",
                         "/player.js", "/player.css"]:
                with opener.open(f"http://127.0.0.1:{port}{path}", timeout=1) as response:
                    assert secret.encode() not in response.read(), path
            configured.send_signal(signal.SIGTERM)
            out, err = configured.communicate(timeout=3)
            assert configured.returncode == 0, (out, err)
            assert secret not in out + err, "environment key leaked into daemon output"
        finally:
            if configured.poll() is None:
                configured.kill()
                configured.wait()

    # An optional background provider credential failure must not prevent playback
    # service startup or expose the configured path in logs.
    invalid_key_path = "/nonexistent/picdplayer-artist-secret"
    port = free_port()
    optional = subprocess.Popen(
        [*base, "--metadata", "musicbrainz", "--artist-background-key-file", invalid_key_path,
         "--api-port", str(port)],
        stdin=subprocess.DEVNULL,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
    )
    try:
        ready, _, _ = select.select([optional.stdout], [], [], 3)
        assert ready, "player did not start with a missing optional key"
        verify_background_api(port, reason="key_missing")
        time.sleep(0.3)
        assert optional.poll() is None, optional.communicate(timeout=1)
        optional.send_signal(signal.SIGTERM)
        out, err = optional.communicate(timeout=3)
        assert optional.returncode == 0, (out, err)
        assert "disabled invalid_key" in err, err
        assert invalid_key_path not in out + err, (out, err)
    finally:
        if optional.poll() is None:
            optional.kill()
            optional.wait()

    with tempfile.NamedTemporaryFile(mode="w", encoding="ascii") as key_file:
        key_file.write("test-api-key\n")
        key_file.flush()
        port = free_port()
        configured = subprocess.Popen(
            [*base, "--metadata", "musicbrainz", "--artist-background-key-file", key_file.name,
             "--api-port", str(port)],
            stdin=subprocess.DEVNULL,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
        )
        try:
            ready, _, _ = select.select([configured.stdout], [], [], 3)
            assert ready, "player did not start with a valid optional key"
            verify_background_api(port, available=True)
            time.sleep(0.3)
            configured.send_signal(signal.SIGTERM)
            out, err = configured.communicate(timeout=3)
            assert configured.returncode == 0, (out, err)
            assert "disabled invalid_key" not in err, err
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
