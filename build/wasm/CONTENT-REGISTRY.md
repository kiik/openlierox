# The OpenLieroX content registry

Where community mods, levels and skins live, what metadata travels with
them, who decides what gets listed, and what a client is allowed to
assume. This is the frozen input to the on-demand catalogue (#4): every
field below is specified tightly enough that a client implementer never
has to guess.

A sibling to [WASM-CONTENT.md](WASM-CONTENT.md) (why the web build shows
four mods, and what content on demand costs) and
[WASM-PORT.md](WASM-PORT.md) (what the port is). It lives with the wasm
design records because that is where the need surfaced, but the policy is
cross-platform -- the desktop client is the one that needs the *stricter*
rules, for reasons set out under [Trust](#trust).

Like its siblings, this is a design record. Every claim about the engine
carries a `file:line`, verified against the tree at the time of writing.
Hosting behaviour was measured with `curl`, and the commands are given so
the measurements can be repeated. Decisions that are the maintainers' to
make rather than the author's are collected under
[Open questions](#open-questions), each with a recommendation, and are not
silently resolved anywhere else in this document.

## Decision, in one paragraph

**A static registry, not a service.** One repository holds one JSON
manifest per item and the content blobs; a GitHub Actions workflow
validates submissions and generates `index.json`; GitHub Pages serves
both. Submission is a pull request, so moderation is code review with an
audit trail, and there is no server, no database, no uptime obligation
and no hosting bill. The alternatives -- Steam Workshop, an upload API --
were considered and rejected in
[WASM-CONTENT.md](WASM-CONTENT.md#a-community-layer); this document does
not re-litigate them. What it adds is the part that has to be exact: the
schema, the fetch contract, and the trust and licence policy.

## Prior art, and what is worth taking from it

Three comparable systems were read for their design rather than their
infrastructure. Steam Workshop is covered in #32 and rejected there; the
other two are closer in spirit.

**Factorio's mod portal.** Each mod archive carries an `info.json` at its
root -- name, version, title, author, the minimum game version, and a
dependency list with real operators (`>=`, optional with `?`,
incompatible with `!`). The portal itself is a server with an API and
authenticated downloads.

**Thunderstore.** A package is a zip with a `manifest.json` inside it,
packages are namespaced by team, and a published version is **immutable**
-- it can never be overwritten, only superseded. Also a server, with a CDN
behind it.

What that suggests, and what this design does with it:

- **Immutable versioned artifacts.** Taken, and for free: blobs live at
  content-addressed paths, so a URL either returns the bytes it always
  returned or returns nothing. No policy needed to enforce it.
- **Manifests travel inside the package.** *Not* taken in schema 1, and
  this is a considered omission rather than an oversight. It is the right
  design -- it makes a blob self-describing, which is exactly what local
  import (#24) needs, since a file dragged off a player's disk arrives with
  no index entry at all. But no item in the existing corpus has such a
  file, so requiring one would mean repackaging everything before anything
  could be listed. The natural schema-2 addition is an optional
  `olx-item.json` at the archive root, carrying the same fields minus
  `url`, `size` and `sha256`, with the index remaining authoritative where
  the two disagree.
- **Dependency operators.** Not taken, deliberately: see
  [Dependencies](#dependencies). Equality only, because the engine cannot
  compare versions at all.
- **Namespacing.** Not taken; see [Open questions](#open-questions).
- **A server.** Not taken. Both systems need one, and that is precisely
  the commitment #32 identifies as the trap.

## What the engine can hold, and why the metadata has to be external

`ModInfo` has four data members: `valid`, `name`, `path`, `typeShort`,
plus a non-serialized `type`
([src/game/Mod.h:17-31](../../src/game/Mod.h#L17)). `LevelInfo` is
structurally identical
([src/game/Level.h:20-34](../../src/game/Level.h#L20)), and the map header
underneath it holds only `name`, `width`, `height`
([include/MapLoader.h:20-24](../../include/MapLoader.h#L20)). There is no
slot for author, version, checksum, licence or dependencies anywhere in
the engine's content model.

Nor does the content itself carry any of it. Every version field in every
content format is a *file-format* revision, not a content version:
`script.lgs` has `gs_header_t::Version`, range-checked against
`GS_FIRST_SUPPORTED_VERSION`..`GS_VERSION`
([include/CGameScript.h:37-41](../../include/CGameScript.h#L37),
enforced at
[src/common/CGameScript.cpp:500-505](../../src/common/CGameScript.cpp#L500)),
and `.lxl` has a `version` compared against `MAP_VERSION`
([src/common/MapLoader_LieroX.cpp:40-48](../../src/common/MapLoader_LieroX.cpp#L40)).
An LX source mod's `main.txt` is read for exactly one key, `ModName`
([src/common/CGameScript.cpp:529](../../src/common/CGameScript.cpp#L529)).
A Gusanos `mod.cfg` is not a manifest at all -- it is a console script
run verbatim
([src/gusanos/gusgame.cpp:457](../../src/gusanos/gusgame.cpp#L457)).
Skins carry no metadata whatsoever; the display name is the filename
minus its extension
([src/main.cpp:1015](../../src/main.cpp#L1015)). And the `.ver`
convention is dead data: `share/gamedir/promode/promode.ver` contains
`130` and nothing in the tree reads it.

So the manifest is not duplicating engine state. It is the only place this
information can exist.

## Content kinds

The registry's `kind` enum has to match what the engine actually loads,
which is less tidy than "mods and maps". Mods are **directories in the
gamedir root**, siblings of `levels/` and `cfg/`, detected by sniffing for
`script.lgs`, `main.txt` or an `objects/` subdirectory
([src/common/CGameScript.cpp:561-577](../../src/common/CGameScript.cpp#L561),
probes at
[:472](../../src/common/CGameScript.cpp#L472),
[:516](../../src/common/CGameScript.cpp#L516),
[:537](../../src/common/CGameScript.cpp#L537)) --
which is why `checkGusMod` needs an explicit dot-directory guard so it
does not match `.git/objects`
([:540-542](../../src/common/CGameScript.cpp#L540)). And there are *two*
unrelated things called a theme, in two different directories.

| `kind` | Installs to | Shape | Enumerated by |
|---|---|---|---|
| `mod` | `<name>/` in the gamedir root | directory | [src/main.cpp:991-1000](../../src/main.cpp#L991), [Menu_Local.cpp:919](../../src/client/DeprecatedGUI/Menu_Local.cpp#L919) |
| `level` | `levels/` | `.lxl` file **or** directory | [src/common/CMap.cpp:2458-2466](../../src/common/CMap.cpp#L2458), [MenuSystem.cpp:1070](../../src/client/DeprecatedGUI/MenuSystem.cpp#L1070) |
| `skin` | `skins/` | single `png` or `bmp` image | [src/main.cpp:1003-1021](../../src/main.cpp#L1003), [Menu_Player.cpp:842](../../src/client/DeprecatedGUI/Menu_Player.cpp#L842) |
| `campaign` | `games/<name>/` | directory with `game.cfg` | [Menu_Local.cpp:204-210](../../src/client/DeprecatedGUI/Menu_Local.cpp#L204) |
| `gui-theme` | `themes/<name>/` | directory | [Menu_Main.cpp:346](../../src/client/DeprecatedGUI/Menu_Main.cpp#L346) |
| `map-theme` | `data/themes/<name>/` | directory with `theme.txt` | [src/common/CMap.cpp:395](../../src/common/CMap.cpp#L395), [:308-315](../../src/common/CMap.cpp#L308) |
| `gamesettings` | a single `*.gamesettings` in the gamedir root | single file | [src/game/SettingsPreset.cpp:33](../../src/game/SettingsPreset.cpp#L33) |

`gui-theme` and `map-theme` are deliberately spelled differently because
the engine reads them from `themes/` and `data/themes/` respectively
([Menu_Main.cpp:346](../../src/client/DeprecatedGUI/Menu_Main.cpp#L346)
vs [CMap.cpp:395](../../src/common/CMap.cpp#L395)). Collapsing them into
one `theme` value would put half the items in the wrong directory.

Two kinds are deliberately **excluded**, and the reasons are worth
recording so nobody adds them by reflex:

- **Anything under `cfg/`** -- weapon-restriction presets
  ([Menu_Local.cpp:1796-1797](../../src/client/DeprecatedGUI/Menu_Local.cpp#L1796))
  and config presets
  ([src/main.cpp:1024-1036](../../src/main.cpp#L1024)). `cfg/` holds the
  player's own configuration, and the engine's existing file-transfer
  sanitizer already refuses that prefix outright
  ([src/common/FileDownload.cpp:917](../../src/common/FileDownload.cpp#L917)).
  A distribution channel should not be the first thing allowed to write
  there.
- **Dedicated-server scripts** (`scripts/`) -- selected by a config
  string, never enumerated in any GUI
  ([src/server/DedicatedControl.cpp:416-421](../../src/server/DedicatedControl.cpp#L416)),
  and they run outside every sandbox discussed under [Trust](#trust).

Touchscreen layouts (`touchscreen/*.yaml`,
[src/client/TouchControls.cpp:931](../../src/client/TouchControls.cpp#L931))
are a plausible future kind and the name `touchscreen-layout` is reserved
for them, but nothing is listed under it in schema 1.

## The schema

Two artefacts, one authored and one generated.

- `items/<id>.json` -- one manifest per item, hand-written, reviewed in a
  pull request. The source of truth.
- `index.json` -- the concatenation of every manifest plus a header,
  generated by CI. **The only file a client fetches to browse.** Clients
  must not read `items/` directly; it is an implementation detail of the
  repository and may be restructured.

Everything is UTF-8 JSON. No comments, no trailing commas.

### Index document

```json
{
  "schema": 1,
  "generated": "2026-07-28T15:40:00Z",
  "items": [ /* item objects */ ]
}
```

| Field | Type | Required | Meaning |
|---|---|---|---|
| `schema` | integer | yes | Schema major version. `1` today. |
| `generated` | string | yes | RFC 3339 / ISO 8601 UTC timestamp, `Z` suffix. Informational. |
| `items` | array of item objects | yes | May be empty. Order is not significant; clients must sort for display. |

### Item object

Required fields. A client must reject an item that is missing any of
them, or whose value fails the stated constraint -- reject the *item*,
not the index.

| Field | Type | Constraint |
|---|---|---|
| `id` | string | `[a-z0-9][a-z0-9._-]{0,63}`. Stable forever. Unique across the index. A registry identifier only -- it is **not** the on-disk name. |
| `kind` | string | One of the values in [Content kinds](#content-kinds). |
| `installName` | string | The exact file or directory name the item creates in its target directory. 1..64 chars. No `/`, no `\`, no `..`, not `.`; no control characters, no leading or trailing space or dot. Case and spaces are preserved verbatim -- see below. |
| `title` | string | 1..80 chars, display name. May differ from `id` and may contain spaces and punctuation. |
| `author` | string | 1..80 chars. Who made it. Accountability, not decoration -- see [Licence](#licence-and-redistribution). |
| `version` | string | 1..32 chars, `[0-9A-Za-z.+-]`. **Opaque.** See below. |
| `licence` | string | An SPDX licence identifier, or `custom`. See [Licence](#licence-and-redistribution). |
| `format` | string | `zip` or `file`. How to install the blob -- see [Installing](#installing-a-blob). For `kind: "skin"` the file must be `.png` or `.bmp`; the other two extensions the skin list accepts cannot actually be decoded, see [Parsing](#parsing-untrusted-files-which-happens-before-any-script-runs). |
| `url` | string | The blob. Relative reference resolved against the index document's own URL per RFC 3986; if absolute, the scheme must be `https`. |
| `size` | integer | Exact byte length of the blob at `url`, after any transport `content-encoding` is undone. `> 0`. |
| `sha256` | string | 64 lowercase hex digits. SHA-256 over exactly those `size` bytes. |

Optional fields. A client must tolerate their absence and must ignore any
field it does not recognise.

| Field | Type | Constraint / meaning |
|---|---|---|
| `description` | string | <= 500 chars, plain text. No markup -- the menus have no renderer for it. |
| `tags` | array of string | Each `[a-z0-9-]{1,24}`, <= 8 entries. |
| `installSize` | integer | Bytes on disk after installation. Lets a client check quota before it starts; IDBFS is not infinite. |
| `preview` | object | `{ "url": ..., "sha256": ..., "size": ... }`, same rules as the blob fields. PNG. Verified before decoding, because decoding is a parsing surface -- see [Trust](#trust). |
| `homepage` | string | `https` URL. Where the work lives upstream. |
| `source` | string | `https` URL of the original release -- forum thread, repository, archive page. Provenance for a licence dispute. |
| `licenceUrl` | string | `https` URL of the licence text. **Required when `licence` is `custom`.** |
| `licenceNote` | string | <= 500 chars. Free text recording how redistribution rights were established. |
| `requires` | array of object | `[{ "id": ..., "version": ... }]`; `version` optional. See [Dependencies](#dependencies). |
| `addedAt`, `updatedAt` | string | RFC 3339 UTC timestamps. |
| `engineMin` | string | Lowest OLX version known to load the item. Advisory; a client may warn but must not hide the item. |

### Why `installName` exists, separately from `id`

Because the engine identifies content by its on-disk name, and those names
are not slugs. `MW 1.0`, `Cruel Weapons 0.96` and `Gusanos` are all real
directory names in `share/gamedir`, and all three are referenced verbatim
somewhere:

- A campaign's `game.cfg` names its mod as a literal string --
  `Mod = MW 1.0` in the bundled introduction campaign, read at
  [src/game/SinglePlayer.cpp:93](../../src/game/SinglePlayer.cpp#L93).
- Every Gusanos mod's resource fallback is the literal `"Gusanos"`
  ([src/gusanos/gusgame.cpp:82](../../src/gusanos/gusgame.cpp#L82)).
- For a Gusanos mod the directory name *is* the name shown to the player:
  `checkGusMod` sets `info.name = info.path = basefn`
  ([src/common/CGameScript.cpp:546-552](../../src/common/CGameScript.cpp#L546)).

Path lookup is case-insensitive in practice -- `GetExactFileName` walks the
components and matches case-insensitively when a direct `stat` fails
([src/common/FindFile.cpp:373-420](../../src/common/FindFile.cpp#L373)) --
so case alone would probably survive. Spaces, punctuation and the
player-visible label would not. Keeping `id` as a slug for URLs, CI and
dependency references, and `installName` as the byte-exact on-disk name, is
the only arrangement that satisfies both.

For `format: "zip"`, every archive entry must be under
`<installName>/`. For `format: "file"`, `installName` is the filename
written, including its extension.

### Example

```json
{
  "id": "promode",
  "kind": "mod",
  "installName": "promode",
  "title": "Pro Mode",
  "author": "The Gusanos team",
  "version": "1.30",
  "licence": "custom",
  "licenceUrl": "https://example.invalid/promode/licence.txt",
  "licenceNote": "Redistribution confirmed by the author in the submission PR.",
  "format": "zip",
  "url": "blobs/3f/3fa1c0de....zip",
  "size": 2984110,
  "sha256": "3fa1c0de00000000000000000000000000000000000000000000000000000000",
  "installSize": 3080647,
  "requires": [ { "id": "gusanos" } ],
  "tags": ["gusanos", "physics"],
  "addedAt": "2026-07-28T00:00:00Z"
}
```

### Why `version` is opaque

Because nothing can compare it. No content kind in the tree carries a
semantic version -- the only versions in any format are the file-format
revisions cited above -- and `ModInfo` has no field to record what the
player installed
([src/game/Mod.h:17-31](../../src/game/Mod.h#L17)). A client therefore
cannot answer "is my copy older than the listed one" from the engine's
own state, and inventing an ordering the engine cannot act on would be
schema weight for a capability that does not exist.

So: `version` is compared by **equality only**. A client that wants to
offer updates must remember the `id`/`version`/`sha256` it installed, in
its own sidecar record, and compare against the index. Submitters are
asked to use `MAJOR.MINOR.PATCH` or a `YYYYMMDD` date, and the workflow
does not enforce it. Ordering can be added under schema 2 once something
can use it.

The index carries **one entry per `id`** -- the current version. Older
blobs stay reachable at their content-addressed URLs but are not listed,
for the same reason: a client that cannot compare versions has nothing to
do with a version list.

### Dependencies

`requires` is optional and exists because content-to-content references
are already real in the engine, not because a package manager sounds
nice. Three concrete cases:

- **Every Gusanos mod depends on the `Gusanos` directory.**
  `C_DefaultModPath` is the hardcoded string `"Gusanos"`
  ([src/gusanos/gusgame.cpp:82](../../src/gusanos/gusgame.cpp#L82)), and
  every resource locator is given a three-tier search path -- level
  directory, selected mod, then `Gusanos/` -- for fonts, GUI XML,
  scripts, objects, sounds, sprites and map effects
  ([:559-604](../../src/gusanos/gusgame.cpp#L559)). Install a Gusanos mod
  without `Gusanos/` present and resources silently fall back to nothing.
- **A campaign names a mod and a level.** `games/<game>/game.cfg` gives
  each level a `Dir` and a `Mod`, with `Mod` defaulting to `Classic`
  ([src/game/SinglePlayer.cpp:92-95](../../src/game/SinglePlayer.cpp#L92)).
  Failure produces a red HTML string and no repair path
  ([:99-111](../../src/game/SinglePlayer.cpp#L99)).
- **A LieroX level names a map theme.** The `.lxl` header embeds a
  32-byte theme name
  ([src/common/MapLoader_LieroX.cpp:57](../../src/common/MapLoader_LieroX.cpp#L57)),
  resolved to `data/themes/<theme>/theme.txt`
  ([src/common/CMap.cpp:308-315](../../src/common/CMap.cpp#L308)); a
  missing theme makes `CMap::Create` fail
  ([:195-197](../../src/common/CMap.cpp#L195)). The theme name never
  reaches `LevelInfo`, so nothing outside `CMap` can see the dependency
  -- which is exactly why it has to be declared in the manifest.

Rules, kept deliberately thin:

- `requires` entries must resolve to an `id` **in the same index**. CI
  fails a submission that references an unknown or removed id.
- The graph must be acyclic, and CI checks it.
- A client installs dependencies before the item that names them, and
  installs nothing if any dependency is missing from the index.
- `version` inside a `requires` entry, if present, is an equality
  constraint -- for the same reason as above. Omitting it means "any
  listed version", which is what almost every real case wants.

There is no transitive-conflict resolution, no version ranges and no
optional dependencies. If the corpus ever needs them, that is a schema
bump with a real motivating example attached.

### Schema evolution

The compatibility contract, stated so a client written today keeps working
and a client written today can also refuse something it genuinely cannot
handle:

1. A client **must** refuse an index whose `schema` is greater than the
   version it implements, and say so to the player. It must not attempt a
   partial read.
2. A client **must** ignore object fields it does not recognise. Adding an
   optional field is therefore not a schema bump.
3. A client **must** skip -- not reject the whole index over -- an item
   whose `kind` or `format` it does not recognise, or which fails a
   constraint above. New `kind` and `format` values are therefore also not
   a schema bump.
4. `schema` increments only when an existing field changes meaning, type
   or requiredness, or when a required field is added. Such a change also
   requires publishing the old index at a frozen URL for one release
   cycle, so existing clients do not simply break.

Rule 3 is what makes the enum in [Content kinds](#content-kinds) safe to
extend, and it is why that table can be complete rather than minimal.

## Hosting and fetching

### The constraint that decides the design

Everything here was measured, not assumed. GitHub Pages:

```
curl -sSI https://openlierox.net/web-demo/release/20260717.2/openlierox.wasm
```

returns `access-control-allow-origin: *`, `cache-control: max-age=600`,
and for the `.data` also `content-encoding: gzip`. So a browser client can
fetch JSON *and* binary assets from a Pages origin, cross-origin, with no
server in between. Pages allows no header configuration, so `max-age=600`
is fixed.

GitHub **release assets cannot be fetched by a browser.**

```
curl -sS -o /dev/null -D - -H 'Origin: https://openlierox.net' -r 0-0 -L \
  https://github.com/openlierox/openlierox/releases/download/20260717.2/openlierox-20260717.2-wasm.zip
```

Neither the `302` from `github.com` nor the `206` from
`release-assets.githubusercontent.com` carries **any** `access-control-*`
header. This contradicts the "blobs as release assets" sketch in
[WASM-CONTENT.md](WASM-CONTENT.md#options) and in #32, and it is the
single measurement that shapes the rest of this section: a browser cannot
read release assets, so the registry cannot put blobs there.

`raw.githubusercontent.com` does send `access-control-allow-origin: *`
(with `cache-control: max-age=300`), so it is technically usable, but it
is a source-code convenience endpoint with undocumented rate limiting,
not a content CDN, and every blob served from it lives in git history
forever.

The general form of the constraint is the important part: **arbitrary
third-party origins do not send CORS headers.** A registry that stored
only manifests and pointed `url` at wherever each author happens to host
their mod would work on desktop and fail in the browser for almost every
entry. The registry therefore has to serve the bytes itself. That is not
a preference; it is the measurement.

### Layout

One repository, published to Pages:

```
items/<id>.json          authored manifests, reviewed in PRs
blobs/<xx>/<sha256>.<ext>  content, addressed by hash; <xx> = first 2 hex chars
previews/<xx>/<sha256>.png
index.json               generated by CI at deploy time
schema/index.schema.json  JSON Schema for the above, for CI and editors
```

Blob paths are content-addressed, so they are immutable: a given URL
always returns the same bytes or nothing. That makes `max-age=600`
harmless for blobs -- a client caches them in its own persistent
directory keyed by `sha256` and never refetches, which is the only durable
caching available given that Pages allows no header configuration. The
index is the one thing that should be short-cached, and ten minutes is a
reasonable delisting latency.

Ceilings worth writing down: GitHub's documented limits are a 1 GB
published Pages site, a 100 GB/month soft bandwidth allowance and 10
builds per hour. At a few megabytes per item -- the largest mod in
`share/gamedir` is `telek` at 6.8 MB, and `Doom` and `HVL mod v1.0` are
the only others above 5 MB -- that is on the order of a couple of hundred
items before the site limit is the binding constraint. That is
comfortably beyond the 33 mods and 140 levels (79 `.lxl` files and 61
directories) that `share/gamedir` holds today, and it is the number to
watch rather than a problem to solve now.

### Fetch contract for a client

- `GET <registryBase>/index.json`. No authentication, no cookies. In the
  browser: `fetch(url, { mode: 'cors', credentials: 'omit' })`.
- `registryBase` must be configurable and must default to the project's
  registry. It is a plain string, not a mirror list -- the existing
  `cfg/downloadservers.txt` mirror list has both of its entries dead (see
  [WASM-CONTENT.md](WASM-CONTENT.md#a-server-side-catalogue-second)) and
  is not a model to copy.
- Resolve each `url` against the index document's own URL. Refuse a
  resolved URL whose scheme is not `https`, and refuse one that leaves
  `registryBase`'s directory unless it is absolute and `https` -- a
  relative `../..` must not become a way to point the installer at an
  arbitrary path.
- Enforce a maximum blob size before starting the transfer, from `size`.
  A cap in the tens of megabytes is right for today's corpus. A client
  that streams `size` bytes it never checked is a client an index typo can
  use to fill the player's disk.
- Verify the received length equals `size` **and** the SHA-256 equals
  `sha256`, before a single byte is written into the game directory. On
  mismatch, discard and report; do not install "most of" an item.
- Pages sends `accept-ranges: bytes`, so resuming an interrupted transfer
  with a `Range` request is available. Optional, and the hash check at the
  end is what makes it safe.

### SHA-256 is a new dependency, and a small one

The engine has no cryptographic hash. What it has is CRC-32
([src/util/CRC32.h:12-31](../../src/util/CRC32.h#L12), built on zlib's
`crc32`) and Adler-32
([src/common/StringUtils.cpp:1161-1188](../../src/common/StringUtils.cpp#L1161)),
the latter being what the in-game UDP file transfer already uses
([include/FileDownload.h:174-175](../../include/FileDownload.h#L174)).
There is no SHA-1 and no SHA-256 anywhere in `src/`, `include/` or
`libs/`. Breakpad ships an MD5 but is hard-disabled
([CMakeOlxCommon.cmake:207-210](../../CMakeOlxCommon.cmake#L207)). No
target links `libcrypto`, `libssl` or mbedTLS; the wasm build compiles
curl with all TLS backends off
([build/wasm/CMakeLists.txt:146-148](CMakeLists.txt#L146)), and curl
exposes no digest API in any case.

Adler-32 is not a substitute. It is a checksum, not a hash: it detects
transmission damage and does nothing at all against a deliberately
crafted blob, which is precisely the case a content registry has to
survive. Reusing it here because it is already linked would be the wrong
saving.

The cheapest correct answer is a single-file public-domain SHA-256
dropped into `src/util/` next to `CRC32.h`. It needs no CMake edit: the
build globs `src/*.c*` recursively
([CMakeOlxCommon.cmake:196](../../CMakeOlxCommon.cmake#L196)). Linking
OpenSSL to get one hash function would add a dependency to every platform
that currently does without it.

The index format has a second, smaller cost of the same kind: **there is
no JSON parser in the tree** -- no `libs/` entry, no include, nothing.
The engine parses INI-style files through `ConfigHandler`
([include/ConfigHandler.h:37](../../include/ConfigHandler.h#L37)) and YAML
through yaml-cpp, which *is* linked
([CMakeOlxCommon.cmake:445](../../CMakeOlxCommon.cmake#L445)). JSON is
still the right choice -- the browser parses it natively for free, and the
generator is a few lines of Python -- but it means one vendored
single-header parser, and that should be a conscious line item in #4
rather than a surprise. See [Open questions](#open-questions).

## Installing a blob

`format` says what the bytes are, and `kind` says where they go. This is
the whole of it:

- `format: "file"` -- write the blob verbatim to the target directory for
  `kind`, under a filename derived from the manifest, not from the URL.
  Used for `skin`, `gamesettings`, and single-file `.lxl` levels. It
  matches what the engine already does for map downloads, which install a
  plain file with no unzip
  ([src/client/CClient.cpp:492](../../src/client/CClient.cpp#L492),
  [:560-593](../../src/client/CClient.cpp#L560)). And it is the right
  choice for `.lxl` on its own merits: those files are already
  compressed, so zipping one buys nothing -- `747.lxl` gzips from 1430242
  to 1427359 bytes, 0.2%.
- `format: "zip"` -- extract into the target directory for `kind`. Used
  for `mod`, `campaign`, `gui-theme`, `map-theme`, and directory-shaped
  Gusanos levels, which are real and numerous: `share/gamedir/levels`
  holds 79 `.lxl` files **and** 61 directories.

### Rules the extractor must enforce

The engine's existing extraction loop is
[src/client/CClient.cpp:625-643](../../src/client/CClient.cpp#L625). It
already gets two things right and four things wrong, and #24 is pulling
it out into a reusable function -- so these are requirements on that
function, stated once, here.

Already right, and to be kept:

- Reject any entry name containing `..`
  ([:627](../../src/client/CClient.cpp#L627)).
- Require the entry name to be inside the item's own directory
  ([:628](../../src/client/CClient.cpp#L628)).

To be fixed, and each is a real defect:

1. **The prefix check has no separator.** It is
   `stringtolower(fname).find(stringtolower(sModDownloadName)) != 0`
   ([:628](../../src/client/CClient.cpp#L628)) -- a bare prefix test. For
   an item named `Classic`, an entry called `Classic-extra/x` passes and
   is written to a *different* top-level directory. The check must be
   against `installName` followed by `/` or end-of-name.
2. **Absolute entry names are not rejected.** A leading `/` (or a
   `C:\` on Windows) contains no `..` and passes the prefix test only by
   accident of what `find` returns. Reject them explicitly.
3. **Existing files are silently skipped**
   ([:629-631](../../src/client/CClient.cpp#L629), via
   `IsFileAvailable`). That is how a truncated install becomes permanent.
   Overwrite, or fail the whole install; do not half-do it.
4. **Extraction is unbounded.** The `zip_fread` loop
   ([:638-640](../../src/client/CClient.cpp#L638)) writes until the entry
   ends, with no cap on the total. A zip bomb fills the disk, or the
   IDBFS quota. Enforce a total-extracted limit, cross-checked against
   `installSize` where the manifest supplies it. Windows backslash
   separators are also not normalized, and entry modes and symlink
   entries are not inspected.

And one rule that is new, and is the most important line in this
document:

5. **Never write to a search-path root.** `initLuaGlobal()` globs
   `startup*.lua` across the search paths and executes every match at
   startup, in the *global* Lua context
   ([src/gusanos/luaapi/context.cpp:1084-1091](../../src/gusanos/luaapi/context.cpp#L1084),
   the glob at
   [:1088](../../src/gusanos/luaapi/context.cpp#L1088)). As
   [Trust](#trust) sets out, the global context is the privileged one. An
   installer that can drop a file into a search-path root can therefore
   arrange for arbitrary Lua to run at every launch.

   The one kind that legitimately installs at the root is
   `gamesettings`. Its carve-out is narrow: exactly one file, matching
   `*.gamesettings`, and nothing else -- never a `startup*.lua`, never a
   directory. Every other kind writes only inside its own subdirectory.

A client must also verify, after extracting and before listing the item
as installed, that the engine agrees the item is valid -- `infoForMod`
for a mod ([src/game/Mod.cpp:14-19](../../src/game/Mod.cpp#L14)),
`infoForLevel` for a level
([src/game/Level.cpp:17-31](../../src/game/Level.cpp#L17)) -- and undo
the install if not. Note that this makes the old `<modname>/script.lgs`
precondition
([src/client/CClient.cpp:616](../../src/client/CClient.cpp#L616))
unnecessary as well as wrong: it excludes Gusanos mods, which have
`mod.cfg` and no script, and `infoForMod` accepts those correctly
([src/common/CGameScript.cpp:537-557](../../src/common/CGameScript.cpp#L537)).

## Trust

Installing community content means running community code. The honest
question is *which* code and *how much* it can reach, and the answer is
not uniform across content kinds -- which is the finding that matters,
because the intuitive ordering is wrong.

### Mod and level scripts are genuinely contained

Two Lua contexts exist:
`luaIngame`
([src/gusanos/luaapi/context.cpp:28](../../src/gusanos/luaapi/context.cpp#L28))
and `luaGlobal`
([:29](../../src/gusanos/luaapi/context.cpp#L29)). Both open only four
standard libraries -- `base`, `table`, `string`, `math`
([:120-130](../../src/gusanos/luaapi/context.cpp#L120)) -- so no `io`, no
`os`, no `package`: content script cannot open a file or spawn a process
through the standard library at all.

Mod and level scripts run in `luaIngame`
([src/gusanos/gusgame.cpp:462-491](../../src/gusanos/gusgame.cpp#L462)),
and the engine bindings gate the two dangerous doors on context identity:

- Executing an OLX console command is refused unless the caller is
  `luaGlobal`
  ([src/gusanos/lua/bindings-game.cpp:722-726](../../src/gusanos/lua/bindings-game.cpp#L722)).
- Writing an engine variable is refused the same way
  ([:652-656](../../src/gusanos/lua/bindings-game.cpp#L652)).

Both checks are real, and both cover a surface that is otherwise the
engine's entire command and variable table -- `l_commands_get` resolves
any registered command by name
([:777-795](../../src/gusanos/lua/bindings-game.cpp#L777)), and
`l_settings_set` would set any registered variable with
`rights.Everything()`
([:652-696](../../src/gusanos/lua/bindings-game.cpp#L652)). The
restriction is what makes a mod script safe, not the library list alone.

The one write a mod script has is `dump()`, and it is tightly scoped: the
name is restricted to `[A-Za-z0-9_-]` and the path is fixed to
`gusanos/persistance/<name>.lpr` under the write directory
([src/gusanos/lua/bindings.cpp:236-252](../../src/gusanos/lua/bindings.cpp#L236)).
`undump()` evaluates that file as a Lua expression
([:296](../../src/gusanos/lua/bindings.cpp#L296)) -- worth knowing, but
it is the sandbox evaluating something it wrote itself, inside the same
sandbox, so it is not an escalation.

A Gusanos `mod.cfg` reaches a *different* command namespace: the Gusanos
console
([src/gusanos/gusgame.cpp:457](../../src/gusanos/gusgame.cpp#L457) ->
[src/gusanos/console/console.cpp:209](../../src/gusanos/console/console.cpp#L209),
[:154](../../src/gusanos/console/console.cpp#L154)), which registers
cvars, `ALIAS`, `ECHO` and `EXEC`
([src/gusanos/gconsole.cpp:99-102](../../src/gusanos/gconsole.cpp#L99)).
It cannot reach `setVar` or any other OLX command. `EXEC` resolves
relative to the mod directory
([:105-108](../../src/gusanos/gconsole.cpp#L105)) but does not check for
`..`; that is worth fixing, and it buys an attacker the ability to parse
another Gusanos config file, which is not much. Note also that the
`autoexec.cfg` shipped in `share/gamedir/promode/` is **dead** -- the
string appears nowhere in `src/`, so OLX never reads it. It is a Gusanos
leftover, not a vector.

### Campaigns are the privileged kind

`games/<game>/game.cfg` may carry an `Exec` key, and its value is split on
`;` and handed to `Execute(&stdoutCLI(), cmd)` -- arbitrary OLX console
commands, with none of the context gating above
([src/game/SinglePlayer.cpp:214-220](../../src/game/SinglePlayer.cpp#L214)).
The bundled introduction campaign uses it for `addbot` and `setWormTeam`
(`share/gamedir/games/introduction/game.cfg`), which is exactly what it
was added for -- and it is nonetheless a general command channel opened
by a data file.

What that channel reaches is worth being precise about, because it is
narrower than "arbitrary code" and wider than "harmless":

- `setVar` sets any registered variable with `rights.Everything()`
  ([src/common/Command.cpp:1727-1757](../../src/common/Command.cpp#L1727)).
- `connect` joins an arbitrary server
  ([:887](../../src/common/Command.cpp#L887)); `quit`, `crash` and
  `coreDump` are all reachable
  ([:1042](../../src/common/Command.cpp#L1042),
  [:572](../../src/common/Command.cpp#L572),
  [:588](../../src/common/Command.cpp#L588)).
- `script`, which loads an external dedicated-control script, is *not* --
  it refuses outside dedicated mode, and rejects absolute paths and `..`
  ([:1052-1073](../../src/common/Command.cpp#L1052)).

So there is no file-write and no process-spawn command in that surface.
There is annoyance, denial of service, and settings tampering.
Campaigns also traverse by design: `Dir = ../../levels/ctf_poo` in the
bundled campaign, and the loader rewrites the path relative to `levels/`
([src/game/SinglePlayer.cpp:97](../../src/game/SinglePlayer.cpp#L97)), so
a campaign can reference any file the search paths can reach. That is read
access inside the game directory, and it is intended behaviour.

### Parsing untrusted files, which happens before any script runs

Every installed item is decoded by the engine's own loaders, and those run
whether or not the item contains a line of script. This is the surface
that matters most and the one that gets described least accurately, so:
what it is, and how bad it is.

**Images are delegated, not hand-rolled.** `USE_GD_FOR_IMAGE_LOADING` is
set unconditionally for every non-dedicated build
([src/client/GfxPrimitives.cpp:20](../../src/client/GfxPrimitives.cpp#L20)),
so content images decode through libgd -- `gdImageCreateFromPngPtr`,
`gdImageCreateFromJpegPtr`, `gdImageCreateFromGifPtr`
([:2877-2894](../../src/client/GfxPrimitives.cpp#L2877)) -- with `.bmp`
going through SDL's own loader
([:2878](../../src/client/GfxPrimitives.cpp#L2878)). Dimensions come from
the decoder rather than from a separately declared field
([:2826-2861](../../src/client/GfxPrimitives.cpp#L2826)). There is no
OLX-written pixel-format parser; the residual risk is inherited
dependency CVEs, which is a real but ordinary risk that every program
displaying a PNG carries.

One concrete schema consequence: the skin list accepts four extensions,
`png`, `bmp`, `tga` and `pcx`
([src/main.cpp:1007-1010](../../src/main.cpp#L1007)), but the loader has a
branch for only the first two of those and rejects the rest as "file
extension unknown"
([GfxPrimitives.cpp:2891-2894](../../src/client/GfxPrimitives.cpp#L2891)).
A `skin` item's blob must therefore be `.png` or `.bmp`; CI should
enforce it rather than let a `.tga` skin be listed, installed, and then
silently fail to load.

**Sound is mostly delegated.** OGG goes through libvorbisfile
([src/sound/sound_sample_openal.cpp:39-110](../../src/sound/sound_sample_openal.cpp#L39)).
WAV is a hand-rolled RIFF parser
([:121-187](../../src/sound/sound_sample_openal.cpp#L121)) which is
written carefully -- every `fread` return checked, non-PCM rejected -- with
one trusted value: `buffer.resize(chunkSize)` from the file's declared
chunk size
([:166](../../src/sound/sound_sample_openal.cpp#L166)). That is an
allocation failure, not memory corruption.

**Level and mod loaders read counts out of the file, and were recently
hardened to bound them.** The Teeworlds loader is the clearest example: it
reads eight counts and sizes from a 36-byte header, and
`validateHeaderSizes` requires the whole declared structure to fit inside
the actual file, in 64-bit arithmetic, before anything is resized
([src/common/MapLoader_Teeworlds.cpp:393-413](../../src/common/MapLoader_Teeworlds.cpp#L393),
with the threat stated in the comment at
[:388-392](../../src/common/MapLoader_Teeworlds.cpp#L388)). Item and data
offsets are separately checked for monotonicity and range
([:433](../../src/common/MapLoader_Teeworlds.cpp#L433),
[:469](../../src/common/MapLoader_Teeworlds.cpp#L469)). The mod loader
clamps the weapon count to 4096 with a comment naming downloaded mods as
the reason
([src/common/CGameScript.cpp:669-676](../../src/common/CGameScript.cpp#L669)),
and clamps point, colour and action counts the same way
([:898](../../src/common/CGameScript.cpp#L898),
[:915](../../src/common/CGameScript.cpp#L915),
[:1100](../../src/common/CGameScript.cpp#L1100)). The Gusanos directory
loader parses no binary at all -- its `parseHeader` is `return true`
([src/common/MapLoader_Gusanos.cpp:29-31](../../src/common/MapLoader_Gusanos.cpp#L29))
and map dimensions come from the material image, not from a declared
field.

What is left is memory *exhaustion*, not memory corruption, and it is
worth naming precisely because it is the part a registry makes easier to
reach:

- `Decompress` inflates into an unbounded `std::string`, appending until
  the stream ends with no output cap
  ([src/common/StringUtils.cpp:1126-1158](../../src/common/StringUtils.cpp#L1126)).
  A decompression bomb in a Teeworlds `.map` data chunk is straightforward
  memory exhaustion. The `.lxl` loader avoids this by passing a declared
  output size to `uncompress` instead
  ([src/common/MapLoader_LieroX.cpp:86](../../src/common/MapLoader_LieroX.cpp#L86)).
- An `.lxl` extra-data chunk's `Uint32 size` goes straight into
  `new uint8_t[size]`
  ([:192-197](../../src/common/MapLoader_LieroX.cpp#L192)); the
  `if(pSource == NULL)` guard below it is dead, because `new` throws.
- `Proj_EventAndAction::read` is the one count-driven `resize` in the mod
  loader with no bound: `events.resize(eventNum)` straight from the file
  ([src/common/CGameScript.cpp:2318-2321](../../src/common/CGameScript.cpp#L2318)).
- Mod-declared filenames are unsanitized and are concatenated into paths
  ([src/common/CGameScript.cpp:1166](../../src/common/CGameScript.cpp#L1166),
  [:1192](../../src/common/CGameScript.cpp#L1192)), and OLX path
  resolution passes `..` through as an ordinary component
  ([src/common/FindFile.cpp:416](../../src/common/FindFile.cpp#L416)). So
  a crafted mod can make the client *open* an arbitrary path -- read-only,
  and only to hand the bytes to an image or sound decoder. Low severity,
  and a real traversal on the read side.

The honest summary: this area has been audited, the hard bounds are in
place, and the remaining failure mode is a process that allocates too much
and dies. In the browser that is a crashed tab; on desktop it is an OOM.
That argues for the blob size cap in the
[fetch contract](#fetch-contract-for-a-client) rather than for a scarier
install prompt.

It also argues for a test corpus. Malformed-content coverage is currently
absent -- `tests/headless/test_teeworlds_map.py` loads one valid map and
there are no loader tests under `tests/unit/`. A registry that serves
third-party content to third-party machines should ship with a handful of
deliberately broken items in the headless suite: a truncated `.lxl`, a
`.map` with a declared count larger than the file, a zip bomb, and an
archive with an entry escaping its prefix. That is cheap, and it is the
difference between "the bounds are in" and "the bounds are checked".

### What is actually risky

Ranked, with the inflated items named as inflated:

1. **`Exec` in a campaign.** A general command channel in a plain-text
   data file, with no sandbox gate. This is the real one.
2. **Untrusted binary parsing**, as set out just above: bounded to
   memory exhaustion rather than corruption, but reachable by every
   installed item whether or not it contains script.
3. **Filesystem placement.** Not the content's doing but the installer's:
   rule 5 in [the extractor rules](#rules-the-extractor-must-enforce).
   The consequence of getting it wrong is worse than anything in 1 or 2,
   which is why it is a hard requirement rather than a warning.
4. **Mod and level Lua.** Contained, as shown above. Real but bounded:
   it can make the game misbehave while it is loaded.
5. **`.ver` files, `autoexec.cfg`, `mod.cfg` reaching OLX commands.** All
   things that sound like vectors and are not: the first two are read by
   nothing, and the third reaches a different, much smaller namespace.

### Browser versus desktop

In the browser, all of the above composes with the wasm sandbox. The worst
outcome is a corrupted IDBFS and a crashed tab, and both are recoverable
by clearing site data. On desktop the same content is decoded by the same
loaders and the same scripts run, inside a native process with the
engine's own file access.

So the policy is asymmetric, and inverted relative to intuition:

- **Browser:** install any listed item on a single confirmation. Show
  author, licence and size; do not interrogate the player about trust
  they have no way to evaluate. The browser is the *safest* place to pilot
  an open library.
- **Desktop:** the same list, but installation requires an explicit
  per-item confirmation naming author and licence, and `kind: campaign`
  additionally shows the player the `Exec` lines the campaign contains --
  or is refused outright, if [Open question 4](#open-questions) is settled
  that way.
- **Both:** `sha256` verified before anything is written; no exceptions,
  no "skip verification" option, no config flag. An unverified install
  path that exists will be the one that gets used.

### Licence and redistribution

The registry serves other people's work to third parties, which is
redistribution, which is a licence question and not an etiquette
question. The position:

- `licence` is **required** on every entry. There is no "unknown" value
  and an absent field fails CI. A distribution channel that cannot say
  under what terms it distributes is the liability that #32 warns about.
- The value is an SPDX identifier from an allowlist maintained in the
  registry repository, or the literal `custom` -- in which case
  `licenceUrl` becomes required and `licenceNote` is expected.
- An item may be listed only if its licence permits redistribution, **or**
  the submitter is the copyright holder and says so in the pull request.
  The PR is the record; that is the whole point of using one.
- `author` and `source` exist so a dispute can be adjudicated rather than
  argued. `source` is optional in the schema and requested in review for
  anything the submitter did not write.

There is an honest problem with this, and it should not be buried:
**almost none of the existing corpus is licensed.** `share/gamedir` holds
41 top-level directories, 33 of which are mods: 29 with a `script.lgs`,
and four Gusanos mods detected by an `objects/` directory (`Gusanos`,
`promode`, `Doom`, `telek`). Six of the 33 ship a readme of any kind, and
**not one ships a licence file.** The engine is GPL and `COPYING.LIB` is
the LGPL, but neither covers the content. So a strict rule applied to the
historical corpus would list almost nothing, which is a real cost, because
making that content reachable again is the point of the registry. The
recommended resolution is in [Open questions](#open-questions); it is a
maintainer decision and this document does not make it.

Who decides what gets listed: the registry repository's maintainers, via
`CODEOWNERS`, by merging or declining a pull request. Delisting is a pull
request that removes the manifest; the item disappears from `index.json`
on the next deploy and from every client within the `max-age=600` the
measurement above pins down. The blob may be left in place or deleted --
deleting it makes existing installs unrepairable but is the right response
to a takedown.

## Submission and moderation

The flow a maintainer has to actually operate, using only a repository, a
workflow and Pages.

1. **Submit.** A contributor opens a pull request adding
   `items/<id>.json`, the blob under `blobs/<xx>/<sha256>.<ext>`, and
   optionally a preview. A pull-request template asks for the licence
   basis and, where the submitter is not the author, the `source`.
2. **CI validates**, and everything here is mechanical, so review is
   about judgement rather than checking:
   - JSON parses; validates against `schema/index.schema.json`; every
     required field present and matching its constraint.
   - `id` is unique, well-formed, and not previously used by a different
     item; `installName` is well-formed, and is unique among items of the
     same `kind`, because two items cannot own one directory.
   - The blob exists at the path `url` resolves to; its byte length
     equals `size`; its SHA-256 equals `sha256`.
   - `licence` is in the allowlist, or is `custom` with a `licenceUrl`.
   - `requires` resolves entirely within the index, and the graph is
     acyclic.
   - For `format: "zip"`: the archive opens; every entry name is
     relative, contains no `..`, uses `/` separators, and lies under
     `<installName>/`; no entry is named `startup*.lua` at any
     archive root; no symlink or device entries; the extracted total
     matches `installSize` if given, and is under the size cap.
   - For `format: "file"` with `kind: "skin"`: the extension is `.png` or
     `.bmp`, and the image decodes.
   - For `kind: "campaign"`: the workflow greps the archive's `game.cfg`
     for `Exec` and prints every match into the PR, so it cannot be
     merged without a human having seen it.
3. **Review.** A maintainer reads the licence basis and, for a campaign,
   the `Exec` lines. One approving maintainer is enough; this is a
   volunteer project and a two-reviewer rule would mean a queue nobody
   drains.
4. **Merge and deploy.** Merging to the default branch triggers a Pages
   deploy that regenerates `index.json` from `items/` -- contributors
   never hand-edit `index.json`, and CI fails if a PR modifies it. Live
   within a build plus up to ten minutes of edge cache.
5. **Update an item.** A pull request that changes `version`, `url`,
   `size` and `sha256` in place. The old blob stays at its
   content-addressed path; nothing links to it.
6. **Takedown.** An issue template for third-party requests, and a pull
   request removing the manifest. Both leave a public record, which is
   the reason to prefer this over a database with an admin panel.

No server, no accounts beyond GitHub, no moderation queue to staff. The
cost is the one #32 already names: a contributor needs a GitHub account,
and there is no in-app upload. That trade is the decision, and it is the
right one until submission volume proves otherwise.

## Open questions

Each of these is a maintainer decision. A recommendation is given; none is
resolved anywhere else in this document.

1. **Where the registry repository lives, and who owns it.**
   Recommendation: a new repository in the upstream `openlierox`
   organisation, so the listing decision and any takedown obligation sit
   with the project rather than with a fork. If that is not available,
   the fork can host it and the client's default `registryBase` moves
   later -- which is cheap precisely because it is one configurable
   string.
2. **How strict the licence rule is for pre-existing unlicensed
   content.** This is the load-bearing one and it is partly a legal
   judgement. Recommendation: two tiers. New submissions require a real
   SPDX identifier. Historical content may be listed as
   `licence: "custom"` with `licenceNote` recording the provenance
   (author, original release thread, date) and an explicit maintainer
   decision in the PR, plus a documented takedown path. This lists the
   corpus while keeping the record that makes a takedown answerable. The
   stricter alternative -- no licence, no listing -- is defensible and
   would list almost nothing.
3. **A takedown contact.** The registry needs a published address for
   requests before it serves third-party work. Recommendation: an issue
   template plus one maintainer email in the repository README.
4. **Whether `Exec` is allowed in listed campaigns at all.**
   Recommendation: disallow it for registry-listed campaigns in schema 1,
   and revisit if a submission has a real need. Nothing in the current
   corpus needs it except the bundled introduction campaign, which ships
   in-repo and is not a registry item. Disallowing it removes the single
   largest item from [What is actually risky](#what-is-actually-risky) at
   no cost to any content that exists. The alternative is to allow it and
   rely on the CI grep plus review, which is what step 2 above describes.
5. **Whether blobs live in git or are staged by the workflow.**
   Recommendation: in git, under `blobs/`. History grows, but the corpus
   is small, and it buys an audit trail, free rollback and no dependency
   on an upstream URL still existing at deploy time. Revisit if the
   repository becomes unwieldy; the alternative is a workflow that
   downloads each blob from a submitter-provided URL and stages it into
   the Pages artifact, which keeps git small and makes a rebuild depend on
   thirty third-party hosts.
6. **Identifier namespacing.** Recommendation: flat slugs, uniqueness
   enforced by CI and review. `author/item` would prevent squabbles over
   a name but puts a `/` in a string used as a filesystem path, and the
   corpus is far too small for the problem it solves.
7. **The JSON parser dependency for the desktop client.**
   Recommendation: vendor one single-header parser and accept it as the
   cost of a format the browser reads for free. The alternative is an
   INI-shaped index read through the existing `ConfigHandler`
   ([include/ConfigHandler.h:37](../../include/ConfigHandler.h#L37)),
   which adds nothing to the C++ side but makes the shell parse INI in
   JavaScript and makes nested structures such as `requires` awkward. A
   third option is YAML, which *is* already linked
   ([CMakeOlxCommon.cmake:445](../../CMakeOlxCommon.cmake#L445)) and is
   used for touchscreen layouts -- but the browser has no YAML parser, so
   it moves the dependency rather than removing it. This belongs in #4,
   not here.

## What #4 can rely on

The short version, for the client implementer:

- One `GET` of `index.json`, `schema: 1`, over CORS from a Pages origin
  with `access-control-allow-origin: *`. Measured, not assumed.
- Required fields exactly as tabulated: `id`, `kind`, `installName`,
  `title`, `author`, `version`, `licence`, `format`, `url`, `size`,
  `sha256`. Unknown fields ignored; unknown `kind` or `format` skipped;
  a greater `schema` refused.
- Blobs from the same origin, at immutable content-addressed paths, so
  they can be cached in the persistent directory by hash and never
  refetched.
- SHA-256 verified before any write, which needs one new single-file
  dependency.
- Install targets per `kind` from the table in
  [Content kinds](#content-kinds), through the extractor #24 is factoring
  out, with the five rules in
  [Rules the extractor must enforce](#rules-the-extractor-must-enforce)
  -- of which rule 5 is not optional.

Not available, deliberately: version ordering, multiple versions per
item, ratings, search, in-app upload. Each is a schema bump or a service,
and each needs a motivating case the current corpus does not supply.
