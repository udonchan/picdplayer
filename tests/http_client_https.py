"""Run the C++ libcurl test against a local HTTPS response fixture."""

import http.server
import pathlib
import ssl
import subprocess
import sys
import threading
import time


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
