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
| Content shipped | A curated stage: 2396 files, 26 MB on disk, 19.5 MB packed. Full `share/gamedir` is 8083 files / 136 MB |
| HTTP from the browser | **Broken**, in two independent ways — see [HTTP](#http-is-broken-in-two-ways) |
| Download from local disk | **Decided** — the first content feature; needs no server |
| Server-side catalogue | **Decided in shape**, blocked on HTTP |
| Bundle download cost | **Measured.** ~19.2 MB compressed on first visit, 85% of it game data — see [Download cost](#download-cost) |
| Repeat-visit cost | **85% removed.** `--use-preload-cache` keeps the package in IndexedDB, verified across two browser sessions. The remaining ~2 MB of wasm and JS still refetches, because Pages sends `max-age=600` and allows no header configuration |
| Community layer | **Proposed, undecided** — see [A community layer](#a-community-layer) |

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
- **Any GitHub Pages origin will do — same-origin is not required.**
  Pages returns `access-control-allow-origin: *` on JSON *and* on binary
  assets (verified against `openlierox.net/web-demo/`), so the catalogue
  can live in its own repository and still be fetched from the demo. An
  earlier draft of this document claimed the catalogue had to be
  same-origin; that was generalized from the wrong evidence. What is
  genuinely unusable is the existing mirror list in
  `cfg/downloadservers.txt` — and not because of CORS, but because both
  entries are simply dead: the SourceForge SVN URL returns 404, and
  `openlierox.net/file_ignore_case.php` returns 404 now that the site is
  static Pages. In-game HTTP content download is therefore broken on
  *every* platform, not only in the browser.
- **Integrity, not trust.** A catalogue entry carries a `sha256`, and the
  client verifies before installing. Cheap, and it is the difference
  between a distribution channel and a place a mistake becomes permanent.
- **Start with content already in `share/gamedir`** — in-repo and already
  licensed — then widen through the community layer below. Third-party
  content is also what local import is for.

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

### Measured

The 123 MB figure that framed earlier discussion is a debug artifact and
nobody downloads it. `llvm-strip --strip-debug` takes the local debug
`openlierox.wasm` from 123465852 to 61998477 bytes, so half of it is
DWARF, and the rest is unoptimized `-O0` code.

What a visitor actually fetches, from a local `--release` build at
`b87872b9b`, with gzip sizes as the wire cost:

| Asset | Raw | Over the wire (gzip) | Share of transfer |
|---|---:|---:|---:|
| `openlierox.data` | 19529830 | 16358743 | 85% |
| `openlierox.wasm` | 9702087 | 2778740 | 14% |
| `openlierox.js` | 499266 | 107118 | 1% |
| **First visit** | **~29.7 MB** | **~19.2 MB** | |

The live release channel is a useful cross-check and a warning. At
`/web-demo/release/20260717.2/` (commit `0a20c6d0a`) the wasm is 6023670
raw / 1934435 gzipped — 3.7 MB smaller. That commit predates
`21eb388cf`, which moved the build to single-threaded plain static
hosting, so the difference is the price of `-sASYNCIFY=1`: whole-module
instrumentation, about 3.7 MB raw and 0.85 MB compressed. That is what
buys hosting anywhere with no COOP/COEP, and it is the size argument for
finishing the JSPI work (#3), which replaces the instrumentation with a
native stack switch.

Three conclusions follow, and two of them contradict the first draft of
this section.

1. **The code is not the problem.** Even with ASYNCIFY the wasm is 2.8 MB
   compressed — a fortieth of the debug binary, and a seventh of the
   transfer. `-O3` already reaches the link step (verified in
   `output/build/CMakeFiles/openlierox.dir/link.txt`), so `wasm-opt`
   runs. There is no easy win left here. `-sASSERTIONS=1` and
   `-sSTACK_OVERFLOW_CHECK=2` do stay on in release, which also turns on
   `asyncify-asserts`; that is a deliberate safety net for the ASYNCIFY
   work, and it is not what makes the page slow.
2. **Hosting-layer compression is already done, not a lever.** GitHub
   Pages returns `content-encoding: gzip` for `application/wasm` and for
   the `.data` today. Brotli would still help — but Pages does not offer
   it, and the `.data` is mostly PNG and OGG, which are already
   compressed, so the remaining headroom is small. This was previously
   written down as "the largest single lever"; it is not.
3. **`cache-control: max-age=600` is the real problem.** Ten minutes.
   The assets sit at immutable versioned paths, so they could be cached
   for a year — but GitHub Pages does not allow header configuration, so
   the only way to get durable caching is client-side.

### Measures, in order of value per line of code

- **`--use-preload-cache`, one flag. Done and measured**
  ([CMakeLists.txt:323](CMakeLists.txt#L323)). `file_packager` stores the
  package in IndexedDB keyed by `sha256(data)`
  ([file_packager.py:784](https://github.com/emscripten-core/emscripten/blob/main/tools/file_packager.py)),
  and `emcc` forwards the flag straight through
  (`emcc.py:1331` → `link.py:3056`), so it is one line in
  `target_link_options`. It removes 16.4 MB — 85% of the transfer — from
  every repeat visit. It costs 4 KB of JS.

  Verified by running it, not by reading the flag: two loads of the local
  bundle in headless Chromium sharing one profile, counted at the server.
  The first requests `openlierox.data`; the second does not, and still
  reaches `/gamedir` and loads the touch-control layout out of the package,
  so it is reading cached content rather than merely skipping a fetch. The
  embedded key (`sha256-16c8ebb0…`) matches `sha256sum` of the package
  exactly. Rebuilding with one file added to the stage changes both, and the
  next load refetches — so a returning player cannot be served content from
  a build they are no longer running.

  Two caveats the test exposed, neither a reason not to ship it:

  - **The key is version-scoped, and nothing evicts.** The cache key runs
    through `Module.locateFile`, which the channel shell defines as
    `ENGINE_DIR + p` — that is `/web-demo/<channel>/<version>/`. So the
    common case works (a player returns, no new release, no download), but
    every release a player visits leaves its own ~19 MB entry behind
    forever. Deleting the other keys in the store on startup is a few lines
    and belongs with this work; browser quota eviction is not a plan.
  - **The preload stage is not a CMake dependency.** Editing staged content
    and rebuilding does *not* repackage — the hash was unchanged until the
    link output was deleted by hand. That is a pre-existing build wrinkle,
    not caused by this flag, but it means a data-only change can ship the
    previous `.data`. Worth fixing separately.
- **A service worker for the wasm and JS.** This is not new
  infrastructure: the live site already registers one
  (`/web-demo/coi-serviceworker.js`) to inject COOP/COEP headers, which
  this build no longer needs — it is single-threaded, with no
  SharedArrayBuffer and no cross-origin-isolation requirement, so that
  worker is a fetch interceptor doing nothing. Repurposing it as a
  cache-first store for the versioned engine paths removes the remaining
  2 MB from repeat visits, and gets offline play for free. Because the
  paths are versioned, a stale cache cannot serve a mismatched build.
- **A smaller boot stage.** The only lever that improves the *first*
  visit. The stage is 26 MB across 2396 files
  ([build-wasm.sh:100-145](build-wasm.sh#L100)), and about half of it is
  not needed to reach the menu:

  | Staged | Size | Why it is there | Deferrable? |
  |---|---:|---|---|
  | `promode` | 5.3 MB | Introduction campaign's later levels | Yes — fetch with the campaign |
  | `levels/747.lxl`, `Base Fight.lxl` | 2.5 MB | Two of five sample maps | Yes — the comment only requires `levels/` to *exist* |
  | `data/teeworlds` | 2.4 MB | `MapLoader_Teeworlds.cpp:1092` reads `data/teeworlds/mapres/*.png` | Yes — **no Teeworlds map is staged at all**, so this is dead weight today |
  | `MW 1.0` | 1.3 MB | Introduction campaign | Yes |
  | `GeoIP.dat` | 1.0 MB | `main.cpp:310` loads it unconditionally at boot | Yes — and see below |
  | `data/flags` | 1.0 MB | Country flags for the internet server list | Yes |

  That is ~13.5 MB of 26 MB, which roughly halves the first visit. The
  GeoIP and flag pair is worth singling out: it exists to show a
  country per server row, and behind a relay every browser player shares
  the gateway's address anyway — the same shared-identity problem the ban
  list has — so in P0 it is 2 MB spent on information that is wrong.
- **Report wasm download progress.** The shell parses Emscripten's
  `Downloading data... (x/y)` for the `.data`
  ([shell/shell.html:207-217](shell/shell.html#L207)) but shows nothing
  for the `.wasm`, which is the *first* thing a visitor waits on. Cheap,
  and it changes perceived wait more than a megabyte does.
- **Rejected: `--lz4`.** It compresses the package at build time, which
  then defeats the server's gzip; lz4's ratio is worse than gzip's, so
  transfer goes up. It buys lower peak memory during load, not a faster
  download.
- **Rejected: a lazy virtual filesystem.** `FS.createLazyFile` uses
  synchronous XHR on the main thread, which is deprecated and freezes the
  tab; filling the level list alone would issue 154 sequential blocking
  reads, because listing opens every candidate. ASYNCIFY does not rescue
  this — it can unwind an arbitrary call, but stdio has no yield point to
  unwind *from*, so every read path would need instrumenting. The WasmFS
  fetch backend wants SharedArrayBuffer and worker proxying, which
  contradicts the single-threaded, no-cross-origin-isolation design that
  lets this build be hosted anywhere. Fetching at explicit, already
  asynchronous points — the menu, joining a server — gets the same
  benefit with none of that, and is what local import and the catalogue
  already do.

### Why growing the library does not grow the wait

The whole point of the measurement above is that it decouples the two.
Once content arrives on demand, the boot stage stops being "the game's
content" and becomes "the smallest thing that reaches a playable menu" —
a fixed cost that does not move when the catalogue goes from 20 items to
2000. The library can then be arbitrarily large, because a player only
ever pays for what they install, cached durably in IDBFS. That inverts
today's arrangement, where every visitor downloads `promode` and two
1 MB sample maps whether or not they play them.

## A community layer

If players browse and install content in-app, the interesting question
stops being bundle size and becomes: who publishes, where does it live,
what is the metadata, and who is accountable for it. Worth settling
before any code, because the schema is the expensive part to change once
content exists.

### What the engine can express today

Very little. `ModInfo` carries `name`, `path` and `typeShort` and nothing
else ([src/game/Mod.h:17-31](../../src/game/Mod.h#L17)) — no version, no
author, no checksum, no dependencies. Gusanos mods have a bare integer
version file (`promode/promode.ver` contains `130`); LieroX `script.lgs`
mods have no version at all. Any community layer has to add that
metadata alongside the content, because the engine's own model cannot
hold it.

The distribution side is worse than "not implemented" — it is implemented
and dead. Both entries in `cfg/downloadservers.txt` 404 (see above), and
`CHttpDownloadManager` never starts its worker on Emscripten. So there is
no working content-distribution channel on *any* platform to preserve
compatibility with. That is unusually free ground.

### Options

- **Steam Workshop.** Scrutinized and rejected as infrastructure.
  Workshop items are retrievable only through the Steam client, so a
  browser build can never reach them — which defeats the purpose here.
  The Steamworks SDK is a closed-source native library with no wasm
  target, and a Steam app needs a submission fee and a legal entity to
  own it, which a GPL community project does not have. It is still worth
  reading for its *item model* — id, title, description, tags, version,
  dependencies, author, preview image, visibility — which is the schema
  a registry should broadly copy rather than invent.
- **A static registry (recommended).** A separate repository holding one
  small manifest per item — id, kind (`mod` / `level` / `skin` / `theme`),
  title, author, version, size, `sha256`, licence, tags, preview image,
  blob URL — plus a generated `index.json`, published to GitHub Pages.
  Pages is the right host: it sends `access-control-allow-origin: *` and
  gzips responses, so the browser client reads it directly with no
  server, no CORS work and no hosting bill. Submission is a pull request,
  which means moderation is code review with an audit trail, and
  versioning is free. The cost is friction — a contributor needs a GitHub
  account — and no in-app upload.

  **Correction: the blobs cannot be release assets**, which an earlier
  draft of this section recommended. Two independent reasons, both
  measured against a real asset. A release download 302s to
  `release-assets.githubusercontent.com`, and *neither* the redirect nor
  the `206` carries any `access-control-*` header, so a browser on
  another origin cannot read it at all. And the redirect target is a
  signed URL that expires about an hour out (`se=…` in the query), so it
  could never have been written into a static index even if CORS
  allowed it. The registry has to serve the bytes from the same Pages
  site that serves the index. Generalizing: any third-party origin is
  assumed CORS-hostile until proven otherwise, so "just link to where
  the mod already lives" is not available to the browser client.
- **A content API service** next to the relay gateway: in-app upload,
  ratings, a moderation queue. This is the trap. It means accepting
  arbitrary uploads and serving them to other players' machines, and OLX
  content contains *executable script*, so it is a permanent moderation
  obligation plus storage, abuse handling and uptime — a far larger
  commitment than the relay itself. Defer until the registry demonstrates
  there is submission volume worth the ops.
- **Peer-to-peer only.** `CUdpFileDownloader` already fetches the joined
  server's mod over the game channel. It works, needs no infrastructure,
  and covers joining but not discovery. Keep it as the always-available
  fallback regardless of which option above is chosen.

### The code-execution question, and why the browser is the safe place

Mods ship Lua and Gusanos script, so installing community content is
running community code. The existing sandbox is narrower than it looks:
the Lua context opens only `base`, `table`, `string` and `math`
([src/gusanos/luaapi/context.cpp:127-130](../../src/gusanos/luaapi/context.cpp#L127))
— no `io`, no `os`, no `package`, so a mod cannot open a file or spawn a
process of its own accord.

In the browser that composes with the wasm sandbox: the worst a hostile
mod can do is corrupt the player's own IDBFS and crash the tab, and both
are recoverable by clearing site data. On desktop the same script runs
inside a native process with the engine's own file access. So the browser
is not the risky place to open a community library — it is the *safest*
place to pilot one, and the desktop client is what needs the stricter
policy. Whatever is decided, the registry should record a licence per
item and the client should verify `sha256` before installing; those two
fields are what make the difference between a channel and a liability.

## Order of work

1. Persistence (IDBFS mount, write-directory choice, flushes). Nothing
   else is possible first.
2. Local import and export, reusing the extracted installer. First
   visible win; no server involved.
3. Cut the wait with the measured levers. `--use-preload-cache` is done;
   what remains is evicting superseded package versions, the service
   worker, a smaller boot stage, and wasm download progress.
4. `emscripten_fetch` backend for `CHttp`, plus the per-frame
   `ProcessDownloads` pump.
5. The catalogue, reading a static index.
6. Verify `CUdpFileDownloader` once a real transport exists.
7. Decide the community layer — registry schema and licence policy —
   before the catalogue's index format hardens.
