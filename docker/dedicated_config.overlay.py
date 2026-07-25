# Container config overlay for the OpenLieroX dedicated server.
#
# docker/entrypoint.sh copies this file to $HOME/.OpenLieroX/cfg/dedicated_config.py,
# where it shadows the stock config on OLX's searchpath. It loads the packaged
# defaults (level list, mods, voting, ...) and then applies the OLX_* container
# environment variables on top. See docker/README.md for the variables.
import os

# Load the packaged defaults by absolute path. We cannot "import
# dedicated_config" here because this file *is* that module on the searchpath,
# so importing by name would re-import ourselves. OLX_GAMEDIR is exported by
# the Dockerfile / entrypoint.
_stock = os.path.join(os.environ["OLX_GAMEDIR"], "cfg", "dedicated_config.py")
with open(_stock) as _f:
    exec(compile(_f.read(), _stock, "exec"))


def _env_int(name, default):
    value = os.environ.get(name, "").strip()
    return int(value) if value else default


def _env_str(name):
    return os.environ.get(name, "").strip()


# --- Control-script settings (not OLX options) -----------------------------
SERVER_PORT = _env_int("OLX_PORT", SERVER_PORT)
MIN_PLAYERS = _env_int("OLX_MIN_PLAYERS", MIN_PLAYERS)

_preset = _env_str("OLX_PRESET")
if _preset:
    PRESETS = [p.strip() for p in _preset.split(",") if p.strip()]

# --- OLX options pushed through GLOBAL_SETTINGS -----------------------------
# The handler applies cfg.GLOBAL_SETTINGS after its own defaults, so anything
# set here wins.
GLOBAL_SETTINGS["GameOptions.Network.Port"] = SERVER_PORT
GLOBAL_SETTINGS["GameOptions.Server.MaxPlayers"] = _env_int("OLX_MAX_PLAYERS", 14)
GLOBAL_SETTINGS["GameOptions.Network.RegisterServer"] = _env_int("OLX_REGISTER_SERVER", 1)

if _env_str("OLX_SERVER_NAME"):
    GLOBAL_SETTINGS["GameOptions.Network.ServerName"] = os.environ["OLX_SERVER_NAME"]
if _env_str("OLX_WELCOME_MESSAGE"):
    GLOBAL_SETTINGS["GameOptions.Network.WelcomeMessage"] = os.environ["OLX_WELCOME_MESSAGE"]
# The admin password is the server's /login password: a player who types
# "/login <password>" in chat is authorised as a dedicated-server admin.
if _env_str("OLX_ADMIN_PASSWORD"):
    GLOBAL_SETTINGS["GameOptions.Network.Password"] = os.environ["OLX_ADMIN_PASSWORD"]
