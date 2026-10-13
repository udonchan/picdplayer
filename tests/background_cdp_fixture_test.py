#!/usr/bin/env python3
"""Local fixture checks; these are not Raspberry Pi browser evidence."""
import base64
import importlib.util
import io
import json
from pathlib import Path
import struct
import threading
import unittest
from unittest.mock import patch
from types import SimpleNamespace
from urllib.error import HTTPError
from urllib.request import ProxyHandler, Request, build_opener
import zlib

root = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("background_cdp", root / "scripts/test-background-cdp.py")
fixture = importlib.util.module_from_spec(spec)
spec.loader.exec_module(fixture)


class FixtureTest(unittest.TestCase):
    def test_hidden_default_skips_surface_capture(self):
        for flags, expected in (([], True), (["--isolated-headless"], False)):
            with patch("sys.argv", ["test-background-cdp.py", "smoke", "--output", str(root), *flags]), \
                 patch.object(fixture, "smoke") as smoke:
                fixture.main()
                self.assertEqual(smoke.call_args.args[0].skip_screenshots, expected)

    def test_headless_safety_guards(self):
        version = json.dumps({"Browser": "Chrome/test", "User-Agent": "Chrome/test"}).encode()
        args = SimpleNamespace(fixture_port=18081, cdp_port=19224, isolated_headless=True)
        with patch.object(fixture, "urlopen", return_value=io.BytesIO(version)):
            with self.assertRaisesRegex(RuntimeError, "separately launched headless"):
                fixture.smoke(args)
        with patch.object(fixture, "urlopen", return_value=io.BytesIO(version)), \
             patch("sys.argv", ["test-background-cdp.py", "close-headless"]), \
             patch("sys.stderr", io.StringIO()):
            with self.assertRaises(SystemExit) as error:
                fixture.main()
            self.assertEqual(error.exception.code, 2)

    def test_png(self):
        data = fixture.png()
        self.assertEqual(data[:8], b"\x89PNG\r\n\x1a\n")
        offset = 8
        compressed = b""
        while offset < len(data):
            size = struct.unpack("!I", data[offset:offset + 4])[0]
            kind = data[offset + 4:offset + 8]
            value = data[offset + 8:offset + 8 + size]
            crc = struct.unpack("!I", data[offset + 8 + size:offset + 12 + size])[0]
            self.assertEqual(crc, zlib.crc32(kind + value))
            if kind == b"IHDR":
                self.assertEqual(struct.unpack("!2I5B", value), (320, 180, 8, 2, 0, 0, 0))
            if kind == b"IDAT":
                compressed += value
            offset += size + 12
        self.assertEqual(len(zlib.decompress(compressed)), 180 * (1 + 320 * 3))

    def test_server(self):
        server = fixture.fixture_server(0, root / "ui/default")
        thread = threading.Thread(target=server.serve_forever)
        thread.start()
        opener = build_opener(ProxyHandler({}))
        base = f"http://127.0.0.1:{server.server_port}"
        def request(path, body=None, headers=None):
            try:
                return opener.open(Request(base + path, data=body, headers=headers or {}), timeout=2)
            except HTTPError as error:
                return error
        try:
            with request("/player") as response:
                self.assertIn(b'background-artist', response.read())
                self.assertEqual(response.headers["Cache-Control"], "no-store")
            with request("/api/state") as response:
                state = json.load(response)
                self.assertIsNone(state["artwork"]["background"])
                self.assertNotIn("tracks", {key: value for key, value in state["disc"].items() if key != "layout"})
                self.assertEqual(state["tracks"][0]["number"], state["disc"]["layout"]["tracks"][0]["number"])
            for path in ("/../../AGENTS.md", "/api/play", "/fixture/unknown"):
                with request(path) as response:
                    self.assertEqual(response.status, 404)
            with request("/fixture/jpeg", b"{}") as response:
                self.assertEqual(response.status, 400)
            with request("/fixture/jpeg", b"x", {"Content-Length": str(1024 * 1024 + 1)}) as response:
                self.assertEqual(response.status, 413)
            with request("/fixture/jpeg", b'"anything"', {"Origin": "https://external.invalid"}) as response:
                self.assertEqual(response.status, 403)
            payload = json.dumps("data:image/jpeg;base64," + base64.b64encode(b"synthetic").decode()).encode()
            with request("/fixture/jpeg", payload) as response:
                self.assertEqual(response.status, 204)
            with request("/api/presentation/artwork/background/fixture-jpeg") as response:
                self.assertEqual(response.headers["Content-Type"], "image/jpeg")
                self.assertEqual(response.read(), b"synthetic")
        finally:
            server.shutdown()
            thread.join()
            server.server_close()


if __name__ == "__main__":
    unittest.main()
