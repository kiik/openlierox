#!/usr/bin/env python3
"""Tiny static server for the OpenLieroX Wasm build.

Serves the bundle on http://localhost:8000 with correct MIME types for
.wasm / .data / .js. This is a SINGLE-THREADED build: it needs no
SharedArrayBuffer and therefore no cross-origin isolation, so any plain
static host works (`python3 -m http.server` is enough) — this script just
adds the wasm MIME type and a no-cache policy for convenient iteration.

Serves build/wasm/output/ when run from the repo,
or the directory it sits in when shipped inside a distributed bundle
(next to index.html).
Set OLX_WASM_ROOT to serve a different directory.

Optional TLS, for testing on a phone over the LAN (some browser features
want a secure context over a bare LAN IP; localhost is exempt). Set
CERTFILE (and optionally KEYFILE) to serve https:// instead. Example:

    CERTFILE=/tmp/olx-tls/cert.pem KEYFILE=/tmp/olx-tls/key.pem \\
        PORT=8443 python3 serve.py
"""
import http.server
import os
import ssl
import sys

_HERE = os.path.dirname(os.path.abspath(__file__))
# Serve the bundle directory.
# Inside a distributed bundle this script sits next to index.html;
# in the repo it sits in build/wasm/ with the artefacts under output/.
# An explicit OLX_WASM_ROOT wins over both.
if os.environ.get("OLX_WASM_ROOT"):
    ROOT = os.environ["OLX_WASM_ROOT"]
elif os.path.isfile(os.path.join(_HERE, "index.html")):
    ROOT = _HERE
else:
    ROOT = os.path.join(_HERE, "output")
PORT = int(os.environ.get("PORT", "8000"))
CERTFILE = os.environ.get("CERTFILE")
KEYFILE = os.environ.get("KEYFILE")  # may be None if cert.pem also holds the key


class Handler(http.server.SimpleHTTPRequestHandler):
    extensions_map = dict(http.server.SimpleHTTPRequestHandler.extensions_map)
    extensions_map.update({
        ".wasm": "application/wasm",
        ".data": "application/octet-stream",
        ".js":   "application/javascript",
        ".html": "text/html",
    })

    def end_headers(self):
        # Aggressive no-cache so phones don't keep serving a stale build while
        # iterating. no-store alone isn't always honoured by mobile browsers;
        # pair it with no-cache/must-revalidate + the legacy Pragma/Expires.
        self.send_header("Cache-Control", "no-store, no-cache, must-revalidate, max-age=0")
        self.send_header("Pragma", "no-cache")
        self.send_header("Expires", "0")
        super().end_headers()


def main():
    os.chdir(ROOT)
    httpd = http.server.ThreadingHTTPServer(("", PORT), Handler)
    scheme = "http"
    if CERTFILE:
        ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
        ctx.load_cert_chain(certfile=CERTFILE, keyfile=KEYFILE)
        httpd.socket = ctx.wrap_socket(httpd.socket, server_side=True)
        scheme = "https"
    print(f"Serving {ROOT} on {scheme}://localhost:{PORT}/")
    httpd.serve_forever()


if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        print("\nStopping.")
        sys.exit(0)
