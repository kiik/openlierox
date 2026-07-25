#!/bin/bash
# Build a standalone Windows bundle folder containing openlierox.exe,
# its MinGW runtime DLLs, and the game data from share/gamedir/.
#
# Inputs (env):
#   BUILD_DIR - cmake build directory (default: build-cmake)
#
# Output: distrib/openlierox-<version>-windows/ folder.

set -euo pipefail

cd $(dirname $0)"/../.."

BUILD_DIR="${BUILD_DIR:-build-cmake}"
BINARY="$BUILD_DIR/bin/openlierox.exe"

if [ ! -f "$BINARY" ]; then
    echo "ERROR: $BINARY not found — build the project first" >&2
    exit 1
fi

REPO_ROOT="$(pwd)"

PACKAGE_NAME="openlierox-$(./get_version.sh)-windows"
PACKAGE_DIR="$REPO_ROOT/distrib/$PACKAGE_NAME"
rm -rf "$PACKAGE_DIR"
mkdir -p "$PACKAGE_DIR"

cp "$BINARY" "$PACKAGE_DIR/"

# Game data — everything OLX searches for at ./ at runtime.
mkdir -p "$PACKAGE_DIR/gamedir"
cp -r share/gamedir/. "$PACKAGE_DIR/gamedir"

# Pull in every MinGW runtime DLL the bundle needs.
#
# ntldd prints lines like "libfoo.dll => C:\msys64\mingw64\bin\libfoo.dll (0x...)".
# A single `ntldd -R` on the exe can leave holes: its recursion does not
# reliably descend through every intermediate library (e.g. libgd's own
# libpng/libjpeg/... deps), so the bundle ends up missing DLLs that are only
# reached indirectly. That bundle then runs only when MSYS2's mingw64/bin
# happens to be on PATH -- fine from a dev shell, but from Explorer the loader
# can't find the missing DLL and image loading fails.
#
# Resolve the closure ourselves instead of trusting -R: scan the exe, copy each
# mingw64 DLL, then scan every DLL we copy, until nothing new turns up.
direct_mingw_dlls() {
    ntldd "$1" \
        | awk -F'=>' '/=>/ { print $2 }' \
        | awk '{ print $1 }' \
        | grep -i 'mingw64' || true
}

queue=("$PACKAGE_DIR/openlierox.exe")
while [ "${#queue[@]}" -gt 0 ]; do
    current="${queue[0]}"
    queue=("${queue[@]:1}")
    while read -r dll; do
        [ -n "$dll" ] || continue
        winpath="$(cygpath -u "$dll")"
        base="$(basename "$winpath")"
        if [ ! -f "$PACKAGE_DIR/$base" ]; then
            cp "$winpath" "$PACKAGE_DIR/"
            queue+=("$PACKAGE_DIR/$base")
        fi
    done < <(direct_mingw_dlls "$current")
done

echo ">>> built $PACKAGE_DIR"
