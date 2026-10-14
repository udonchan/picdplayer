"""Run the C++ libcurl test against a local HTTPS response fixture."""

import http.server
import pathlib
import ssl
import subprocess
import sys
import threading
import time
import tempfile


class Handler(http.server.BaseHTTPRequestHandler):
    def do_GET(self):
        if self.path == "/slow":
            time.sleep(0.5)
        if self.path == "/redirect-ok":
            self.send_response(302)
            self.send_header("Location", f"https://archive.org:{self.server.server_port}/ok")
            self.send_header("Content-Length", "0")
            self.end_headers()
            return
        if self.path == "/redirect-bad":
            self.send_response(302)
            self.send_header("Location", "https://evil.invalid/cover")
            self.send_header("Content-Length", "0")
            self.end_headers()
            return
        if self.path == "/retry":
            self.send_response(429)
            self.send_header("Retry-After", "4")
            self.send_header("Content-Length", "0")
            self.end_headers()
            return
        body = b"x" * 128 if self.path == "/large" else b'{"images":[]}'
        self.send_response(200)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        try:
            self.wfile.write(body)
        except (BrokenPipeError, ConnectionResetError):
            pass

    def log_message(self, *_args):
        pass


class FanartHandler(Handler):
    def do_GET(self):
        self.server.requests.append((self.path, self.headers.get("api-key"),
                                     self.headers.get("client-key")))
        if self.path.endswith("/redirect"):
            self.send_response(302)
            self.send_header("Location", "https://assets.fanart.tv/fanart/music/leaked.jpg")
        elif self.path.endswith("/first") and (self.server.mode != "asset" or self.path.startswith("/fanart/")):
            if self.server.mode == "concurrent":
                time.sleep(0.3)
            self.send_response(429)
            retry = {"positive": "4", "concurrent": "4", "asset": "4", "invalid": "invalid",
                     "zero": "0", "huge": "9223372036854775807"}.get(self.server.mode)
            if retry is not None:
                self.send_header("Retry-After", retry)
        else:
            self.send_response(200)
        self.send_header("Content-Length", "0")
        self.end_headers()


fixtures = pathlib.Path(__file__).parent / "fixtures"
server = http.server.ThreadingHTTPServer(("127.0.0.1", 0), Handler)
context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
context.load_cert_chain(fixtures / "http-test-cert.pem", fixtures / "http-test-key.pem")
server.socket = context.wrap_socket(server.socket, server_side=True)
thread = threading.Thread(target=server.serve_forever, daemon=True)
thread.start()
try:
    subprocess.run([sys.argv[1], str(server.server_port), str(fixtures / "http-test-cert.pem")],
                   check=True, timeout=10)
finally:
    server.shutdown()
    server.server_close()
    thread.join(timeout=2)

# Public fixture certificate for the existing fixture key, valid until 2036.
# Separate hostnames preserve existing CAA certificate/hostname checks. Port 443
# keeps production URL policy. No openssl executable is needed in the image.
fanart_certificate = """-----BEGIN CERTIFICATE-----
MIIDGDCCAgCgAwIBAgIJAMfzbek+G6OkMA0GCSqGSIb3DQEBCwUAMB8xHTAbBgNV
BAMMFHdlYnNlcnZpY2UuZmFuYXJ0LnR2MB4XDTI2MTAxNDEyMjIyN1oXDTM2MTAx
MTEyMjIyN1owHzEdMBsGA1UEAwwUd2Vic2VydmljZS5mYW5hcnQudHYwggEiMA0G
CSqGSIb3DQEBAQUAA4IBDwAwggEKAoIBAQDRbKq4RyTrrCbFk+UAKIZiumrMwtUX
qXN12Gu5IH9gznQZNPLq7cyDDkUY6bE1LSsBxMOYl48CY4r+nmIOKtc50oOv6O32
gTEMsfiqB9/4ER4vkW+GtBuzHPJDZUnPeSwGfvVH44Z9YTarF0+B4O3NlUrWvCnn
XdojEQbEx6P9b6ZR/Vx0d5TRyNQtVkjue01Zwil58E5V9mxnQnPcN0HoDhzf42aq
p2Az5Io7KlS+LXTBlrPIkIc0nNFiJmjEmwPq+8LJIdz0mdy+79T8wnkDmu8oh1hx
dxZBeltZWR7i4ixJgIBXhKgYp2cCEoBNgX4aaCFfFlt+YkRJGmYGY7apAgMBAAGj
VzBVMEIGA1UdEQQ7MDmCFHdlYnNlcnZpY2UuZmFuYXJ0LnR2ghBhc3NldHMuZmFu
YXJ0LnR2gg9tdXNpY2JyYWluei5vcmcwDwYDVR0TAQH/BAUwAwEB/zANBgkqhkiG
9w0BAQsFAAOCAQEAYHP+Uf7pYbaw52QZm0LaOGirlBMBHU+DRd3Nuw6hp6u97SY4
5GVDmsTLXYqVrpDhlHAZ8w6V6exvLyRNbuPWeldyN786638SobsUZOJ8Q0Aq85Ga
ECD4+A4bzFyV45EtU283Ac+iAPco7bD4ruxWeG36KSdjDFzuEfPKjXZsiMSFMRcU
iQzSc2IrKlMqcdgG6PaSk4GzBw3zI4io+s0Cbav9l08V4cQPyeiZVFw+T9cfoWrh
iob0etJh/jiX8uUxQQo0MM7Qe7d1/l9lOKvwowcQdbOq/4oMaxmxTsGCGRVaU5vu
Un+qHkT9n3RtWd08YT9j52JqaS9G58g5PAzWjw==
-----END CERTIFICATE-----
"""
with tempfile.TemporaryDirectory(prefix="picdplayer-fanart-tls-") as directory:
    cert = pathlib.Path(directory) / "cert.pem"
    cert.write_text(fanart_certificate, encoding="ascii")
    fanart = http.server.ThreadingHTTPServer(("127.0.0.1", 443), FanartHandler)
    context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    context.load_cert_chain(cert, fixtures / "http-test-key.pem")
    fanart.socket = context.wrap_socket(fanart.socket, server_side=True)
    thread = threading.Thread(target=fanart.serve_forever, daemon=True)
    thread.start()
    try:
        for mode in ("positive", "missing", "invalid", "zero", "huge", "concurrent", "asset"):
            fanart.mode = mode
            fanart.requests = []
            subprocess.run([sys.argv[1], "443", str(cert), mode], check=True, timeout=3)
            expected = [
                ("/v3.2/music/redirect", "fixture-project-key", None),
                ("/v3.2/music/first", "fixture-project-key", None),
                ("/v3.2/music/unauthenticated", None, None),
                ("/fanart/music/image.jpg", None, None),
                ("/ok", None, None),
            ] if mode != "asset" else [
                ("/v3.2/music/redirect", "fixture-project-key", None),
                ("/fanart/music/first", None, None),
                ("/v3.2/music/first", "fixture-project-key", None),
                ("/ok", None, None),
            ]
            assert fanart.requests == expected, "unexpected fanart network request or authentication header"
        print("PASS: exact TLS request counts, api-key header only, no query/redirect key leakage")
    finally:
        fanart.shutdown()
        fanart.server_close()
        thread.join(timeout=2)
