"""Real daemon/HTTP/stdin regression with synthetic Linux ioctl evidence only.

PRESENT is simulated only for the exact test path and one canonical sysfs lookup;
the underlying inode/fd stays a regular file. No production callback or result
injection. This is not evidence of native hardware identity/observation.
The independent/aligned assertion below is exclusively about this synthetic
fixture, not a real disc, drive, C2 accuracy, or original-audio correctness.
"""
import collections
import hashlib
import json
import os
import pathlib
import shutil
import signal
import socket
import subprocess
import sys
import tempfile
import time
import urllib.error
import urllib.request


BINARY, SHIM = (str(pathlib.Path(arg).resolve()) for arg in sys.argv[1:])
# The linked binary, not a guessed compiler location, owns ASan runtime choice.
BASE_ENV = {key: os.environ[key] for key in (
    "PATH", "LANG", "ASAN_OPTIONS", "UBSAN_OPTIONS", "LSAN_OPTIONS", "TSAN_OPTIONS") if key in os.environ}
dependencies = subprocess.run(["ldd", BINARY], env=BASE_ENV, check=True,
                              capture_output=True, text=True, timeout=2).stdout
asan = [line.split()[2] for line in dependencies.splitlines()
        if line.strip().startswith("libasan.so") and "=>" in line]
assert len(asan) <= 1 and all(pathlib.Path(path).is_file() for path in asan)
PRELOAD = ":".join(asan + [SHIM])
ZERO = "f81c4fa3aa1ad49efe00502d9d9a92330a660f1b0325d9184f23bf478e96e22e"
CHALLENGE = "50a67a54d673811aca2c2c625ddb1eced284d7cd44184301931cb0c3bab214c3"
PROVENANCE = "Synthetic fixture-only independently specified zero/challenge sectors; not real-disc evidence"
assert hashlib.sha256(bytes(2352)).hexdigest() == ZERO
OPENER = urllib.request.build_opener(urllib.request.ProxyHandler({}))
SUITE_DEADLINE = time.monotonic() + 33


class Daemon:
    def __init__(self, scenario):
        self.root = pathlib.Path(tempfile.mkdtemp(prefix="picdplayer-c2-", dir=pathlib.Path(BINARY).parent))
        self.device = self.root / "regular-device"
        self.device.touch()
        if scenario != "success":
            (self.root / scenario).touch()
        with socket.socket() as sock:
            sock.bind(("127.0.0.1", 0))
            self.port = sock.getsockname()[1]
        # Deliberately do not inherit API keys, provider configuration, or preload.
        env = dict(BASE_ENV)
        env.update(HOME=str(self.root), LD_PRELOAD=PRELOAD,
                   C2_FIXTURE_ROOT=str(self.root), C2_FIXTURE_DEVICE=str(self.device),
                   PICDPLAYER_LOG_LEVEL="info")
        if scenario != "unknown":
            sysfs = self.root / "mock-sysfs"
            sysfs.mkdir()
            (sysfs / "busnum").write_text("1\n")
            (sysfs / "devnum").write_text("1\n")
            env["C2_FIXTURE_SYSFS"] = str(sysfs)
        self.log = (self.root / "daemon.log").open("w+b")
        self.process = subprocess.Popen(
            [BINARY, "--player", str(self.device), "--cdda-reader", "direct",
             "--audio-device", "null", "--no-cec", "--interactive",
             "--api-listen", "127.0.0.1", "--api-port", str(self.port),
             "--read-offset-samples", "0"], env=env, stdin=subprocess.PIPE,
            stdout=self.log, stderr=subprocess.STDOUT)

    def request(self, path, body=None):
        assert self.process.poll() is None, "daemon exited"
        assert time.monotonic() < SUITE_DEADLINE, "suite budget exhausted"
        request = urllib.request.Request(
            f"http://127.0.0.1:{self.port}/api/{path}",
            data=body if body is None or isinstance(body, bytes) else json.dumps(body).encode(),
            headers={"Content-Type": "application/json"})
        try:
            response = OPENER.open(request, timeout=0.5)
        except urllib.error.HTTPError as error:
            response = error
        with response:
            data = response.read()
            return response.status, json.loads(data) if data else {}

    def get(self, path="c2-assessment"):
        status, body = self.request(path)
        assert status == 200, (path, status, body)
        self.last_response = (path, body)
        if path == "c2-assessment":
            public = json.dumps(body)
            assert ZERO not in public and CHALLENGE not in public and PROVENANCE not in public
            assert '"sha256"' not in public and '"provenance"' not in public
        return body

    def wait(self, predicate, seconds=3):
        deadline = min(time.monotonic() + seconds, SUITE_DEADLINE)
        while time.monotonic() < deadline:
            assert self.process.poll() is None, "daemon exited during wait"
            try:
                result = predicate()
                if result:
                    return result
            except (urllib.error.URLError, ConnectionError, TimeoutError):
                pass
            time.sleep(0.01)
        raise AssertionError(("bounded daemon wait expired", getattr(self, "last_response", None)))

    def events(self):
        path = self.root / "events"
        return [line.split() for line in path.read_text().splitlines()] if path.exists() else []

    def output(self):
        self.log.flush()
        return (self.root / "daemon.log").read_text(errors="replace")

    def start(self, unknown=False):
        state = self.wait(lambda: value if (value := self.get("state"))["disc"]["state"] == "AUDIO_READY"
                          and value["disc"]["layout"] is not None else None)
        self.wait(lambda: self.get()["start_rejection"] == ("c2_support_not_reported" if unknown else ""))
        observation = state["drive"]["observation"]
        assert observation["presence"] == ("UNKNOWN" if unknown else "PRESENT") and observation["generation"] > 0
        assert state["drive"]["c2_supported"]["value"] == ("UNKNOWN" if unknown else "YES")
        assert state["drive"]["digital_audio_extraction"]["value"] == "UNKNOWN"
        assert state["enrichment"]["status"] == "NOT_REQUESTED"
        c2 = self.get()
        disc = state["disc"]["layout"]["disc_generation"]
        self.command = {"action": "start", "session_id": c2["session_id"],
                        "disc_generation": disc, "observation_generation": observation["generation"],
                        "profile": c2["profile"], "reference": {
                            "id": "synthetic-fixture-only", "provenance": PROVENANCE,
                            "independent": True, "alignment_verified": True, "offset_samples": 0,
                            "sectors": [{"lba": 10, "sha256": ZERO},
                                        {"lba": 20, "sha256": CHALLENGE}]}}
        status, reply = self.request("c2-assessment", self.command)
        if unknown:
            assert status == 409 and reply == {"error": "c2_not_ready", "executed": False, "retry": "manual"}
            result = self.get()
            assert result["state"] == "IDLE" and not result["busy"] and result["reader_released"]
            assert result["value"] == "UNKNOWN" and result["start_rejection"] == "c2_support_not_reported"
            assert not any(event[0] in ("READ_CD", "RAW_CLOSE", "ORDINARY_AUDIO") for event in self.events())
        else:
            assert status == 202, (status, reply)

    def terminal(self, expected, seconds=3):
        result = self.wait(lambda: (value if not value["busy"] else None)
                           if (value := self.get())["state"] == expected else None, seconds)
        assert result["reader_released"] is True
        assert result["value"] == ("YES" if expected == "COMPLETE" else "UNKNOWN"), result
        events = self.events()
        names = [event[0] for event in events]
        assert names.count("RAW_CLOSE") == 1, events
        assert "CLOSE_FAILED" not in names and "INVALID_READ" not in names and "GATE_EXPIRED" not in names
        assert "ORDINARY_AUDIO" not in names, "ordinary extraction masked C2 evidence"
        reads = [int(event[2]) for event in events if event[0] == "READ_CD"]
        assert len(reads) <= 6
        assert all(count <= 3 for count in collections.Counter(reads).values())
        close_index = names.index("RAW_CLOSE")
        assert close_index > max(i for i, name in enumerate(names) if name == "READ_RETURN")
        read_fds = {event[1] for event in events if event[0] == "READ_CD"}
        assert read_fds == {events[close_index][1]}, "raw close must match the actual READ CD fd"
        if expected == "COMPLETE":
            assert reads == [10, 10, 10, 20, 20, 20], reads
            assert result["samples"] == 6 and result["clean_equal"] == 3 and result["flagged_different"] == 3
            assert result["clean_different"] == result["flagged_equal"] == result["unavailable"] == 0
            assert result["source"] == "TESTED" and "conditional" in result["detail"].lower()
            assert "TOC" in names[close_index + 1:], "final TOC must follow raw close"
        print(f"  {expected}: READ_CD={reads}, actual fd={events[close_index][1]} closed after READ_RETURN "
              f"and before terminal receipt; post-close TOC={'TOC' in names[close_index + 1:]}")
        return result

    def shutdown(self):
        self.process.send_signal(signal.SIGTERM)
        try:
            code = self.process.wait(timeout=2)
        except subprocess.TimeoutExpired:
            self.process.kill()  # Failure cleanup only; never evidence of reader release.
            self.process.wait(timeout=1)
            raise AssertionError("normal SIGTERM shutdown failed; forced cleanup used")
        assert code == 0, ("daemon shutdown", code)
        self.process.stdin.close()


def run(scenario):
    daemon = Daemon(scenario)
    try:
        daemon.start(unknown=scenario == "unknown")
        if scenario == "gate":
            daemon.wait(lambda: any(event[0] == "READ_ENTERED" for event in daemon.events()))
            active = daemon.get()
            assert active["busy"] and not active["reader_released"]
            status, reply = daemon.request("c2-assessment", daemon.command)
            assert status == 409 and reply["error"] == "c2_busy"
            status, reply = daemon.request("complete-pcm", {
                "action": "start", "session_id": active["session_id"],
                "disc_generation": daemon.command["disc_generation"],
                "profile": "local-pcm-sha256-v1", "duration_ms": 1000})
            assert status == 409 and reply["error"] == "c2_busy"
            status, _ = daemon.request("c2-assessment", {
                "action": "cancel", "session_id": active["session_id"], "job_id": active["job_id"]})
            assert status == 202
            for path, body in (("play", b""), ("seek", {"offset_seconds": 1})):
                status, reply = daemon.request(path, body)
                assert status == 409 and reply == {
                    "error": "c2_releasing", "executed": False, "retry": "manual"}, (status, reply)
            daemon.process.stdin.write(b"play\n")
            daemon.process.stdin.flush()
            daemon.wait(lambda: "command not executed: capture releasing; retry manually" in daemon.output())
            held = daemon.get()
            assert held["busy"] and not held["reader_released"] and held["value"] == "UNKNOWN"
            assert not any(event[0] in ("RAW_CLOSE", "ORDINARY_AUDIO") for event in daemon.events())
            (daemon.root / "release").touch()
            daemon.terminal("CANCELLED")
            # Wait across snapshot/publication cycles: rejected commands must not be queued.
            until = time.monotonic() + 0.35
            while time.monotonic() < until:
                assert daemon.get("state")["player"]["state"] == "STOPPED"
                assert not any(event[0] == "ORDINARY_AUDIO" for event in daemon.events())
                time.sleep(0.02)
            (daemon.root / "allow_audio").touch()
            assert daemon.request("play", b"")[0] == 204
            daemon.wait(lambda: any(event[0] == "ORDINARY_AUDIO" for event in daemon.events()))
            assert daemon.request("stop", b"")[0] == 204
            daemon.wait(lambda: daemon.get("state")["player"]["state"] == "STOPPED")
        elif scenario != "unknown":
            expected = {"success": "COMPLETE", "short": "FAILED", "changed": "FAILED", "late": "TIMED_OUT"}[scenario]
            result = daemon.terminal(expected, 5 if scenario == "late" else 3)
            events = daemon.events()
            if scenario in ("short", "late"):
                assert sum(event[0] == "READ_CD" for event in events) == 1
            if scenario == "late":
                names = [event[0] for event in events]
                assert not any(name in ("TOC", "LEADOUT") for name in names[names.index("RAW_CLOSE") + 1:]), \
                    "timed-out read must not trigger a final TOC after raw close"
            if scenario == "short":
                assert any(event[0] == "READ_RETURN" and event[2] == "1" for event in events)
            if scenario == "changed":
                assert "after assessment" in result["error"]
                names = [event[0] for event in events]
                assert any(event[0] == "LEADOUT" and event[2] == "76"
                           for event in events[names.index("RAW_CLOSE") + 1:])
        daemon.shutdown()
    except BaseException:
        (daemon.root / "release").touch()
        if daemon.process.poll() is None:
            daemon.process.terminate()
            try:
                daemon.process.wait(timeout=3)
            except subprocess.TimeoutExpired:
                daemon.process.kill()
                daemon.process.wait(timeout=1)
        print(f"FAIL {scenario}: preserved evidence {daemon.root}", file=sys.stderr)
        print(daemon.output(), file=sys.stderr)
        print(daemon.events(), file=sys.stderr)
        raise
    else:
        # Retain failure evidence, remove only this scenario's successful temp files.
        daemon.log.close()
        shutil.rmtree(daemon.root)
        print(f"PASS {scenario}: production daemon synthetic ioctl/command ingress")


for scenario in ("unknown", "success", "short", "changed", "late", "gate"):
    run(scenario)
print("PASS 6 scenarios; PRESENT is fixture-simulated; CEC ingress and real hardware remain unverified")
