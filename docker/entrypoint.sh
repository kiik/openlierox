#!/bin/sh
# Entrypoint for the headless OpenLieroX dedicated server.
#
# Renders the server config from OLX_* environment variables (see
# docker/README.md) and launches the dedicated server. The control script
# resolves "cfg/dedicated_config" through OLX's searchpaths, and
# ${HOME}/.OpenLieroX is searched first, so we drop the overlay there to
# shadow the stock config packaged in the gamedir.
set -e

CFG_DIR="$HOME/.OpenLieroX/cfg"
mkdir -p "$CFG_DIR"

# The overlay reads OLX_* at import time; it only has to be in place.
cp /opt/openlierox/dedicated_config.overlay.py "$CFG_DIR/dedicated_config.py"

echo "OpenLieroX dedicated server"
echo "  name:     ${OLX_SERVER_NAME:-<stock default>}"
echo "  port:     ${OLX_PORT} (publish with -p ${OLX_PORT}:${OLX_PORT}/udp)"
echo "  register: ${OLX_REGISTER_SERVER:-1} (1 = listed on the master server)"
echo "  gamedir:  ${OLX_GAMEDIR}"

# ${BIN} (the binary's own directory) resolves the packaged gamedir via the
# ${BIN}/gamedir searchpath, so no working directory juggling is needed.
exec /opt/openlierox/openlierox -dedicated \
    -exec "script dedicated_control cfg/dedicated_config"
