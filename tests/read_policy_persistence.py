"""Exercise the real loopback API and saved policy across daemon restarts, without a drive."""

import json
import os
import pathlib
import socket
import subprocess
import sys
import tempfile
import time
import urllib.error
import urllib.request


binary = str(pathlib.Path(sys.argv[1]).resolve())
metadata_compiled = sys.argv[2] == "ON"
opener = urllib.request.build_opener(urllib.request.ProxyHandler({}))


def free_port():
    with socket.socket() as sock:
        sock.bind(("127.0.0.1", 0))
        return sock.getsockname()[1]


def get_policy(port):
    with opener.open(f"http://127.0.0.1:{port}/api/read-policy", timeout=1) as response:
        assert response.status == 200
        return json.load(response)


def run_daemon(path, check, extra_args=(), background_available=False):
    port = free_port()
    args = [binary, "--player", "/nonexistent", "--cdda-reader", "direct",
            "--audio-device", "null", "--no-cec", "--api-port", str(port)]
    if path is not None:
        args += ["--settings-file", str(path)]
    args += list(extra_args)
    environment = dict(os.environ)
    environment.pop("FANART_TV_API_KEY", None)
    if background_available:
        args += ["--metadata", "musicbrainz"]
        environment["FANART_TV_API_KEY"] = "a" * 32  # Synthetic fixture; No Disc never fetches.
    with tempfile.TemporaryFile() as log:
        proc = subprocess.Popen(args, stdout=log, stderr=log, env=environment)
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
    assert saved["schema_version"] == 2
    assert saved["read_policy"]["mode"] == "repeat"
    assert saved["artist_background_enabled"] is False

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

    def get_background(port):
        with opener.open(f"http://127.0.0.1:{port}/api/artist-background", timeout=1) as response:
            assert response.status == 200
            return json.load(response)

    def post_background(port, enabled):
        request = urllib.request.Request(
            f"http://127.0.0.1:{port}/api/artist-background",
            data=json.dumps({"enabled": enabled}).encode(), method="POST",
            headers={"Content-Type": "application/json"})
        with opener.open(request, timeout=2) as response:
            return response.status

    # Existing v1 documents load without eager rewrite; the next explicit save migrates.
    legacy = json.dumps({"schema_version": 1, "read_policy": repeat})
    settings.write_text(legacy)

    def legacy_loaded(port, policy):
        assert policy["requested"]["mode"] == "REPEAT"
        current = get_background(port)
        assert current["requested"] is False
        assert current["persistence_state"] == "saved"
        assert settings.read_text() == legacy
        assert post_policy(port, repeat) == 204

    run_daemon(settings, legacy_loaded)
    assert json.loads(settings.read_text())["schema_version"] == 2

    def cross_writes(port, _policy):
        current = get_background(port)
        assert current["available"] is True and current["requested"] is False
        assert post_background(port, True) == 204
        assert get_background(port)["persistence_state"] == "saved"
        single = dict(repeat, mode="single")
        assert post_policy(port, single) == 204
        assert json.loads(settings.read_text())["artist_background_enabled"] is True
        assert post_background(port, False) == 204
        document = json.loads(settings.read_text())
        assert document["read_policy"] == single
        assert document["artist_background_enabled"] is False
        assert post_background(port, True) == 204
        assert get_background(port)["requested_source"] == "saved"
        assert get_background(port)["persistence_state"] == "saved"  # Not a one-shot result.

    if metadata_compiled:
        run_daemon(settings, cross_writes, background_available=True)
    else:
        settings.write_text(json.dumps({"schema_version": 2, "read_policy": repeat,
                                       "artist_background_enabled": True}))

    def restored_unavailable_and_failed_off(port, _policy):
        current = get_background(port)
        assert current["requested"] is True and current["effective"] is False
        assert current["available"] is False and current["unavailable_reason"] == "metadata_disabled"
        assert current["requested_source"] == "restored" and current["persistence_state"] == "saved"
        # An unavailable fresh ON remains rejected without erasing restored consent.
        try:
            post_background(port, True)
            raise AssertionError("unavailable ON unexpectedly accepted")
        except urllib.error.HTTPError as error:
            assert error.code == 409
        assert get_background(port) == current
        # Block rename without relying on uid/permission behavior; retain the previous file.
        previous = settings.with_name("previous.json")
        settings.rename(previous)
        settings.mkdir()
        assert post_background(port, False) == 204
        off = get_background(port)
        assert off["requested"] is False and off["effective"] is False
        assert off["persistence_state"] == "unsaved" and off["requested_source"] == "session"
        assert json.loads(previous.read_text())["artist_background_enabled"] is True
        settings.rmdir()
        previous.rename(settings)
        # A policy save includes the currently applied OFF and clears the unsaved warning.
        assert post_policy(port, repeat) == 204
        assert get_background(port)["persistence_state"] == "saved"
        assert json.loads(settings.read_text())["artist_background_enabled"] is False

    run_daemon(settings, restored_unavailable_and_failed_off)

    def restored_off(port, _policy):
        current = get_background(port)
        assert current["requested"] is False and current["effective"] is False
        assert current["persistence_state"] == "saved" and current["requested_source"] == "restored"

    run_daemon(settings, restored_off, background_available=metadata_compiled)

    def failed_on_keeps_current(port, _policy):
        initial = get_background(port)
        assert initial["available"] is True and initial["requested"] is False
        try:
            post_background(port, True)
            raise AssertionError("ON without writable save target unexpectedly accepted")
        except urllib.error.HTTPError as error:
            assert error.code == 409
        assert get_background(port) == initial

    if metadata_compiled:
        run_daemon(missing_parent, failed_on_keeps_current, background_available=True)

    def background_session_only(port, _policy):
        assert post_background(port, True) == 204
        current = get_background(port)
        assert current["requested"] is True and current["effective"] is True
        assert current["persistence_configured"] is False
        assert current["persistence_state"] == "session_only" and current["requested_source"] == "session"

    if metadata_compiled:
        run_daemon(None, background_session_only, background_available=True)

    def fresh_session_off(port, _policy):
        assert get_background(port)["requested"] is False

    if metadata_compiled:
        run_daemon(None, fresh_session_off, background_available=True)

print("PASS: joint settings persistence, v1 migration, cross-writes, restored consent, OFF unsaved and rollback")
