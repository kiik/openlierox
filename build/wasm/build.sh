#!/usr/bin/env bash
# build.sh — produce a redistributable OpenLieroX Wasm bundle.
#
# Wraps build-wasm.sh (release build by default) and stages the
# resulting artefacts into <repo>/distrib/openlierox-wasm/, ready to
# upload to a static web host.
#
# This is a SINGLE-THREADED wasm build: no pthreads, no SharedArrayBuffer,
# and therefore NO cross-origin isolation. The bundle is plain static
# files and runs on any static host (GitHub Pages, Netlify, S3, nginx,
# `python3 -m http.server`, ...) with no special response headers and no
# service-worker shim. A minimal .htaccess is shipped only to set the
# .wasm MIME type on Apache.
#
# Usage: ./build.sh [--debug] [extra args forwarded to build-wasm.sh]

set -euo pipefail

WASM_DIR="$(cd "$(dirname "$0")" && pwd)"
OLX_ROOT="$(cd "$WASM_DIR/../.." && pwd)"
DIST_DIR="$OLX_ROOT/distrib/openlierox-wasm"

BUILD_FLAVOR="--release"
FORWARD_ARGS=()

while [ $# -gt 0 ]; do
    case "$1" in
        --debug)    BUILD_FLAVOR="--debug" ;;
        --release)  BUILD_FLAVOR="--release" ;;
        -h|--help)
            sed -n '2,20p' "$0" | sed 's/^# \?//'
            exit 0 ;;
        *)          FORWARD_ARGS+=("$1") ;;
    esac
    shift
done

# ---------- build ---------------------------------------------------------

"$WASM_DIR/build-wasm.sh" "$BUILD_FLAVOR" "${FORWARD_ARGS[@]}"

OUT_DIR="$WASM_DIR/output"
for f in index.html openlierox.js openlierox.wasm openlierox.data; do
    if [ ! -f "$OUT_DIR/$f" ]; then
        echo "ERROR: expected artefact missing: $OUT_DIR/$f" >&2
        exit 1
    fi
done

# ---------- stage ---------------------------------------------------------

echo "Staging redistributable bundle into $DIST_DIR..."
rm -rf "$DIST_DIR"
mkdir -p "$DIST_DIR"

cp -f "$OUT_DIR/index.html"      "$DIST_DIR/index.html"
cp -f "$OUT_DIR/openlierox.js"   "$DIST_DIR/openlierox.js"
cp -f "$OUT_DIR/openlierox.wasm" "$DIST_DIR/openlierox.wasm"
cp -f "$OUT_DIR/openlierox.data" "$DIST_DIR/openlierox.data"

# Local-testing launcher. serve.py is just a convenience static server
# (adds the .wasm MIME type + no-cache); a plain `python3 -m http.server`
# works too, since this single-threaded build needs no special headers.
# run.command is a double-clickable macOS wrapper.
cp -f "$WASM_DIR/serve.py" "$DIST_DIR/serve.py"
cat > "$DIST_DIR/run.command" <<'EOF'
#!/bin/sh
# Double-click (macOS) to serve this OpenLieroX bundle locally,
# then open it in a browser.
cd "$(dirname "$0")" || exit 1
( sleep 1 && open "http://localhost:8000/" ) &
exec python3 serve.py
EOF
chmod +x "$DIST_DIR/run.command"

# PWA assets: web app manifest + icons referenced by the shell's <head>.
# These make the bundle installable as a standalone web app out of the box
# (the shell's "Install web app" button needs a manifest to get a real
# install prompt). The manifest uses relative start_url/scope, so it works
# wherever the bundle is hosted.
for asset in manifest.webmanifest icon-256.png icon-512.png; do
    if [ ! -f "$WASM_DIR/shell/$asset" ]; then
        echo "ERROR: shell asset missing: $WASM_DIR/shell/$asset" >&2
        exit 1
    fi
    cp -f "$WASM_DIR/shell/$asset" "$DIST_DIR/$asset"
done

# Apache MIME types (the single-threaded build needs no COOP/COEP headers).
cat > "$DIST_DIR/.htaccess" <<'EOF'
AddType application/wasm         .wasm
AddType application/octet-stream .data
AddType application/javascript   .js
EOF

# Machine-readable build metadata. Lets a downstream deployer (e.g. the
# website's update step) read the exact version/commit a bundle came from
# instead of eyeballing the release title, and script a versioned rollout
# (name the target folder, record provenance) without guessing.
OLX_VERSION="$("$OLX_ROOT/get_version.sh")"  # fails the build if undeterminable
OLX_COMMIT="$(git -C "$OLX_ROOT" rev-parse HEAD 2>/dev/null || echo unknown)"
OLX_COMMIT_SHORT="$(git -C "$OLX_ROOT" rev-parse --short HEAD 2>/dev/null || echo unknown)"
OLX_COMMIT_DATE="$(git -C "$OLX_ROOT" show -s --format=%cI HEAD 2>/dev/null || echo unknown)"
OLX_VERSION="$OLX_VERSION" OLX_COMMIT="$OLX_COMMIT" \
OLX_COMMIT_SHORT="$OLX_COMMIT_SHORT" OLX_COMMIT_DATE="$OLX_COMMIT_DATE" \
python3 - "$DIST_DIR/build-info.json" <<'PY'
import json, os, sys
json.dump({
    "name": "openlierox-wasm",
    "version": os.environ["OLX_VERSION"],
    "commit": os.environ["OLX_COMMIT"],
    "commitShort": os.environ["OLX_COMMIT_SHORT"],
    "commitDate": os.environ["OLX_COMMIT_DATE"],
    # The web entry point and the engine artefacts a deployer must publish.
    "entry": "index.html",
    "engineFiles": ["openlierox.js", "openlierox.wasm", "openlierox.data"],
    "files": [
        "index.html", "openlierox.js", "openlierox.wasm", "openlierox.data",
        "manifest.webmanifest", "icon-256.png", "icon-512.png",
        ".htaccess", "serve.py", "run.command",
    ],
}, open(sys.argv[1], "w"), indent=2)
open(sys.argv[1], "a").write("\n")
PY

# ---------- summary -------------------------------------------------------

echo
echo "Bundle ready: $DIST_DIR"
ls -lh "$DIST_DIR"
TOTAL=$(du -sh "$DIST_DIR" | cut -f1)
echo
echo "Total size: $TOTAL"
echo "Upload the contents of $DIST_DIR to any static web host."
echo
echo "This single-threaded build needs no COOP/COEP headers and no service"
echo "worker — plain static hosting works (GitHub Pages, Netlify, S3, nginx)."
echo "The only requirement is that .wasm is served as application/wasm"
echo "(most hosts do this already; .htaccess covers Apache)."
echo
echo "To test the bundle locally, run 'python3 serve.py' inside it"
echo "(or 'python3 -m http.server', or double-click run.command on macOS),"
echo "then open http://localhost:8000/."
echo "Opening index.html as a file:// URL will not work (must be served over HTTP)."
