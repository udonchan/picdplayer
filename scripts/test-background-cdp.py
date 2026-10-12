#!/usr/bin/env python3
"""Synthetic background smoke test in an isolated hidden Chromium CDP target.

Run serve on the Mac, forward its loopback port to the Pi, and forward CDP back
to the Mac. This does not install files or navigate the live kiosk target.
"""

import argparse
import base64
import hashlib
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import importlib.util
import json
from pathlib import Path
import struct
import time
from urllib.parse import urlsplit
from urllib.request import urlopen
import zlib


def png(width=320, height=180):
    def chunk(kind, data):
        return struct.pack("!I", len(data)) + kind + data + struct.pack("!I", zlib.crc32(kind + data))
    rows = b"".join(b"\0" + bytes((40 + y % 120, 90, 140)) * width for y in range(height))
    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack("!2I5B", width, height, 8, 2, 0, 0, 0)) +
            chunk(b"IDAT", zlib.compress(rows)) + chunk(b"IEND", b""))


def snapshot(revision=1, background=None, cover=None):
    return {"revision": revision, "player": {"state": "STOPPED", "track_number": 1, "position_frames": 0,
                                            "track_duration_frames": 7500},
            "disc": {"state": "AUDIO_READY", "title": "Synthetic fixture", "artist": "Generated pixels",
                     "layout": {"session_id": "fixture", "disc_generation": 1, "start_lba": 0,
                                "leadout_lba": 7500, "tracks": [{"number": 1, "start_lba": 0, "end_lba": 7500}]}},
            "tracks": [{"number": 1, "title": "Fixture", "artist": "Generated pixels", "duration_frames": 7500}],
            "enrichment": {"status": "AVAILABLE", "selection": {"metadata_generation": 1, "selected_index": 0}},
            "read": {"session_id": "fixture"}, "artwork": {"background": {"url": background} if background else None,
                                                             "cover": {"url": cover} if cover else None}}


def fixture_server(port, ui):
    images = {"png": ("image/png", png()), "bad": ("image/png", b"not an image")}

    class Handler(BaseHTTPRequestHandler):
        def log_message(self, *_):
            pass

        def reply(self, status, mime="application/json", data=b""):
            self.send_response(status)
            self.send_header("Content-Type", mime)
            self.send_header("Content-Length", str(len(data)))
            self.send_header("Cache-Control", "no-store")
            self.end_headers()
            self.wfile.write(data)

        def do_GET(self):
            path = urlsplit(self.path).path
            files = {"/player": ("player.html", "text/html"), "/player.js": ("player.js", "application/javascript"),
                     "/player.css": ("player.css", "text/css")}
            if path in files:
                name, mime = files[path]
                return self.reply(200, mime, (ui / name).read_bytes())
            if path == "/api/state":
                return self.reply(200, data=json.dumps(snapshot()).encode())
            if path == "/api/presentation/artwork/cover":
                return self.reply(200, "image/png", png(128, 128))
            prefix = "/api/presentation/artwork/background/fixture-"
            if path.startswith(prefix) and path[len(prefix):] in images:
                return self.reply(200, *images[path[len(prefix):]])
            return self.reply(404)

        def do_POST(self):
            if self.path == "/api/ui-boot":
                return self.reply(204)
            if self.path not in ("/fixture/jpeg", "/fixture/webp"):
                return self.reply(404)
            length = int(self.headers.get("Content-Length", "0"))
            if not 0 < length <= 1024 * 1024:
                return self.reply(413)
            origin = self.headers.get("Origin")
            if origin and origin != "http://" + self.headers.get("Host", ""):
                return self.reply(403)
            name = self.path.rsplit("/", 1)[1]
            try:
                value = json.loads(self.rfile.read(length))
                mime = "image/" + name
                prefix = "data:" + mime + ";base64,"
                if not isinstance(value, str) or not value.startswith(prefix):
                    return self.reply(400)
                data = base64.b64decode(value[len(prefix):], validate=True)
                if not data or len(data) > 1024 * 1024:
                    return self.reply(400)
                images[name] = (mime, data)
                return self.reply(204)
            except (ValueError, TypeError):
                return self.reply(400)

    return ThreadingHTTPServer(("127.0.0.1", port), Handler)


def smoke(args):
    origin = f"http://127.0.0.1:{args.fixture_port}"
    cdp = f"http://127.0.0.1:{args.cdp_port}"
    def get(path):
        with urlopen(cdp + path, timeout=5) as response:
            return json.load(response)
    version = get("/json/version")
    if args.isolated_headless and "HeadlessChrome/" not in version.get("User-Agent", ""):
        raise RuntimeError("Visible-target testing requires a separately launched headless browser")
    original = {(item["id"], item["url"]) for item in get("/json/list") if item["type"] == "page"}
    if not original:
        raise RuntimeError("No original kiosk page; refusing an unverified browser")
    spec = importlib.util.spec_from_file_location("kiosk_cdp", Path(__file__).with_name("measure-kiosk-cdp.py"))
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    endpoint = f"ws://127.0.0.1:{args.cdp_port}" + urlsplit(version["webSocketDebuggerUrl"]).path
    started = time.monotonic()
    class BoundedWebSocket(module.WebSocket):
        def receive(self):
            remaining = 27 - (time.monotonic() - started)
            if remaining <= 0:
                raise TimeoutError("CDP connection exceeded its bounded smoke window")
            self.socket.settimeout(min(5, remaining))
            return super().receive()
    browser = BoundedWebSocket(endpoint)
    page = None
    context = None
    errors = []
    report = {"browser": version["Browser"], "hidden_target": not args.isolated_headless,
              "isolated_headless": args.isolated_headless, "screenshots_requested": not args.skip_screenshots,
              "cases": []}
    try:
        context = browser.call("Target.createBrowserContext", {"disposeOnDetach": True})["browserContextId"]
        target = browser.call("Target.createTarget", {"url": "about:blank", "background": True,
                              "hidden": not args.isolated_headless, "browserContextId": context})["targetId"]
        page = BoundedWebSocket(f"ws://127.0.0.1:{args.cdp_port}/devtools/page/{target}")
        def event(message):
            if message.get("method") == "Runtime.exceptionThrown":
                errors.append(message["params"]["exceptionDetails"].get("text", "exception"))
        def evaluate(expression):
            if time.monotonic() - started > 24:
                raise RuntimeError("CDP smoke test exceeded its 24-second work budget")
            result = page.call("Runtime.evaluate", {"expression": expression, "returnByValue": True,
                               "awaitPromise": True}, on_event=event)
            if "exceptionDetails" in result:
                raise RuntimeError(result["exceptionDetails"])
            return result.get("result", {}).get("value")
        page.call("Runtime.enable")
        page.call("Page.enable")
        page.call("Performance.enable")
        # No fake WebSocket traffic is mistaken for daemon integration evidence.
        page.call("Page.addScriptToEvaluateOnNewDocument", {"source":
                  "window.WebSocket = class { close() {} };"})
        page.call("Emulation.setDeviceMetricsOverride", {"width": 1920, "height": 1080,
                  "deviceScaleFactor": 1, "mobile": False})
        navigation = page.call("Page.navigate", {"url": origin + "/player"}, on_event=event)
        if navigation.get("errorText"):
            raise RuntimeError("Fixture navigation failed: " + navigation["errorText"])
        evaluate("new Promise((resolve,reject)=>{const end=Date.now()+4000;function wait(){"
                 "if(typeof render==='function' && document.readyState==='complete')resolve(true);"
                 "else if(Date.now()>end)reject(Error('UI not ready'));else setTimeout(wait,20)}wait()})")
        baseline = page.call("Performance.getMetrics")["metrics"]
        evaluate("""(async()=>{const c=document.createElement('canvas');c.width=320;c.height=180;
          const x=c.getContext('2d');x.fillStyle='#397aa0';x.fillRect(0,0,320,180);
          for(const f of ['jpeg','webp']){const data=c.toDataURL('image/'+f);
            if(!data.startsWith('data:image/'+f+';'))throw Error('encoder unavailable: '+f);
            const r=await fetch('/fixture/'+f,{method:'POST',body:JSON.stringify(data)});
            if(r.status!==204)throw Error('fixture upload failed')}return true})()""")
        cover = "/api/presentation/artwork/cover"
        revision = 2
        def show(name, artist, has_cover=True, generation=1):
            nonlocal revision
            value = snapshot(revision, artist, cover if has_cover else None)
            value["disc"]["layout"]["disc_generation"] = generation
            value["enrichment"]["selection"]["metadata_generation"] = generation
            revision += 1
            result = evaluate("""(async()=>{window.fixtureSnapshot=VALUE;render(window.fixtureSnapshot);await new Promise(r=>setTimeout(r,120));
              const a=document.getElementById('background-artist'),c=document.getElementById('background-cover');
              for(const i of [a,c]){if(i.getAttribute('src'))try{await i.decode()}catch{}}
              await Promise.race([new Promise(requestAnimationFrame),new Promise(r=>setTimeout(r,80))]);
              const p=document.getElementById('player-background');
              return {artist:p.classList.contains('has-artist'),cover:p.classList.contains('has-cover'),
                artistWidth:a.naturalWidth,coverWidth:c.naturalWidth,artistSrc:a.getAttribute('src'),
                horizontalOverflow:document.documentElement.scrollWidth>innerWidth,
                verticalOverflow:document.documentElement.scrollHeight>innerHeight,
                scrollHeight:document.documentElement.scrollHeight,viewportHeight:innerHeight}}
              )()""".replace("VALUE", json.dumps(value)))
            report["cases"].append({"name": name, **result})
            return result
        for name in ("png", "jpeg", "webp"):
            result = show(name, "/api/presentation/artwork/background/fixture-" + name)
            assert result["artist"] and result["artistWidth"] == 320 and not result["horizontalOverflow"] and not result["verticalOverflow"], result
        evaluate("window.retiredBackgroundLoad=document.getElementById('background-artist').onload")
        result = show("decode-fallback", "/api/presentation/artwork/background/fixture-bad")
        assert not result["artist"] and result["cover"] and result["coverWidth"] == 128, result
        result = show("default-fallback", "/api/presentation/artwork/background/fixture-bad", False)
        assert not result["artist"] and not result["cover"], result
        result = show("off", None)
        assert not result["artist"] and result["artistSrc"] is None, result
        assert evaluate("retiredBackgroundLoad();!document.getElementById('player-background').classList.contains('has-artist')")
        result = show("new-generation", "/api/presentation/artwork/background/fixture-png", generation=2)
        assert result["artist"] and not result["horizontalOverflow"] and not result["verticalOverflow"], result
        report["stale_callback_ignored"] = True
        for width, height, name in ((1920, 1080, "desktop"), (640, 720, "narrow")):
            page.call("Emulation.setDeviceMetricsOverride", {"width": width, "height": height,
                      "deviceScaleFactor": 1, "mobile": False})
            result = show(name, "/api/presentation/artwork/background/fixture-png", generation=2)
            assert not result["horizontalOverflow"] and (name == "narrow" or not result["verticalOverflow"]), result
            if not args.skip_screenshots:
                data = page.call("Page.captureScreenshot", {"format": "png", "fromSurface": True})["data"]
                (args.output / ("background-" + name + ".png")).write_bytes(base64.b64decode(data))
        final = {m["name"]: m["value"] for m in page.call("Performance.getMetrics")["metrics"]}
        report["steady_snapshot_mutations"] = evaluate("""(()=>{
          const observer=new MutationObserver(()=>{});
          observer.observe(document,{subtree:true,childList:true,attributes:true,characterData:true});
          const next=structuredClone(fixtureSnapshot);next.revision++;render(next);
          const records=observer.takeRecords();observer.disconnect();
          return records.map(r=>({type:r.type,id:r.target.id||r.target.parentElement?.id||'',
            command:r.target.dataset?.command,attribute:r.attributeName}))})()""")
        assert not report["steady_snapshot_mutations"], report["steady_snapshot_mutations"]
        report["metrics_delta"] = {m["name"]: final[m["name"]] - m["value"] for m in baseline
                                   if m["name"] in ("LayoutCount", "RecalcStyleCount", "TaskDuration", "JSHeapUsedSize")}
        assert not errors, errors
        report["javascript_exceptions"] = errors
        report["connected_seconds"] = round(time.monotonic() - started, 3)
    finally:
        if page:
            page.socket.close()
        try:
            if context:
                browser.call("Target.disposeBrowserContext", {"browserContextId": context})
        finally:
            browser.socket.close()
        remaining = {(item["id"], item["url"]) for item in get("/json/list") if item["type"] == "page"}
        if remaining != original:
            raise RuntimeError("Original pages changed or fixture target was not removed")
    report["original_pages_preserved"] = True
    report["source_sha256"] = {name: hashlib.sha256((args.ui / name).read_bytes()).hexdigest()
                               for name in ("player.html", "player.css", "player.js")}
    (args.output / "background-cdp.json").write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps(report))


def transport_smoke(args):
    """Observe the UI's real HTTP/WS flow; never replace WS or invoke render."""
    cdp = f"http://127.0.0.1:{args.cdp_port}"
    origin = f"http://127.0.0.1:{args.fixture_port}"
    def get(path):
        with urlopen(cdp + path, timeout=5) as response:
            return json.load(response)
    version = get("/json/version")
    original = {(item["id"], item["url"]) for item in get("/json/list") if item["type"] == "page"}
    if not original:
        raise RuntimeError("No original browser page")
    spec = importlib.util.spec_from_file_location("kiosk_cdp", Path(__file__).with_name("measure-kiosk-cdp.py"))
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    started = time.monotonic()
    class BoundedWebSocket(module.WebSocket):
        def receive(self):
            remaining = 27 - (time.monotonic() - started)
            if remaining <= 0:
                raise TimeoutError("Transport CDP smoke exceeded its bounded window")
            self.socket.settimeout(min(5, remaining))
            return super().receive()
    browser = BoundedWebSocket(f"ws://127.0.0.1:{args.cdp_port}" + urlsplit(version["webSocketDebuggerUrl"]).path)
    page = None
    context = None
    frames = []
    errors = []
    event_sockets = set()
    try:
        context = browser.call("Target.createBrowserContext", {"disposeOnDetach": True})["browserContextId"]
        target = browser.call("Target.createTarget", {"url": "about:blank", "hidden": True,
            "background": True, "browserContextId": context})["targetId"]
        page = BoundedWebSocket(f"ws://127.0.0.1:{args.cdp_port}/devtools/page/{target}")
        def event(message):
            method = message.get("method")
            params = message.get("params", {})
            if method == "Runtime.exceptionThrown":
                errors.append(params["exceptionDetails"].get("text", "exception"))
            elif method == "Network.webSocketCreated" and params["url"] == origin.replace("http:", "ws:") + "/api/events":
                event_sockets.add(params["requestId"])
            elif method == "Network.webSocketFrameReceived" and params["requestId"] in event_sockets:
                value = json.loads(params["response"]["payloadData"])
                if len(frames) >= 64:
                    raise RuntimeError("Too many fixture snapshot frames")
                frames.append({"revision": value["revision"], "disc": value["disc"]["state"],
                               "metadata": value["enrichment"]["status"], "background": value["artwork"]["background"]})
        def evaluate(expression):
            result = page.call("Runtime.evaluate", {"expression": expression, "awaitPromise": True,
                               "returnByValue": True}, on_event=event)
            if "exceptionDetails" in result:
                raise RuntimeError(result["exceptionDetails"])
            return result["result"].get("value")
        page.call("Runtime.enable")
        page.call("Page.enable")
        page.call("Network.enable")
        page.call("Emulation.setDeviceMetricsOverride", {"width": 1920, "height": 1080,
                  "deviceScaleFactor": 1, "mobile": False})
        navigation = page.call("Page.navigate", {"url": origin + "/player"}, on_event=event)
        if navigation.get("errorText"):
            raise RuntimeError(navigation["errorText"])
        result = evaluate("""(async()=>{
          const wait=async(predicate)=>{const end=Date.now()+2500;while(!predicate()){
            if(Date.now()>end)throw Error('UI transport wait timeout');await new Promise(r=>setTimeout(r,20))}};
          const check=(v,m)=>{if(!v)throw Error(m)};
          const api=async(path,body)=>{const r=await fetch(path,body===undefined?{}:
            {method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(body)});return r};
          const state=async()=>await(await api('/api/state')).json();
          await wait(()=>document.getElementById('connection')?.dataset.state==='connected');
          const layer=document.getElementById('player-background'),image=document.getElementById('background-artist');
          const initial=await state();check(initial.artwork.background===null,'default OFF image');
          check((await api('/api/artist-background',{enabled:true})).status===204,'ON rejected');
          await wait(()=>layer.classList.contains('has-artist')&&image.naturalWidth===320);
          const first=(await state()).artwork.background.url;check(image.getAttribute('src')===first,'snapshot/image mismatch');
          check((await api(first)).status===200,'ready image missing');
          check((await api('/api/artist-background',{enabled:false})).status===204,'OFF rejected');
          await wait(()=>!layer.classList.contains('has-artist')&&image.getAttribute('src')===null);
          check((await api(first)).status===404,'OFF reference not invalidated');
          check((await api('/api/artist-background',{enabled:true})).status===204,'second ON rejected');
          await wait(()=>layer.classList.contains('has-artist'));
          const ready=await state(),previous=ready.artwork.background.url,s=ready.enrichment.selection;
          const command={session_id:s.session_id,disc_generation:s.disc_generation,metadata_generation:s.metadata_generation,candidate_index:1};
          check((await api('/api/metadata-selection',command)).status===204,'artist selection rejected');
          await wait(()=>layer.classList.contains('has-artist')&&image.getAttribute('src')!==previous);
          check((await api(previous)).status===404,'artist reference not invalidated');
          command.candidate_index=null;check((await api('/api/metadata-selection',command)).status===204,'decline rejected');
          await wait(()=>!layer.classList.contains('has-artist'));
          check((await fetch('/api/eject',{method:'POST'})).status===202,'synthetic eject rejected');
          await wait(()=>document.getElementById('album').textContent==='No disc');
          check((await api('/api/artist-background',{enabled:true})).status===204,'No Disc ON rejected');
          const final=await state();check(final.disc.state==='NO_DISC'&&final.artwork.background===null,'No Disc state');
          return {imageWidth:320,off404:true,artist404:true,decline:true,noDisc:true,connected:document.getElementById('connection').dataset.state,
            horizontalOverflow:document.documentElement.scrollWidth>innerWidth,verticalOverflow:document.documentElement.scrollHeight>innerHeight};
        })()""")
        assert not result["horizontalOverflow"] and not result["verticalOverflow"], result
        assert not errors and len(frames) >= 3 and any(value["background"] for value in frames), (errors, frames)
        report = {"browser": version["Browser"], "real_transport": True, "websocket_stub": False,
                  "direct_render_calls": False, "connected_seconds": round(time.monotonic() - started, 3),
                  "observations": result, "websocket_snapshots": frames, "javascript_exceptions": errors}
    finally:
        if page:
            page.socket.close()
        try:
            if context:
                browser.call("Target.disposeBrowserContext", {"browserContextId": context})
        finally:
            browser.socket.close()
        remaining = {(item["id"], item["url"]) for item in get("/json/list") if item["type"] == "page"}
        if remaining != original:
            raise RuntimeError("Original page/target changed")
    report["original_pages_preserved"] = True
    (args.output / "background-transport-cdp.json").write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps(report))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("mode", choices=("prepare", "serve", "smoke", "transport", "close-headless"))
    parser.add_argument("--fixture-port", type=int, default=18081)
    parser.add_argument("--cdp-port", type=int, default=19222)
    parser.add_argument("--ui", type=Path, default=Path(__file__).resolve().parents[1] / "ui/default")
    parser.add_argument("--output", type=Path)
    parser.add_argument("--skip-screenshots", action="store_true", help="Hidden targets may not produce a compositor surface")
    parser.add_argument("--isolated-headless", action="store_true", help="Allow a non-hidden target only in a separate headless browser")
    args = parser.parse_args()
    if args.mode == "prepare":
        if not args.output or not args.output.is_dir():
            parser.error("--output must be an existing evidence directory")
        asset = args.output / "synthetic.png"
        asset.write_bytes(png())
        print(json.dumps({"fixture_image": str(asset), "sha256": hashlib.sha256(asset.read_bytes()).hexdigest()}))
    elif args.mode == "serve":
        server = fixture_server(args.fixture_port, args.ui)
        print("fixture ready", flush=True)
        try:
            server.serve_forever()
        finally:
            server.server_close()
    elif args.mode == "close-headless":
        with urlopen(f"http://127.0.0.1:{args.cdp_port}/json/version", timeout=5) as response:
            version = json.load(response)
        if "HeadlessChrome/" not in version.get("User-Agent", ""):
            parser.error("Refusing to close a non-headless browser")
        spec = importlib.util.spec_from_file_location("kiosk_cdp", Path(__file__).with_name("measure-kiosk-cdp.py"))
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        endpoint = f"ws://127.0.0.1:{args.cdp_port}" + urlsplit(version["webSocketDebuggerUrl"]).path
        websocket = module.WebSocket(endpoint)
        try:
            websocket.call("Browser.close")
        finally:
            websocket.socket.close()
    else:
        if not args.isolated_headless:
            args.skip_screenshots = True
        if not args.output or not args.output.is_dir():
            parser.error("--output must be an existing evidence directory")
        if args.mode == "transport":
            transport_smoke(args)
        else:
            smoke(args)


if __name__ == "__main__":
    main()
