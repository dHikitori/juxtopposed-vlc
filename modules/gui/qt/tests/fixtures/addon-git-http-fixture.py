#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Serve test repositories through the installed Git smart-HTTP backend."""
import http.server
import os
import pathlib
import ssl
import subprocess
import sys
import urllib.parse

root = pathlib.Path(sys.argv[1]).resolve()
certificate, private_key, git = sys.argv[2:5]


class Handler(http.server.BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.0"

    def log_message(self, *_):
        pass

    def do_GET(self):
        self.run_backend()

    def do_POST(self):
        self.run_backend()

    def run_backend(self):
        parsed = urllib.parse.urlsplit(self.path)
        if parsed.path not in ("/repo.git/info/refs", "/repo.git/git-upload-pack"):
            self.send_error(404)
            return
        try:
            size = int(self.headers.get("Content-Length", "0"))
        except ValueError:
            self.send_error(400)
            return
        if size < 0 or size > 1024 * 1024 or self.headers.get("Transfer-Encoding"):
            self.send_error(413)
            return
        env = {"PATH": os.environ.get("PATH", ""), "LC_ALL": "C",
               "GIT_CONFIG_NOSYSTEM": "1", "GIT_CONFIG_GLOBAL": os.devnull,
               "GIT_PROJECT_ROOT": str(root), "GIT_HTTP_EXPORT_ALL": "1",
               "PATH_INFO": parsed.path, "QUERY_STRING": parsed.query,
               "REQUEST_METHOD": self.command, "CONTENT_TYPE": self.headers.get("Content-Type", ""),
               "CONTENT_LENGTH": str(size), "REMOTE_ADDR": "127.0.0.1"}
        protocol = self.headers.get("Git-Protocol", "")
        if protocol == "version=2":
            env["GIT_PROTOCOL"] = protocol
        try:
            result = subprocess.run([git, "http-backend"], input=self.rfile.read(size),
                                    env=env, stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=15)
        except (OSError, subprocess.TimeoutExpired):
            self.send_error(502)
            return
        output = result.stdout
        separator = b"\r\n\r\n" if b"\r\n\r\n" in output else b"\n\n"
        if result.returncode or separator not in output or len(output) > 64 * 1024 * 1024:
            self.send_error(502)
            return
        header, body = output.split(separator, 1)
        headers = []
        status = 200
        for line in header.replace(b"\r", b"").split(b"\n"):
            name, value = line.decode("ascii").split(":", 1)
            if name.lower() == "status":
                status = int(value.strip().split()[0])
            else:
                headers.append((name, value.strip()))
        self.send_response(status)
        for name, value in headers:
            self.send_header(name, value)
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)


server = http.server.ThreadingHTTPServer(("127.0.0.1", 0), Handler)
context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
context.minimum_version = ssl.TLSVersion.TLSv1_2
context.load_cert_chain(certificate, private_key)
server.socket = context.wrap_socket(server.socket, server_side=True)
print(server.server_port, flush=True)
server.serve_forever()
