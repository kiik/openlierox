# OpenLieroX in the browser: content, persistence and download cost

Why the web build shows four mods and five levels when a desktop install
shows dozens, what it would take to fix that, and how to keep the first
visit cheap. A sibling to [WASM-PORT.md](WASM-PORT.md) (what the port is)
and [WASM-NETWORKING.md](WASM-NETWORKING.md) (how it reaches other
players).

Like the networking document, this is a design record. Numbers were
measured and `file:line` references verified against the tree at the
time of writing; where something is unmeasured it says so.

## Status

| Area | State |
|---|---|
| Persistent user data | **Missing.** No IDBFS mount exists; every write is lost on reload |
| Content shipped | A curated stage: 2392 files, 19.5 MB packed. Full `share/gamedir` is 8083 files / 136 MB |
| HTTP from the browser | **Broken**, in two independent ways — see [HTTP](#http-is-broken-in-two-ways) |
| Download from local disk | **Decided** — the first content feature; needs no server |
| Server-side catalogue | **Decided in shape**, blocked on HTTP |
| Bundle download cost | **Unmeasured for release builds.** Measure before optimizing |

## The starting position

The menus list content by scanning the filesystem and validating each
candidate by opening it: `Menu_FillLevelList` →
`FindFiles(filler, "levels")` → `infoForLevel` → `MapLoad::open`
([src/client/DeprecatedGUI/MenuSystem.cpp:1051-1078](../../src/client/DeprecatedGUI/MenuSystem.cpp#L1051)),
and `Menu_Local_FillModList` → `FindFiles(adder, "", FM_DIR)` →
`infoForMod` → `CGameScript::CheckFile`
([src/client/DeprecatedGUI/Menu_Local.cpp:912-922](../../src/client/DeprecatedGUI/Menu_Local.cpp#L912)).
Anything that is not on disk in a valid form does not appear. That is the
whole of the "short list" bug: the lists are telling the truth about a
deliberately slimmed preload
([build/wasm/build-wasm.sh:100-143](build-wasm.sh#L100)).

Three properties of that code decide everything below.

- **Selection cannot hold uninstalled content.** `LevelInfo::fromString`
  and `ModInfo::fromString`
  ([src/game/Level.cpp:66](../../src/game/Level.cpp#L66),
  [src/game/Mod.cpp:47](../../src/game/Mod.cpp#L47)) re-run
  `infoForLevel` / `infoForMod` and return `valid = false` for absent
  content, so `gameSettings[FT_Map]` cannot even represent "the map the
  player picked but has not downloaded". Install-on-select is therefore
  not a menu hook; it needs the content on disk *before* the selection is
  committed.
- **Nothing can be installed until writes persist.** Every install path
  goes through `OpenGameFile(..., "wb")` → `GetWriteFullFileName` →
  `GetFirstSearchPath()`
  ([src/common/FindFile.cpp:620-644](../../src/common/FindFile.cpp#L620)),
  and on Emscripten the first search path is `/gamedir` on MEMFS
  ([:491-496](../../src/common/FindFile.cpp#L491)). MEMFS is writable, so
  nothing fails — the files simply vanish on reload.
- **The comment there is false.** `FindFile.cpp:494` states that
  persistent user data "is mounted on IDBFS at /home/web_user". No mount
  exists anywhere in the tree: there is no `FS.mount`, no `syncfs`, and
  no `preRun` in [shell/shell.html](shell/shell.html). Fix the comment in
  the same change that makes it true.

## Persistence, done properly

The tempting one-liner — "put the writable path first in
`basesearchpaths`" — is a trap. `InitSearchPaths` reads `SearchPath1..N`
out of `cfg/options.cfg` and pushes them ahead of the base paths
([src/client/Options.cpp:103-114](../../src/client/Options.cpp#L103)),
and options saving writes that list back out
([:593](../../src/client/Options.cpp#L593)). The moment persistence
starts working, the first run's ordering is frozen in every returning
player's config, and a later build that reorders the base paths is a
no-op for them. Reordering also makes the writable directory shadow
`/gamedir` for reads, which matters because the extraction loop skips
files that already exist
([src/client/CClient.cpp:629-631](../../src/client/CClient.cpp#L629)) —
so a half-finished install becomes permanently unrepairable.

So:

- Mount IDBFS at the user directory in `Module.preRun`, and gate `main()`
  on the populate `FS.syncfs(true)` completing (`addRunDependency`), not
  on hope. The first write happens very early — `main.cpp` opens the log
  file immediately after `GameOptions::Init()`.
- Flush explicitly with `FS.syncfs(false)` at known checkpoints: options
  save, and after any content install. `-sEXIT_RUNTIME=0` plus a closed
  tab means shutdown is not a reliable flush point.
- Choose the write directory under `__EMSCRIPTEN__` **without** depending
  on search-path order, so nothing gets pinned into a user's config.
- Keep logs on MEMFS. One log file per session, in IndexedDB, forever, is
  not what anybody wants.

## Getting content in

### Local import first

Let the player hand the game a mod or a level from their own disk: drag
it onto the canvas, or pick it with a file input. The bytes go into the
persistent directory, through the same installer the network download
path uses, and the existing menu scan then finds them with no engine
change at all.

This is first on purpose:

- **No server, no HTTP, no CORS, no hosting bill**, so it is unblocked by
  everything else in this document.
- **No redistribution question.** Shipping a catalogue of community mods
  means hosting other people's work; a player opening their own files
  does not.
- It works in every browser. Drag-and-drop and `<input type="file">`
  (including `webkitdirectory` for a whole mod folder) are universal.
  The File System Access API — `showDirectoryPicker`, persistent
  directory handles — is *not*, and should not be the primary path; it is
  worth adding later only as a convenience where present.
- The inverse direction is nearly free and closes the loop for the
  in-game map editor: read the file out of the VFS, hand it to the player
  as a download. A map made in the browser can then be shared, or
  imported by someone else through the path above.

What it needs from the code: the archive-extraction loop currently welded
into `CClient::FinishModDownloads`
([src/client/CClient.cpp:625-643](../../src/client/CClient.cpp#L625))
pulled out into a reusable function taking archive, destination and
allowed prefix. Two of its assumptions have to go: it bails unless the
archive contains `<modname>/script.lgs`
([:616](../../src/client/CClient.cpp#L616)), which excludes Gusanos mods
that have `mod.cfg` and no script; and it silently skips existing files,
which is how a truncated install becomes permanent. Keep the `..`
path-traversal check — an imported archive is untrusted input.

### A server-side catalogue second

For discovery — "show me maps I do not have yet" — a small index served
next to the bundle, fetched over HTTP, installed through the same
extractor. Shape:

- **Per-mod archives, and per-*directory* level archives.** Not
  per-`.lxl`: those are already compressed (`747.lxl` gzips from
  1430242 to 1427359 bytes, 0.2%) and the existing map download path
  installs them as plain files with no unzip at all
  ([src/client/CClient.cpp:492](../../src/client/CClient.cpp#L492),
  [:560-593](../../src/client/CClient.cpp#L560)). Directory-shaped levels
  are real, though — `share/gamedir/levels` holds 79 `.lxl` files **and
  61 directories** (Gusanos maps with `config.cfg`, `level.png`,
  `material.png`) — and the current download path cannot install those.
- **Same origin as the bundle.** The client is published to GitHub Pages;
  serving the catalogue from that same site means no CORS negotiation and
  the browser HTTP cache does the caching for free. The existing mirror
  list in `cfg/downloadservers.txt` points at `openlierox.net` and
  SourceForge, neither of which sends CORS headers, so it is not usable
  from a browser as-is.
- **Scope it to content already in `share/gamedir`** — that is in-repo
  and already licensed. Third-party content is what local import is for.

### Content over the game channel, free

`CUdpFileDownloader`
([include/FileDownload.h:176](../../include/FileDownload.h#L176)) already
transfers a missing map or mod from the server being joined, over the
game channel. No HTTP, no CORS, no catalogue. It covers joining rather
than browsing, and it needs the transport from
[WASM-NETWORKING.md](WASM-NETWORKING.md) plus persistent writes — but it
needs no new code, only verification that it completes under the
per-frame cooperative pump.

## HTTP is broken in two ways

Both have to be fixed before the catalogue or the server list can work,
and the second one is the surprise.

1. **Sockets are WebSockets.** libcurl is built and linked into the wasm
   target ([CMakeLists.txt:138-166](CMakeLists.txt#L138)), but
   Emscripten turns `connect()` into a WebSocket open — the engine
   already documents the symptom
   ([src/server/CServer.cpp:272-277](../../src/server/CServer.cpp#L272)).
   So curl cannot reach an HTTP origin; it can only reach a WebSocket
   relay. The comment at `CMakeLists.txt:133-136`, which claims the
   socket transport is the right approach here, is wrong and should be
   corrected.
2. **It would block the frame even if it could connect.** `CHttp` hands
   `CurlThread` to `threadPool->start`
   ([src/common/HTTP.cpp:277](../../src/common/HTTP.cpp#L277)), and on
   Emscripten `ThreadPool::start` defers the action to a main-thread
   queue drained once per frame
   ([src/common/ThreadPool.cpp:186-196](../../src/common/ThreadPool.cpp#L186)).
   The blocking `curl_easy_perform` therefore runs *on the browser main
   thread*, where `poll` and `select` timeouts are ignored and nothing
   yields, for up to the 10 s connect timeout. A WebSocket relay in front
   of the origin would not help: the handshake completes only after
   control returns to JS, which never happens inside
   `curl_easy_perform`.

The fix is an `emscripten_fetch` backend for `CHttp` behind
`__EMSCRIPTEN__`. Its async callback mode needs no pthreads (only the
synchronous mode does) and is orthogonal to ASYNCIFY, and `CHttp`'s
public API is already poll-shaped — `RequestData` / `ProcessRequest` /
`GetData` / `CancelProcessing`
([include/HTTP.h:148](../../include/HTTP.h#L148)) — so it maps onto fetch
callbacks directly. Three things do not survive the swap and should be
dropped or reimplemented rather than faked: proxy support
(`CURLOPT_PROXY`), multipart form POST (`curl_formadd`), and the
`curl_easy_getinfo` accessors (`GetMimeType`, `GetDownloadSpeed`).

Note also that `CHttpDownloadManager` never starts its manager thread on
Emscripten — downloads are inert by construction
([src/common/FileDownload.cpp:365-372](../../src/common/FileDownload.cpp#L365)).
Fixing `CHttp` alone downloads nothing; `ProcessDownloads` has to be
called from the per-frame pump.

## Download cost

**Measure first.** The local debug bundle is 123 MB of wasm plus 19.5 MB
of data, but CI ships `--release`
([build.sh:23](build.sh#L23)) and that size has not been recorded
anywhere. Optimizing against the debug number would be optimizing
against a number nobody downloads. Record release `.wasm`, `.js` and
`.data` sizes, and the transfer sizes actually observed from Pages, then
decide.

Once measured, in rough order of value per line of code:

- **Transfer compression at the hosting layer.** wasm compresses very
  well; whether GitHub Pages already serves it compressed needs
  checking, and a self-hosted container can do brotli deliberately. This
  is configuration, not code, and it is the largest single lever.
- **`--use-preload-cache`.** `file_packager` can store the data package
  in IndexedDB with a version check, so repeat visits skip the download
  entirely ([tools/file_packager.py](https://github.com/emscripten-core/emscripten/blob/main/tools/file_packager.py), `--use-preload-cache`,
  also `--separate-metadata` and `--lz4`). Available in the pinned emsdk
  3.1.74. Cheap to try; costs a second copy of the data in IndexedDB, and
  the version check invalidates on every rebuild, which is noise during
  development.
- **A smaller stage.** Once local import and the catalogue exist, the
  preload only has to carry what a first-time player needs to start a
  game — the shared trees (`data`, `themes`, `skins`) plus one mod and a
  couple of levels. Everything else becomes opt-in.
- **A service worker** caching the bundle for instant repeat loads and
  offline play. Straightforward, but pointless before the release size is
  known and the wrong thing to do while a stale cache can serve a broken
  build — needs a versioned cache and an update path.
- **Rejected: a lazy virtual filesystem.** `FS.createLazyFile` uses
  synchronous XHR on the main thread, which is deprecated and freezes the
  tab; filling the level list alone would issue 154 sequential blocking
  reads, because listing opens every candidate. The WasmFS fetch backend
  wants SharedArrayBuffer and worker proxying, which contradicts the
  single-threaded, no-cross-origin-isolation design that lets this build
  be hosted anywhere. And unlike the archive approach, it has to be
  correct across every read path in the engine — `ifstream`, `SDL_RWops`,
  libgd, libxml2, plus existence and size checks.

## Order of work

1. Persistence (IDBFS mount, write-directory choice, flushes). Nothing
   else is possible first.
2. Local import and export, reusing the extracted installer. First
   visible win; no server involved.
3. Measure the release bundle; apply hosting-layer compression and
   `--use-preload-cache` if they pay.
4. `emscripten_fetch` backend for `CHttp`, plus the per-frame
   `ProcessDownloads` pump.
5. The catalogue, on the bundle's own origin.
6. Verify `CUdpFileDownloader` once a real transport exists.
