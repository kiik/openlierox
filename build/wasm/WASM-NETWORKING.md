# OpenLieroX WebAssembly networking

How the browser build reaches real multiplayer. This is a sibling to
[WASM-PORT.md](WASM-PORT.md): that document explains why the port runs
single-player over HawkNL's loopback driver and stops there; this one
picks up at "LAN / internet networking — stubbed" and specifies the
transport that unstubs it.

It is a design record, not a how-to. It fixes the decision, the
architecture, and the integration seams precisely enough that a
different contributor can implement it without re-litigating the
choice. Where it names a `file:line`, that line is the seam the work
attaches to, verified against the tree at the time of writing.

## Status

| Area | State |
|---|---|
| Role of the browser | **Decided** — client only. A browser cannot host; see [Constraints](#constraints-that-are-not-negotiable) |
| Transport for P0 | **Decided** — relayed UDP through a gateway, over `wss://` |
| Transport mechanism | **Decided** — a custom bridge with its own framing. Emscripten sockfs was measured and cannot do it; see the [spike](#p0--spike-does-sockfs-already-do-this-answered-no) |
| Topology | **Decided** — the existing host-authoritative star; the gateway is a relay, not a peer |
| WebRTC DataChannels | **Deferred** — the upgrade that buys direct connect, not the first step |
| WebTransport | **Deferred** — the datagram upgrade to the same gateway |
| Scope | The `OpenUnreliable` game-transport path only |
| Loopback single-player | Unchanged; must stay green |
| Implementation | **Not started** — phased plan in [Phasing](#phasing) |

## The problem

OLX is a client/server game with an authoritative server. Single-player
is a local server and a local client inside one process, talking over
HawkNL's `NL_LOOP_BACK` driver
([src/common/Networking.cpp:322](../../src/common/Networking.cpp#L322)).
Real matches are UDP between machines.

Browsers cannot open raw UDP sockets, so `NL_IP` is not selected and the
port falls back to loopback. Loopback never leaves the process, so a
browser tab can only play itself. Everything below exists to give the
browser build a datagram path to a *remote* server while leaving the
loopback path exactly as it is for single-player.

Two facts about OLX make this tractable and shape every decision here:

- **OLX runs its own reliability layer.** [CChannel](../../include/CChannel.h)
  implements sequencing, acks, and reliable-message resends over raw
  unreliable UDP. The transport underneath does **not** need to be
  reliable or ordered — it needs to be a datagram pipe.
- **The game transport is one code path.** Every gameplay socket is
  opened through `NetworkSocket::OpenUnreliable`
  ([src/common/Networking.cpp:817](../../src/common/Networking.cpp#L817)).
  `OpenReliable` ([:796](../../src/common/Networking.cpp#L796)) is used
  only by IRC chat and the SMTP crash reporter — out of scope. The
  transport swap therefore lives behind one method, not scattered.

## Constraints that are not negotiable

These are measured properties of the browser platform and of this code
base, not preferences. Every design below has to live with them.

- **The browser cannot host, and it fails quietly rather than loudly.**
  Emscripten's socket layer emulates UDP as one WebSocket per peer.
  Binding a datagram socket is supposed to launch a listen server, which
  exists only under Node; in a browser that `listen` throws `EOPNOTSUPP`
  and `bind()` swallows it
  (`emsdk/upstream/emscripten/src/library_sockfs.js:458-463`).
  Note what that means: `bind()` itself *succeeds* — the same function
  has already recorded `saddr`/`sport`, and it returns normally — so a
  caller gets a socket that looks bound, and `getsockname` will answer.
  HawkNL binds unconditionally on open
  ([libs/hawknl/src/sock.c:881](../../libs/hawknl/src/sock.c#L881)), so
  it sails past this point and only discovers the problem much later.
  The recorded port is not inert either: it is what sockfs puts in the
  10-byte handshake it prepends to datagram peers, which is its own
  hazard — see the sockfs spike answering #27.
  WebRTC does not change the conclusion: an unaddressable peer still
  needs a signaling rendezvous before anyone can reach it. **The browser
  is a client. The authoritative server runs on the desktop or in the
  headless container.** Any phase that assumed browser-hosted play was
  wrong.
- **The wasm build has no way to express a remote address today.** With
  the loopback driver, an address *is* a port number:
  `loopback_AddrToString` always prints `127.0.0.1:%u` and
  `loopback_StringToAddr` parses only the part after the colon,
  discarding the host
  ([libs/hawknl/src/loopback.c:676-705](../../libs/hawknl/src/loopback.c#L676)).
  So the browser client cannot currently store, print, or parse the
  address of a server. Whatever transport wins, an address encoding plus
  a `StringToNetAddr`/`NetAddrToString` round trip is part of the work.
- **One socket, many destinations.** A HawkNL unreliable socket is not a
  connected pair; it multiplexes peers. The client repoints its single
  socket at the UDP masterserver, then at the server behind NAT, then at
  seven ports around the target
  (`p[] = {0, 2, 1, 3, 4, -1, -2}`,
  [src/client/CClient.cpp:1362-1372](../../src/client/CClient.cpp#L1362)),
  and `ServerList` repoints its socket per server while it pings. A
  transport that assumes "the client talks to exactly one peer" is
  wrong; per-destination addressing has to exist somewhere.
- **The reliable window is three packets.**
  `MAX_NON_ACKNOWLEDGED_PACKETS = 3`
  ([src/common/CChannel.cpp:353](../../src/common/CChannel.cpp#L353)).
  Reliable throughput is therefore about three packets per round trip,
  so added latency throttles reliable traffic directly rather than
  merely delaying it. Resend timeouts are adaptive, smoothed from
  measured ping ([:496](../../src/common/CChannel.cpp#L496)), so a slower
  path backs off rather than flooding — but a stalled path stalls the
  window.
- **A page served over `https://` can only open `wss://`.** The wasm
  bundle is published to GitHub Pages
  (`.github/workflows/master-build.yaml:177-200`), so any gateway needs
  a real hostname and certificate. There is no plaintext shortcut.
- **The single browser thread only yields at `CapFPS()`.** There are no
  reader threads. Inbound datagrams are queued by JS as they arrive and
  drained once per frame from `EventHandler::tickAll`
  ([src/common/Networking.cpp:705](../../src/common/Networking.cpp#L705)),
  invoked via `NetworkSocket::tickEventHandlersEmscripten`
  ([:729](../../src/common/Networking.cpp#L729)). Round-trip time is
  therefore quantized by frame time — about 16 ms at 60 fps, which is
  acceptable. Anything that blocks waiting for the network instead of
  returning to the frame loop freezes the tab.

## Decision

**P0 is a relay: the browser client speaks to a gateway over `wss://`,
and the gateway speaks ordinary UDP to an unmodified dedicated server.**

The gateway allocates one UDP socket per browser connection. The server
therefore sees N ordinary UDP peers with distinct source ports and needs
no changes at all — which matters, because the server demultiplexes
in-game packets by address *and* port, and HawkNL's address comparison
includes the port
([libs/hawknl/src/sock.c:1942](../../libs/hawknl/src/sock.c#L1942)), so
the challenge/anti-spoofing path
([src/server/ChallengeTable.cpp](../../src/server/ChallengeTable.cpp))
keeps working unmodified.

Why this before WebRTC:

- **No signaling service, no SDP, no ICE, no STUN, no TURN.** WebRTC
  needs all of it before the first packet moves.
- **Browser ↔ desktop cross-play falls out for free**, and given that
  the browser cannot host, cross-play is not a bonus — it is the only
  way a browser client ever joins a populated game.
- The dedicated server is already containerized
  ([docker/](../../docker/)), so the thing to connect to already exists.

What it costs, stated plainly: every packet is relayed, so the operator
pays bandwidth and adds a hop; and TCP underneath means a lost segment
delays everything behind it. See
[Alternatives](#alternatives-weighed) for why that is an acceptable P0
trade and [Phasing](#phasing) for how it stops being one.

## Alternatives weighed

| Option | Datagram? | Browser can host? | Needs signaling? | Verdict |
|---|---|---|---|---|
| **Relayed UDP over `wss://`** | No — TCP underneath, but message boundaries are preserved 1:1 | No | No | **Chosen for P0.** Smallest path to a real match; cross-play for free |
| WebTransport (HTTP/3 datagrams) to the same gateway | Yes | No | No | **Deferred to P3.** Same seam, removes the only real objection to P0; costs HTTP/3, a cert, and a browser-support check |
| WebRTC DataChannel (`ordered:false, maxRetransmits:0`) | Yes | Only with signaling and TURN | Yes | **Deferred to P4.** Buys direct connect and no relay bandwidth; the most expensive first step |
| Full mesh | — | — | — | Rejected. The server is authoritative; a star is correct |

An earlier draft of this document rejected a WebSocket game transport
outright, on the grounds that TCP retransmits fight CChannel's own
resends and that head-of-line blocking is exactly the failure mode a
shooter must avoid. Half of that argument does not survive contact with
the code: CChannel's resend timeout is adaptive, smoothed from measured
ping ([src/common/CChannel.cpp:496](../../src/common/CChannel.cpp#L496)),
so the two layers back off rather than amplify. Head-of-line blocking is
real, and with a three-packet reliable window it bites harder than the
draft realized — but it degrades play under loss instead of preventing
it, and it is fixed by swapping the same seam to WebTransport at P3.
Paying for signaling, ICE and TURN before a single packet has crossed the
wire is the worse trade.

One genuine hazard the draft did not name: OLX's send throttle
(`checkBandwidth` / `maxRateForClient`,
[src/server/CServer_Send.cpp:395](../../src/server/CServer_Send.cpp#L395))
measures bytes handed to the socket. Over a WebSocket those bytes sit in
`bufferedAmount` instead of being dropped, so the throttle silently
mismeasures under congestion and the queue grows without bound. The
transport must expose the buffered amount and refuse or drop sends above
a watermark, the way UDP would.

## Architecture

### Where the seam is

The swap happens at the `NetworkSocket` C++ layer, behind
`__EMSCRIPTEN__`. `NetworkSocket` already hides the socket
implementation behind `struct InternSocket`
([src/common/Networking.cpp:604](../../src/common/Networking.cpp#L604)),
which is where a relay handle lives instead of — or beside — an
`NLsocket`. Received datagrams are drained in `tickAll` and surfaced
through the existing `OnNewData` event, so the rest of the engine sees
no difference from a UDP socket going ready.

`Connect` ([:859](../../src/common/Networking.cpp#L859)) deliberately
rejects UDP; the game never calls it on the transport socket, so
connection setup must not be routed through it. `Listen`
([:882](../../src/common/Networking.cpp#L882)) has no browser meaning —
see [Constraints](#constraints-that-are-not-negotiable).

`WaitForSocketRead` / `WaitForSocketWrite` / `WaitForSocketReadOrWrite`
([src/common/Networking.cpp:1000-1050](../../src/common/Networking.cpp#L1000))
are blocking spinners and would be a problem in a browser, but they have
no callers anywhere in `src/`, `include/`, `libs/` or `tests/`. Leave
them alone; do not build on them.

### P0 — spike: does sockfs already do this? **Answered: no.**

This section asked whether Emscripten's own UDP-over-WebSocket emulation
made a bridge unnecessary — one WebSocket per `(addr, port)` peer created
on demand by `sendto`, a websockify-compatible handshake, and a target
URL configurable through `Module.websocket.url`. If stock `NL_IP` worked
against a websockify-style gateway there would have been no bridge to
write.

It does not, and the spike answering #27 settled it by measurement rather
than by reading. Two gaps are *independently* decisive, so neither can be
worked around by fixing the other:

1. **Stock `NL_IP` cannot send a single datagram from a tab.** HawkNL
   opens `PF_INET6` datagram sockets unconditionally
   ([sock.c:847](../../libs/hawknl/src/sock.c#L847)) and encodes IPv4
   peers as `::ffff:a.b.c.d`
   ([:1898](../../libs/hawknl/src/sock.c#L1898)). sockfs completes a
   prefix URL by string concatenation with no brackets
   (`library_sockfs.js:191-194`), producing `ws://::ffff:1.2.3.4:23400/`
   — an invalid URL. The constructor throws and it surfaces as
   `EHOSTUNREACH` (`:219-221`). Measured in headless Chromium:
   `sendto` to the mapped address returned `-1 errno=23` while an
   `AF_INET` control against the same port succeeded.
2. **The destination cannot be conveyed to a relay.** The peer URL is
   either derived from the destination — in which case the browser dials
   the game server itself and there is no relay in the path — or a fixed
   gateway URL, in which case the address and port survive only as a JS
   map key and never reach the wire. Two datagrams to *different*
   destinations produced two WebSocket handshakes with byte-identical
   requests. Exactly one of "reach a relay" and "name the destination"
   is expressible, never both.

So the transport is a bridge, not configuration, and the gateway needs
its **own** framing rather than websockify's. That is the shape assumed
by [If a bridge is needed](#if-a-bridge-is-needed) below, which is now
the live plan rather than the fallback.

Three secondary findings from the same spike, which the bridge has to
respect:

- **sockfs prepends a 10-byte handshake** to datagram peers —
  `ff ff ff ff 'p' 'o' 'r' 't' <hi> <lo>` (`library_sockfs.js:241-250`).
  Four leading `0xFF` is OLX's own connectionless marker
  ([CServer.cpp:756](../../src/server/CServer.cpp#L756) parses
  `readInt(4) == -1`), so a forwarded handshake is parsed as a malformed
  *game* packet rather than ignored. Even "just run websockify" would
  have needed a documented rule for those bytes.
- **Backpressure is invisible to C.** `bufferedAmount` grew to 2798 with
  both sends still reporting success, so the engine cannot see a
  congested socket.
- **`select()` is worse than assumed**: readiness on datagram sockets is
  reported as a constant and the timeout is ignored. Harmless for the
  per-frame non-blocking drain, but unusable as a signal — so the bridge
  must not depend on one.

`-sPROXY_POSIX_SOCKETS` was also weighed and rejected: it blocks on
`emscripten_futex_wait`, needs the COOP/COEP headers GitHub Pages will
not send, does not compose with `-sASYNCIFY=1`, and is an open proxy by
construction.

One question the spike deliberately left open, because it cannot change
the verdict: whether a resolved *hostname* escapes the malformed-URL form
in gap 1. Gap 2 rejects that form regardless.

### If a bridge is needed

Add a `--js-library` module the same way the port bridges other JS
subsystems, exposing send / receive / poll entry points, with a
per-frame drain from `tickAll` and framing that carries the destination
address per datagram — because the client repoints one socket across
many destinations. Inbound datagrams are tagged with their source so the
`Read` → `remoteAddress()` sequence returns the right peer; getting this
wrong shows up as "replies go to the wrong destination", which is
invisible with one peer and corrupting with two.

### The gateway

A small standalone service, deployed beside the dedicated server in
[docker/](../../docker/):

- Accepts `wss://` connections; terminates TLS at a reverse proxy.
- Allocates one UDP socket per connection, relays each WebSocket binary
  message as one datagram and each datagram back as one message.
- Drops the UDP socket when the WebSocket closes.

Two requirements that are not optional:

- **It must not accept a client-supplied host and port.** A relay that
  forwards to arbitrary destinations is an open UDP proxy and an
  amplifier. Clients name a server from an allowlist or registry; the
  gateway resolves it. Per-connection rate and byte caps on top.
- **Ban and identity model.** `CBanList` compares addresses with the
  port stripped
  ([src/server/CBanList.cpp:39](../../src/server/CBanList.cpp#L39)), and
  every browser player arrives from the gateway's IP — so banning one
  browser player bans all of them, and IP-to-country lookups become
  meaningless. Either bans key on address *and* port for gateway peers,
  or the gateway supplies a per-client identity the server can ban.
  Decide before the relay is reachable by strangers.

## Scope

**In scope:** the `OpenUnreliable` datagram path used by the server
([src/server/CServer.cpp:146](../../src/server/CServer.cpp#L146),
[:173](../../src/server/CServer.cpp#L173)) and the client
([src/client/CClient.cpp:325](../../src/client/CClient.cpp#L325),
[:328](../../src/client/CClient.cpp#L328),
[:2238](../../src/client/CClient.cpp#L2238)).

**Deliberately deferred inside the transport work:** the menu lobby and
server-list sockets, and the NAT-traversal path
([src/server/CServer_Parse.cpp:1842-1843](../../src/server/CServer_Parse.cpp#L1842)).
They go through the same `OpenUnreliable`, but they talk to the public
masterserver and to arbitrary peers, which a restricted relay will not
forward. P1 connects to a known server by address; browsing comes at P2.
NAT traversal is meaningless for a client that cannot be a peer.

**Out of scope:**

- `OpenReliable` — IRC chat, SMTP crash reporter. TCP, not gameplay.
- The desktop `NL_IP` path. Unchanged.

## Phasing

Each phase is independently testable and leaves the build shippable.

- **P0 — Spike.** Answer the sockfs question above and pick the address
  encoding. Output is a decision plus a throwaway branch, not a feature.
- **P1 — One browser client joins a real server.** Gateway service with
  TLS and an allowlist, the transport behind `__EMSCRIPTEN__`, drained
  from `tickAll`. Success = a browser tab plays a match on the headless
  dedicated container, alongside a desktop client. This is also where
  the send-watermark (`bufferedAmount`) behaviour lands.
- **P2 — Playable in practice.** The ban/identity model, a background-tab
  policy (a hidden tab's frame loop drops to roughly 1 Hz, which will
  trip connection timeouts — decide between keepalive tolerance and
  pause-and-reconnect, and document it), and a way to find a server from
  the browser without pasting an address.
- **P3 — WebTransport.** Same seam, real unreliable datagrams; removes
  head-of-line blocking. Gated on HTTP/3 plus a current browser-support
  check.
- **P4 — WebRTC direct connect.** What WebRTC actually buys once P1
  exists: no relay bandwidth and lower latency, at the price of
  signaling, ICE and a TURN fallback. The address encoding from P0 must
  be able to carry a peer form so this is additive.

## Testing

- **Keep loopback green.** Single-player over `NL_LOOP_BACK` must pass
  unchanged at every phase.
- **Test the gateway with the desktop harness first.**
  [tests/headless/](../../tests/headless/) already runs multiple
  instances over real UDP (`harness.py`, `test_network.py`). A native
  client pointed through the gateway at a dedicated server catches the
  sender-identity and window-stall bugs in seconds, in CI, with no
  browser involved. Two headless Chromes is the expensive way to find
  the same bugs.
- **Make the browser harness able to fail.** [run-headless.py](run-headless.py)
  currently prints console output and exits `0` unconditionally, and no
  workflow runs it — so "single-player still works" is not actually
  measured anywhere. Give it a nonzero exit on page exceptions and
  error-level log entries and put it in CI *before* relying on it as a
  regression canary.
- **Desktop unaffected.** `./tests/headless/run.sh` must still pass;
  nothing outside `__EMSCRIPTEN__` guards should change.

## Open questions

- ~~Does stock `NL_IP` work under Emscripten against a websockify-style
  gateway?~~ **Answered: no.** Measured in the #27 spike; see
  [P0](#p0--spike-does-sockfs-already-do-this-answered-no). The bridge
  and its own framing are now the plan.
- Address encoding: how a gateway-relayed server is written as text so
  connect-by-address and the server list round-trip it, with room for a
  future direct-peer form.
- Where the gateway is deployed and under what name, given the client
  lives on GitHub Pages and needs a certificate on the other end.
- Whether the public masterserver is reachable from the browser at all,
  or whether the web build gets its own server registry.
