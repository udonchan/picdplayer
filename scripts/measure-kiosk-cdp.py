#!/usr/bin/env python3
"""Short Chrome DevTools Protocol measurement over an SSH forwarded port.

This client runs on the Mac. It does not install dependencies on the Pi.
"""

import argparse
import base64
import hashlib
import json
import os
from pathlib import Path
import socket
import struct
import time
from urllib.request import urlopen


class WebSocket:
    def __init__(self, url):
        # CDP's page target is only accessed through localhost SSH forwarding.
        assert url.startswith("ws://127.0.0.1:")
        host_path = url[5:]
        address, path = host_path.split("/", 1)
        host, port = address.split(":", 1)
        self.socket = socket.create_connection((host, int(port)), timeout=15)
        try:
            self.socket.settimeout(20)
            key = base64.b64encode(os.urandom(16)).decode()
            request = (f"GET /{path} HTTP/1.1\r\nHost: {address}\r\nUpgrade: websocket\r\n"
                       f"Connection: Upgrade\r\nSec-WebSocket-Key: {key}\r\nSec-WebSocket-Version: 13\r\n\r\n")
            self.socket.sendall(request.encode())
            reply = b""
            while b"\r\n\r\n" not in reply:
                data = self.socket.recv(4096)
                if not data:
                    raise RuntimeError("CDP socket closed during handshake")
                reply += data
                if len(reply.split(b"\r\n\r\n", 1)[0]) > 16384:
                    raise RuntimeError("CDP handshake header too large")
            header, self.pending = reply.split(b"\r\n\r\n", 1)
            expected = base64.b64encode(hashlib.sha1((key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11").encode()).digest())
            if not header.startswith(b"HTTP/1.1 101 ") or expected.lower() not in header.lower():
                raise RuntimeError("CDP WebSocket handshake rejected: " +
                                   header.split(b"\r\n", 1)[0].decode(errors="replace"))
        except Exception:
            self.socket.close()
            raise
        self.next_id = 0
        self.events = {}

    def read_exact(self, size):
        while len(self.pending) < size:
            data = self.socket.recv(max(4096, size - len(self.pending)))
            if not data:
                raise RuntimeError("CDP socket closed")
            self.pending += data
        result, self.pending = self.pending[:size], self.pending[size:]
        return result

    def receive(self):
        first, second = self.read_exact(2)
        if first & 0x80 == 0:
            raise RuntimeError("Fragmented CDP message")
        length = second & 0x7F
        if length == 126:
            length = struct.unpack("!H", self.read_exact(2))[0]
        elif length == 127:
            length = struct.unpack("!Q", self.read_exact(8))[0]
        if length > 4 * 1024 * 1024:
            raise RuntimeError("CDP message too large")
        body = self.read_exact(length)
        if first & 0xF == 8:
            raise RuntimeError("CDP socket closed")
        if first & 0xF != 1:
            raise RuntimeError("Unexpected CDP message type")
        return json.loads(body)

    def call(self, method, params=None, on_event=None):
        self.next_id += 1
        payload = json.dumps({"id": self.next_id, "method": method, "params": params or {}}).encode()
        mask = os.urandom(4)
        masked = bytes(value ^ mask[index % 4] for index, value in enumerate(payload))
        size = len(payload)
        header = b"\x81" + (bytes([0x80 | size]) if size < 126 else b"\xfe" + struct.pack("!H", size))
        self.socket.sendall(header + mask + masked)
        while True:
            message = self.receive()
            if "method" in message:
                name = message["method"]
                self.events[name] = self.events.get(name, 0) + 1
                if on_event is not None:
                    on_event(message)
            if message.get("id") == self.next_id:
                if "error" in message:
                    raise RuntimeError(message["error"])
                return message.get("result", {})


def finish_trace(websocket):
    """Collect events even when Chromium sends them before the end response."""
    counts = {}
    complete = False

    def collect(message):
        nonlocal complete
        if message.get("method") == "Tracing.dataCollected":
            for event in message.get("params", {}).get("value", []):
                name = event.get("name", "")
                if name in ("Paint", "Layout", "RecalculateStyles", "CompositeLayers"):
                    counts[name] = counts.get(name, 0) + 1
        elif message.get("method") == "Tracing.tracingComplete":
            complete = True

    websocket.call("Tracing.end", on_event=collect)
    deadline = time.monotonic() + 20
    while not complete:
        remaining = deadline - time.monotonic()
        if remaining <= 0:
            raise RuntimeError("CDP trace did not complete")
        websocket.socket.settimeout(remaining)
        collect(websocket.receive())
    return counts


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", type=int, default=9222)
    parser.add_argument("--seconds", type=int, default=15)
    parser.add_argument("--screenshot", type=Path, help="Optional local PNG for visual inspection")
    parser.add_argument("--trace-seconds", type=int, default=0,
                        help="Separate short timeline trace after the metrics window")
    parser.add_argument("--instrument-page", action="store_true",
                        help="Temporarily count render calls and DOM mutations in the built-in Player")
    parser.add_argument("--disable-progress-transition", action="store_true",
                        help="Inject a temporary CSS override for an A/B measurement")
    navigation = parser.add_mutually_exclusive_group()
    navigation.add_argument("--navigate-static", action="store_true",
                            help="Temporarily show a blank local page for the static Chromium comparison")
    navigation.add_argument("--navigate-player", action="store_true",
                            help="Restore the built-in Player page after the static comparison")
    args = parser.parse_args()
    if not 1 <= args.seconds <= 30:
        parser.error("CDP connection is limited to 30 seconds")
    if not 0 <= args.trace_seconds <= 10:
        parser.error("Tracing is limited to 10 seconds")
    with urlopen(f"http://127.0.0.1:{args.port}/json/list", timeout=5) as response:
        targets = json.load(response)
    target = next(item for item in targets if item.get("type") == "page" and
                  item.get("url", "").startswith(("http://127.0.0.1:8080/player", "data:text/html,")))
    # Never print the endpoint or page URL into raw measurement logs.
    url = target["webSocketDebuggerUrl"].replace("ws://localhost:", "ws://127.0.0.1:")
    websocket = WebSocket(url)
    try:
        if args.navigate_static or args.navigate_player:
            destination = ("data:text/html,<html><body style='margin:0;background:black'></body></html>"
                           if args.navigate_static else "http://127.0.0.1:8080/player")
            websocket.call("Page.navigate", {"url": destination})
            print(json.dumps({"navigation": "static" if args.navigate_static else "player"}))
            return
        if args.disable_progress_transition:
            expression = """(() => {
              const progress = document.getElementById('progress');
              if (!progress) return false;
              progress.style.transition = 'none';
              return getComputedStyle(progress).transitionDuration === '0s';
            })()"""
            result = websocket.call("Runtime.evaluate", {"expression": expression,
                                                          "returnByValue": True})
            if not result.get("result", {}).get("value"):
                raise RuntimeError("Progress transition override was not applied")
            print(json.dumps({"temporary_progress_transition": "disabled"}))
        if args.instrument_page:
            expression = """(() => {
              if (typeof window.render !== 'function') return false;
              const oldRender = window.render;
              const counts = { renders: 0, domMutations: 0 };
              const observer = new MutationObserver(records => { counts.domMutations += records.length; });
              observer.observe(document, { subtree: true, childList: true,
                                           attributes: true, characterData: true });
              window.render = function(...args) {
                counts.renders++;
                return oldRender.apply(this, args);
              };
              window.__picdplayerPerf = { counts, stop() {
                observer.disconnect(); window.render = oldRender;
              }};
              return true;
            })()"""
            result = websocket.call("Runtime.evaluate", {"expression": expression,
                                                          "returnByValue": True})
            if not result.get("result", {}).get("value"):
                raise RuntimeError("Player render function not available for instrumentation")
        websocket.call("Performance.enable")
        websocket.call("Network.enable")
        first = {metric["name"]: metric["value"] for metric in websocket.call("Performance.getMetrics")["metrics"]}
        time.sleep(args.seconds)
        second = {metric["name"]: metric["value"] for metric in websocket.call("Performance.getMetrics")["metrics"]}
        counters = ("Frames", "JSEventListeners", "Nodes", "LayoutCount", "RecalcStyleCount", "TaskDuration",
                    "ScriptDuration", "LayoutDuration", "RecalcStyleDuration", "JSHeapUsedSize")
        print(json.dumps({"cdp_connected_seconds": args.seconds, "time_utc": time.time(),
                          "websocket_frames_received": websocket.events.get("Network.webSocketFrameReceived", 0),
                          "metrics": {key: {"start": first.get(key), "end": second.get(key),
                                            "delta": second[key] - first[key]}
                                      for key in counters if key in first and key in second}}))
        if args.instrument_page:
            result = websocket.call("Runtime.evaluate", {"expression": "window.__picdplayerPerf.counts",
                                                          "returnByValue": True})
            print(json.dumps({"page_instrumented_seconds": args.seconds,
                              "counters": result.get("result", {}).get("value", {})}))
            websocket.call("Runtime.evaluate", {"expression": "window.__picdplayerPerf.stop()"})
        if args.screenshot:
            data = websocket.call("Page.captureScreenshot", {"format": "png", "fromSurface": True})["data"]
            args.screenshot.write_bytes(base64.b64decode(data))
        if args.trace_seconds:
            websocket.call("Tracing.start", {"categories": "devtools.timeline",
                                               "transferMode": "ReportEvents"})
            time.sleep(args.trace_seconds)
            counts = finish_trace(websocket)
            print(json.dumps({"trace_connected_seconds": args.trace_seconds,
                              "timeline_event_counts": counts}))
    finally:
        websocket.socket.close()


if __name__ == "__main__":
    main()
