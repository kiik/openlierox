# OpenLieroX dedicated-server Docker image

A headless OpenLieroX dedicated server, built from the `DEDICATED_ONLY`
CMake configuration. Server settings are supplied declaratively through
environment variables — no config files to edit.

## Build

Build from the repository root (the build context is the whole source tree):

```sh
docker build -f docker/Dockerfile -t openlierox-dedicated .
```

The image is multi-stage: a builder compiles the binary with the full
toolchain, and the runtime stage keeps only the binary, the game data, a
Python 3 runtime for the control script and the shared libraries it loads.

## Run

```sh
docker run -d --name olx \
    -p 23400:23400/udp \
    -e OLX_SERVER_NAME="My OLX Server" \
    openlierox-dedicated
```

The game port **must** be published as UDP. For a public server the port also
has to be reachable from the internet (forward `23400/udp` on your router /
firewall). With `OLX_REGISTER_SERVER=1` (the default) the server registers
with the master server at `server.openlierox.net` over outbound HTTPS, so it
shows up in the in-game server browser; otherwise clients join by entering the
host address directly.

Server state (logs, ranking, `players.dat`) is written under `/data`, which is
a volume — mount it to keep ranking across restarts:

```sh
docker run -d -p 23400:23400/udp -v olx-data:/data openlierox-dedicated
```

## docker compose

For a declarative single-server setup, use the committed compose file. Copy
the example environment, edit it, and bring the stack up:

```sh
cp docker/.env.example docker/.env
# edit docker/.env
docker compose -f docker/docker-compose.yml up -d
```

`docker/.env` holds every `OLX_*` variable (see the table below); the compose
file publishes `OLX_PORT` as UDP and keeps state on the `olx-data` named
volume, so ranking and logs survive `docker compose down && up`. Building from
source happens on first `up` (or `--build`); pass a real version with
`OLX_VERSION=…` in `.env`.

Run several games at once as separate stacks with distinct project names,
env files and ports:

```sh
docker compose -p olx-b --env-file docker/.env.b -f docker/docker-compose.yml up -d
```

## Docker Swarm / Portainer

For a Swarm cluster, deploy `docker/olx.stack.yml` (directly as a Portainer
stack). It runs the same image with a `deploy` block and publishes the UDP
port in **host mode** so the server sees each client's real address. Because
the state volume is node-local, the service is pinned with a placement
constraint — label the target node `docker node update --label-add olx=true
<node-id>` — and Portainer pulls a pushed image, so set `OLX_IMAGE` to a
registry ref (Portainer cannot build from source). The header of that file
documents the prerequisites and stack variables.

## Configuration

| Variable              | Default             | Meaning                                                        |
| --------------------- | ------------------- | -------------------------------------------------------------- |
| `OLX_SERVER_NAME`     | stock default       | Server name shown in the browser.                              |
| `OLX_PORT`            | `23400`             | UDP game port. Publish the same port with `-p`.                |
| `OLX_MAX_PLAYERS`     | `14`                | Maximum players (1–32).                                        |
| `OLX_MIN_PLAYERS`     | `1`                 | Players needed before a round starts.                          |
| `OLX_PRESET`          | stock cycle         | Comma-separated preset cycle, e.g. `Mortars,Random`.           |
| `OLX_REGISTER_SERVER` | `1`                 | `1` lists the server on the master server, `0` keeps it unlisted. |
| `OLX_WELCOME_MESSAGE` | stock default       | Message shown to joining players (`<server>`, `<player>`).     |
| `OLX_ADMIN_PASSWORD`  | none                | If set, a player who types `/login <password>` in chat becomes a server admin. |

Available presets (`OLX_PRESET`) are the directories under
`share/gamedir/scripts/presets/` — e.g. `Mortars`, `Random`, `CTF`,
`HideAndSeek`, `Rifles`, `BeforeDawn`. Admin and player chat commands (`!start`,
`!map`, `!preset`, voting, `!rank`, ...) work as documented by the control
script once you are authorised.

### How it works

The entrypoint copies `dedicated_config.overlay.py` to
`$HOME/.OpenLieroX/cfg/dedicated_config.py`, where it shadows the config
packaged in the gamedir. The overlay loads the stock defaults and then applies
the `OLX_*` variables, so anything you do not set keeps its packaged default.
The server is launched as:

```sh
openlierox -dedicated -exec "script dedicated_control cfg/dedicated_config"
```

## Scope

Desktop clients only. Browser (WASM) clients cannot reach a UDP server; that
needs the (unimplemented) WebRTC transport plus a gateway, tracked separately.
