"""Custom UI failures must still start the player/API (no physical devices)."""
import json
import pathlib
import socket
import subprocess
import sys
import tempfile
import time
import urllib.request

opener = urllib.request.build_opener(urllib.request.ProxyHandler({}))
binary = str(pathlib.Path(sys.argv[1]).resolve())
with tempfile.TemporaryDirectory(prefix="picdplayer-ui-startup-") as tmp:
    root = pathlib.Path(tmp)
    manifest = {"picdplayer_ui": 1, "requires_api": 1, "name": "test", "entry": "index.html"}
    (root / "player.css").write_text("body{}")
    (root / "player.js").write_text("// test")
    (root / "assets").mkdir()
    image = b"\x89PNG\r\n\x1a\n" + bytes(range(256)) * 512
    (root / "assets" / "test.png").write_bytes(image)
    cases = ["none", "missing-dir", "no-manifest", "malformed", "no-entry", "ui-version", "api-version", "unsafe-entry", "valid"]
    for case in cases:
        (root / "index.html").write_text("CUSTOM CONTENT")
        m = dict(manifest)
        if case == "ui-version": m["picdplayer_ui"] = 2
        if case == "api-version": m["requires_api"] = 2
        if case == "unsafe-entry": m["entry"] = "../outside.html"
        (root / "manifest.json").write_text(json.dumps(m))
        if case == "no-manifest": (root / "manifest.json").unlink()
        if case == "malformed": (root / "manifest.json").write_text("{")
        if case == "no-entry": (root / "index.html").unlink()
        with socket.socket() as sock:
            sock.bind(("127.0.0.1", 0))
            port = sock.getsockname()[1]
        args = [binary, "--player", "/nonexistent", "--cdda-reader", "direct", "--audio-device", "null", "--no-cec", "--api-port", str(port)]
        if case != "none": args += ["--custom-ui", str(root / "absent" if case == "missing-dir" else root)]
        with tempfile.TemporaryFile() as log:
            proc = subprocess.Popen(args, stdout=log, stderr=log)
            try:
                deadline = time.monotonic() + 6
                while True:
                    try:
                        with opener.open(f"http://127.0.0.1:{port}/player", timeout=1) as response:
                            page = response.read().decode()
                        break
                    except OSError:
                        if proc.poll() is not None or time.monotonic() > deadline:
                            log.seek(0)
                            raise AssertionError((case, log.read().decode()))
                        time.sleep(0.05)
                if case == "valid":
                    assert page == "CUSTOM CONTENT", case
                    with opener.open(f"http://127.0.0.1:{port}/player/assets/test.png", timeout=2) as response:
                        assert response.headers["Content-Type"] == "image/png"
                        assert response.read() == image
                else:
                    assert "PiCDPlayer" in page, case
                    assert ("CUSTOM UI DISABLED" in page) == (case != "none"), case
                with opener.open(f"http://127.0.0.1:{port}/api/state", timeout=1) as response:
                    assert json.load(response)["player"]["state"] == "NO_DISC"
            finally:
                proc.terminate()
                try: proc.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    proc.kill(); proc.wait(); raise
            assert proc.returncode == 0, (case, proc.returncode)
print("UI startup cases passed")
