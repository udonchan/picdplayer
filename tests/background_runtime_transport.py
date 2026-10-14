#!/usr/bin/env python3
"""Real HTTP/WS component fixture; not cdplayerd/hardware integration."""
import importlib.util
import json
from pathlib import Path
import queue
import socket
import subprocess
import sys
import tempfile
import threading
import time
from urllib.error import HTTPError
from urllib.request import ProxyHandler, Request, build_opener

root = Path(__file__).resolve().parents[1]


def module(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    value = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(value)
    return value


def run(binary):
    fixture = module("background_fixture", root / "scripts/test-background-cdp.py")
    transport = module("cdp_transport", root / "scripts/measure-kiosk-cdp.py")
    with tempfile.TemporaryDirectory(prefix="picdplayer-background-transport-") as directory:
        image = Path(directory) / "synthetic.png"
        image.write_bytes(fixture.png())
        process = subprocess.Popen([binary, "--serve-fixture", "0", str(image)],
                                   stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        logs = queue.Queue()
        stderr = []
        def read_stdout():
            for line in process.stdout:
                if not line.startswith("{"):
                    stderr.append(line)
                    continue
                try:
                    logs.put(json.loads(line))
                except ValueError as error:
                    logs.put(error)
        stdout_reader = threading.Thread(target=read_stdout)
        stderr_reader = threading.Thread(target=lambda: stderr.extend(process.stderr.readlines()))
        stdout_reader.start()
        stderr_reader.start()
        websocket = None
        ws_reader = None
        closing = threading.Event()
        events = queue.Queue()
        counts = {"metadata": 0, "cover": 0, "background": 0, "image": 0}
        opener = build_opener(ProxyHandler({}))
        report = {"runtime": "production-components-with-link-time-fakes", "websocket_snapshots": []}
        try:
            ready = logs.get(timeout=5)
            port = ready["fixture_port"]
            assert port > 0, ready
            origin = f"http://127.0.0.1:{port}"
            def request(path, body=None, method=None):
                data = json.dumps(body).encode() if body is not None else None
                try:
                    response = opener.open(Request(origin + path, data=data, method=method,
                        headers={"Content-Type": "application/json"}), timeout=2)
                except HTTPError as error:
                    response = error
                with response:
                    return response.status, response.read(), response.headers
            def state():
                status, data, _ = request("/api/state")
                assert status == 200
                return json.loads(data)
            def control(value):
                process.stdin.write(value + "\n")
                process.stdin.flush()
            def drain_counts():
                while True:
                    try:
                        entry = logs.get_nowait()
                    except queue.Empty:
                        return
                    if isinstance(entry, Exception):
                        raise entry
                    counts.update(entry.get("fixture_counts", {}))
            def await_condition(predicate):
                deadline = time.monotonic() + 4
                while time.monotonic() < deadline:
                    drain_counts()
                    if predicate():
                        return
                    assert process.poll() is None, stderr
                    time.sleep(0.01)
                raise AssertionError("fixture condition timeout: " + str(counts))
            websocket = transport.WebSocket(f"ws://127.0.0.1:{port}/api/events")
            websocket.socket.settimeout(5)
            def receive():
                while not closing.is_set():
                    try:
                        events.put(websocket.receive())
                    except Exception as error:
                        if not closing.is_set():
                            events.put(error)
                        return
            ws_reader = threading.Thread(target=receive)
            ws_reader.start()
            def await_snapshot(predicate):
                deadline = time.monotonic() + 4
                while time.monotonic() < deadline:
                    value = events.get(timeout=max(0.01, deadline - time.monotonic()))
                    if isinstance(value, Exception):
                        raise value
                    report["websocket_snapshots"].append({"revision": value["revision"],
                        "metadata": value["enrichment"]["status"], "disc": value["disc"]["state"],
                        "background": value["artwork"]["background"]})
                    if predicate(value):
                        return value
                raise AssertionError("WebSocket snapshot timeout")
            metadata = await_snapshot(lambda value: value["enrichment"]["status"] == "AVAILABLE")
            assert metadata["artwork"]["background"] is None
            await_condition(lambda: counts["metadata"] > 0 and counts["cover"] > 0)
            assert counts["background"] == counts["image"] == 0, counts
            policy_before = request("/api/read-policy")[:2]
            assert policy_before[0] == 200
            control("hold")
            assert request("/api/artist-background", {"enabled": True})[0] == 204
            await_condition(lambda: counts["image"] >= 2)
            assert state()["artwork"]["background"] is None
            control("release")
            available = await_snapshot(lambda value: value["artwork"]["background"] is not None)
            background = available["artwork"]["background"]
            first = background["url"]
            assert background["urls"] == [first]
            assert background["dwell_ms"] == 45000 and background["crossfade_ms"] == 4000
            status, data, headers = request(first)
            assert status == 200 and data == image.read_bytes()
            assert headers["Content-Type"] == "image/png" and headers["Cache-Control"] == "no-store"
            # Real production service publishes each newly retained URL over WS, not just a fixture JSON list.
            control("release-later")
            two = await_snapshot(lambda value: value["artwork"]["background"] is not None and
                                 len(value["artwork"]["background"]["urls"]) == 2)
            available = await_snapshot(lambda value: value["artwork"]["background"] is not None and
                                       len(value["artwork"]["background"]["urls"]) == 3)
            references = available["artwork"]["background"]["urls"]
            assert two["revision"] > metadata["revision"] and available["revision"] > two["revision"]
            assert two["artwork"]["background"]["urls"] == references[:2]
            assert available["artwork"]["background"]["url"] == first == references[0]
            assert len(set(references)) == 3
            for index, reference in enumerate(references):
                status, data, headers = request(reference)
                assert status == 200 and data == image.read_bytes() + (str(index + 2).encode() if index else b"")
                assert headers["Cache-Control"] == "no-store"
            assert all("fanart" not in value and "0383dadf" not in value for value in references)
            assert request("/api/artist-background", {"enabled": False})[0] == 204
            await_snapshot(lambda value: value["revision"] > available["revision"] and value["artwork"]["background"] is None)
            assert all(request(reference)[0] == 404 for reference in references)
            drain_counts()
            off_counts = counts.copy()
            time.sleep(0.08)
            drain_counts()
            assert counts == off_counts, (off_counts, counts)
            assert request("/api/read-policy")[:2] == policy_before

            assert request("/api/artist-background", {"enabled": True})[0] == 204
            available = await_snapshot(lambda value: value["artwork"]["background"] is not None and
                                       len(value["artwork"]["background"]["urls"]) == 3)
            await_condition(lambda: counts["image"] >= off_counts["image"] + 2)
            before_hold = counts["image"]
            delayed_reference = available["artwork"]["background"]["url"]
            control("hold")
            await_condition(lambda: counts["image"] >= before_hold + 2)
            await_snapshot(lambda value: value["revision"] > available["revision"] and value["artwork"]["background"] is None)
            assert request("/api/artist-background", {"enabled": False})[0] == 204
            control("release")
            time.sleep(0.05)
            assert state()["artwork"]["background"] is None and request(delayed_reference)[0] == 404
            assert request("/api/artist-background", {"enabled": True})[0] == 204
            available = await_snapshot(lambda value: value["artwork"]["background"] is not None and
                                       len(value["artwork"]["background"]["urls"]) == 3)
            previous = available["artwork"]["background"]["url"]
            previous_references = available["artwork"]["background"]["urls"]
            selection = available["enrichment"]["selection"]
            command = {"session_id": selection["session_id"], "disc_generation": selection["disc_generation"],
                       "metadata_generation": selection["metadata_generation"], "candidate_index": 1}
            assert request("/api/metadata-selection", command)[0] == 204
            assert all(request(reference)[0] == 404 for reference in previous_references)
            changed = await_snapshot(lambda value: value["artwork"]["background"] is not None and
                                     value["artwork"]["background"]["url"] != previous and
                                     len(value["artwork"]["background"]["urls"]) == 3)
            previous = changed["artwork"]["background"]["url"]
            previous_references = changed["artwork"]["background"]["urls"]
            command["candidate_index"] = None
            assert request("/api/metadata-selection", command)[0] == 204
            await_snapshot(lambda value: value["enrichment"]["selection"]["state"] == "DECLINED")
            assert all(request(reference)[0] == 404 for reference in previous_references)
            command["candidate_index"] = 0
            assert request("/api/metadata-selection", command)[0] == 204
            reselected = await_snapshot(lambda value: value["artwork"]["background"] is not None and
                                       len(value["artwork"]["background"]["urls"]) == 3)
            disc_reference = reselected["artwork"]["background"]["url"]
            disc_references = reselected["artwork"]["background"]["urls"]
            assert request("/api/eject", method="POST")[0] == 202
            await_snapshot(lambda value: value["disc"]["state"] == "NO_DISC")
            assert all(request(reference)[0] == 404 for reference in disc_references)
            assert request("/api/artist-background", {"enabled": True})[0] == 204
            drain_counts()
            before_offline = counts["background"]
            control("offline")
            control("insert")
            inserted = await_snapshot(lambda value: value["disc"]["state"] == "AUDIO_READY" and
                                      value["enrichment"]["status"] == "AVAILABLE" and
                                      value["disc"]["layout"]["disc_generation"] != selection["disc_generation"])
            assert inserted["artwork"]["background"] is None
            await_condition(lambda: counts["background"] > before_offline)
            assert request("/api/read-policy")[:2] == policy_before
            report["final_counts"] = counts
            report["result"] = "PASS"
            print(json.dumps(report))
        finally:
            closing.set()
            if websocket:
                try:
                    websocket.socket.shutdown(socket.SHUT_RDWR)
                except OSError:
                    pass
                websocket.socket.close()
            if ws_reader:
                ws_reader.join(timeout=2)
            if process.poll() is None:
                try:
                    process.stdin.write("quit\n")
                    process.stdin.flush()
                    process.wait(timeout=3)
                except (BrokenPipeError, subprocess.TimeoutExpired):
                    process.terminate()
                    process.wait(timeout=3)
            stdout_reader.join(timeout=2)
            stderr_reader.join(timeout=2)
            assert process.returncode == 0, stderr


if __name__ == "__main__":
    run(sys.argv[1])
