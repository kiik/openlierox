# Headless OpenLieroX dedicated server.
#
# Multi-stage: the builder compiles the DEDICATED_ONLY binary with the full
# -dev toolchain; the runtime stage keeps only the binary, the gamedir data,
# the Python 3 control script and the shared libraries it loads.
#
# See docker/README.md for the environment variables and ports.

# ---------------------------------------------------------------------------
# Build stage
# ---------------------------------------------------------------------------
FROM debian:bookworm-slim AS build

# HawkNL and Lua are built in-tree (CMake defaults); libzip is the system one.
# SDL2/SDL2_image/gd/openal/vorbis are still linked in a DEDICATED_ONLY build
# (they are just unused at runtime), so their -dev packages are needed here.
RUN apt-get update && apt-get install -y --no-install-recommends \
        g++ \
        make \
        cmake \
        libsdl2-dev \
        libsdl2-image-dev \
        libgd-dev \
        libopenal-dev \
        libvorbis-dev \
        libcurl4-openssl-dev \
        libxml2-dev \
        libzip-dev \
        libyaml-cpp-dev \
        zlib1g-dev \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /src
COPY . .

# The version is normally derived from git, but .git is excluded from the build
# context (see .dockerignore). Fall back to a VERSION file, which the version
# script honours. Pass the real string with
#   --build-arg OLX_VERSION="$(./get_version_full.sh)"
# (CI does this); otherwise the binary reports this placeholder.
ARG OLX_VERSION=0.0-docker
RUN [ -d .git ] || printf '%s\n' "${OLX_VERSION}" > VERSION

# DEDICATED_ONLY drops the X11/G15 linkage; DEBUG=No is a release build.
# HASBFD=No avoids a runtime binutils dependency (glibc backtrace still works).
# The gamecontrollerdb download is off since a dedicated server has no gamepad.
RUN cmake -DDEBUG=No -DDEDICATED_ONLY=Yes -DHASBFD=No \
        -DOLX_DOWNLOAD_GAMECONTROLLERDB=OFF . \
    && make -j"$(nproc)" openlierox

# ---------------------------------------------------------------------------
# Runtime stage
# ---------------------------------------------------------------------------
FROM debian:bookworm-slim AS runtime

# Runtime shared libraries the binary loads, plus python3 for the control
# script and ca-certificates for outbound HTTPS master-server registration.
# apt pulls the transitive dependencies (SDL, X, codecs, ...) automatically.
RUN apt-get update && apt-get install -y --no-install-recommends \
        python3 \
        ca-certificates \
        libsdl2-2.0-0 \
        libsdl2-image-2.0-0 \
        libgd3 \
        libopenal1 \
        libvorbisfile3 \
        libcurl4 \
        libxml2 \
        libzip4 \
        libyaml-cpp0.7 \
        zlib1g \
    && rm -rf /var/lib/apt/lists/*

# ${BIN}/gamedir is an OLX searchpath, so the binary and gamedir sit together.
COPY --from=build /src/bin/openlierox /opt/openlierox/openlierox
COPY --from=build /src/share/gamedir  /opt/openlierox/gamedir
COPY docker/entrypoint.sh             /opt/openlierox/entrypoint.sh
COPY docker/dedicated_config.overlay.py /opt/openlierox/dedicated_config.overlay.py

# The control script resolves cfg/dedicated_config through OLX's searchpaths;
# ${HOME}/.OpenLieroX comes first, so the rendered overlay shadows the stock
# config there. HOME also holds logs, ranking and players.dat.
ENV HOME=/data \
    OLX_GAMEDIR=/opt/openlierox/gamedir \
    PYTHONUNBUFFERED=1 \
    OLX_PORT=23400
RUN mkdir -p /data && chmod 0777 /data
VOLUME /data

EXPOSE 23400/udp

ENTRYPOINT ["/opt/openlierox/entrypoint.sh"]
