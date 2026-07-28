# Spike: can Emscripten's sockfs carry OpenLieroX's UDP?

Answer to issue #27. This is the decision record for one question:

> Can Emscripten's WebSocket-backed socket emulation carry OpenLieroX's
> actual UDP usage pattern, or does it need a real proxy protocol?

**Verdict: a proxy protocol is required.** Stock `NL_IP` on stock sockfs
cannot send a single datagram from a browser, and even after that is
fixed there is no way to tell a relay where to forward. Issue #29 is a
bridge, not a configuration change; issue #28's browser-facing side is
free to use its own framing and does not need websockify compatibility.

Why this file and not an append to `WASM-NETWORKING.md`: that document
lives on an unmerged branch (PR #22, `build/wasm/WASM-NETWORKING.md`) and
does not exist on `develop`. Appending would either duplicate the file
onto `develop` or chain this PR behind that one. When #22 lands, its
"Open questions" entry *"Does stock `NL_IP` work under Emscripten against
a websockify-style gateway?"* should be replaced by a link to here.

Measured against emscripten 3.1.74
(`~/wasm-tools/emsdk/upstream/emscripten/emscripten-version.txt`), the
version this build uses. Line numbers in `library_sockfs.js`,
`library_syscall.js`, `library.js` and `settings.js` refer to that
version's `src/`.

## What OLX actually does on the wire

Established from the code, not from assumption.

**One socket, many destinations, re-targeted per send.** A HawkNL
unreliable socket is never `connect()`ed; `nlSetRemoteAddr` is a plain
`memcpy` into `sock->addressout`
([libs/hawknl/src/nl.c:1080](../../libs/hawknl/src/nl.c#L1080)) and every
write is a `sendto` to whatever that field currently holds
([libs/hawknl/src/sock.c:1777](../../libs/hawknl/src/sock.c#L1777)).
The client has exactly one gameplay socket
([include/CClient.h:315](../../include/CClient.h#L315)) and points it at
the UDP masterserver, then at seven ports around the target
(`static const int p[] = {0, 2, 1, 3, 4, -1, -2};`,
[src/client/CClient.cpp:1365](../../src/client/CClient.cpp#L1365)), then
at the server, and `CChannel::Transmit` re-points it before each send
([src/common/CChannel.cpp:208](../../src/common/CChannel.cpp#L208)).
The server uses one socket for all N clients
([src/server/CServer.cpp:896](../../src/server/CServer.cpp#L896)).
So connectionless `sendto` to multiple peers is not an edge case; it is
the normal path.

**Non-blocking reads, drained once per frame.** Sockets are non-blocking
by default and OLX never changes that
([libs/hawknl/src/nl.c:44](../../libs/hawknl/src/nl.c#L44)). Under
Emscripten the tick polls with timeout 0
(`nlPollGroup(data->nlGroup, NL_READ_STATUS, &s, 1, 0)`,
[src/common/Networking.cpp:717](../../src/common/Networking.cpp#L717),
driven from
[src/common/Networking.cpp:729](../../src/common/Networking.cpp#L729)),
and the actual receives happen unconditionally per frame in
`ReadPackets` ([src/game/Game.cpp:823](../../src/game/Game.cpp#L823),
[:834](../../src/game/Game.cpp#L834)). Nothing subscribes to `OnNewData`;
the poll only wakes the event loop. Nothing blocks on the network.

**The client binds a fixed port.** `OpenUnreliable(tLXOptions->iNetworkPort)`,
default `LX_PORT = 23400`
([src/client/CClient.cpp:325](../../src/client/CClient.cpp#L325),
[include/Consts.h:35](../../include/Consts.h#L35)), falling back to port 0.
HawkNL always `bind()`s a UDP socket even for port 0 — only TCP skips it
([libs/hawknl/src/sock.c:881](../../libs/hawknl/src/sock.c#L881)).
This matters because sockfs only injects its handshake datagram on
*bound* sockets; see gap 3.

**The source address of every received datagram is load-bearing on the
server.** `remoteAddress()` returns the `recvfrom` out-parameter
([libs/hawknl/src/sock.c:1524](../../libs/hawknl/src/sock.c#L1524) via
[libs/hawknl/src/nl.c:1041](../../libs/hawknl/src/nl.c#L1041)), and the
server demultiplexes clients by nothing else:

```
NetworkAddr addrFrom = sock->remoteAddress();          // CServer.cpp:743
if(!AreNetAddrEqual(addrFrom, cl->getChannel()->getAddress())) continue;  // :782
if (GetNetAddrPort(addrFrom) != GetNetAddrPort(cl->getChannel()->getAddress())) continue;  // :787
```

There is no connection id in the packet. `AreNetAddrEqual` compares
family, the 16 address bytes *and* the port
([libs/hawknl/src/sock.c:1942](../../libs/hawknl/src/sock.c#L1942)).
Challenges are keyed by source address and consumed on use
([src/server/ChallengeTable.cpp:45](../../src/server/ChallengeTable.cpp#L45)),
and the channel's remote address is fixed from the connect packet's
source ([src/server/CServer_Parse.cpp:1291](../../src/server/CServer_Parse.cpp#L1291)).
The client, by contrast, verifies nothing and will *adopt* a source
address it did not expect
([src/client/CClient_Parse.cpp:449](../../src/client/CClient_Parse.cpp#L449)).

**Datagram boundaries are the only outer framing.** `CBytestream::Read`
clears its buffer and takes exactly one datagram per call:

```
size_t CBytestream::Read(NetworkSocket* sock) {
	Clear();
	char buf[4096];
	int res = sock->Read(buf, sizeof(buf));
```

([src/common/CBytestream.cpp:693](../../src/common/CBytestream.cpp#L693)).
There is no length prefix on the wire; the read loops terminate on a
zero-length read
([src/server/CServer.cpp:732](../../src/server/CServer.cpp#L732),
[src/client/CClient.cpp:1006](../../src/client/CClient.cpp#L1006)).
Anything over 4096 bytes is silently truncated. Connectionless packets
are recognised by four leading `0xFF`
([src/server/CServer.cpp:756](../../src/server/CServer.cpp#L756),
[src/client/CClient.cpp:1021](../../src/client/CClient.cpp#L1021)) — which
collides with sockfs's handshake; see gap 3.

**Loss and reorder are tolerated by design.** CChannel sequences, acks
and resends over raw UDP: `MAX_PACKET_SIZE = 512`
([src/common/CChannel.cpp:33](../../src/common/CChannel.cpp#L33)),
`MAX_NON_ACKNOWLEDGED_PACKETS = 3`
([:353](../../src/common/CChannel.cpp#L353)), out-of-order packets
buffered in `ReliableIn` and only released once the gap closes
([:524](../../src/common/CChannel.cpp#L524),
[:538](../../src/common/CChannel.cpp#L538)), duplicates dropped
([:521](../../src/common/CChannel.cpp#L521)), resend timeout smoothed
from measured ping
([:496](../../src/common/CChannel.cpp#L496)). `CUdpFileDownloader` has no
sequencing of its own — 1-byte length plus up to `MAX_DATA_CHUNK = 254`
bytes, appended in arrival order
([src/common/FileDownload.cpp:594](../../src/common/FileDownload.cpp#L594),
[:632](../../src/common/FileDownload.cpp#L632)) — but it rides on
CChannel's reliable stream
([src/server/CServer_Send.cpp:53](../../src/server/CServer_Send.cpp#L53)),
so ordering is already provided.

So the transport needs exactly four things: per-datagram `sendto` with a
caller-chosen destination that changes as often as per send; preserved
datagram boundaries; the source address *and port* of each received
datagram surfaced to the app; and nothing else — reliability and
ordering are OLX's own job.

## What Emscripten's emulation provides

sockfs emulates a UDP socket as a map of peers keyed by `addr + ':' + port`
(`library_sockfs.js:255`, `:258`), each peer holding one WebSocket created
on demand by `sendmsg` (`:639`). Inbound messages are tagged with the
peer they arrived on and pushed to a per-socket queue (`:324`), which
`recvmsg` pops one entry at a time (`:667`).

Three things it gets right for OLX:

- **Per-destination demultiplexing works at the C API level.** One socket,
  many peers, and `recvfrom` reports the correct per-peer source address
  and port (`library_syscall.js:404` writes `msg.addr` / `msg.port`).
  Measured below: two destinations on one socket round-tripped with their
  own source addresses intact.
- **Datagram boundaries are preserved 1:1.** `recvmsg` truncates to the
  caller's buffer and discards the remainder; the put-back path is
  `SOCK_STREAM`-only (`library_sockfs.js:703`). That is real UDP
  semantics, and it is what `CBytestream::Read` needs.
- **`bind()` succeeds in a browser.** It records `saddr`/`sport` and only
  the listen server is skipped, with `EOPNOTSUPP` swallowed
  (`library_sockfs.js:441-464`). Note this corrects the framing in #27
  and in `WASM-NETWORKING.md`: the browser cannot *host*, but `bind()`
  itself returns 0 and `getsockname()` reports the bound port. A browser
  build will open a socket happily and then simply never receive anything
  unsolicited.

And five things it does not.

### Gap 1 — no way to convey the UDP destination to a relay (decisive)

The WebSocket URL for each peer comes from one global config string,
`SOCKFS.websocketArgs['url']` (`library_sockfs.js:180`), in one of two
forms (`settings.js:400-406`):

- **Prefix form** (`ws://` or `wss://`, the default). The URL is completed
  from the datagram's destination:
  `url = url + parts[0] + ":" + port + "/"` (`library_sockfs.js:191-194`).
  The browser therefore connects *directly to the game server's own host
  and port*. There is no relay in this picture at all — it requires a
  websockify listening for WebSocket traffic on the exact host and port
  the game believes it is addressing, per server, with a TLS certificate
  valid for that host. And it does not work anyway; see gap 2.
- **Full-URL form.** The completion branch is skipped and the URL is used
  verbatim, so `addr` and `port` survive only as the peer-map key and
  never reach the wire.

Measured, in a real browser: two datagrams from one socket to
`10.0.0.7:23400` and `10.0.0.8:23401`, with a full gateway URL
configured, produced two separate WebSocket connections whose HTTP
requests were byte-identical —

```
[relay] accept #2 from 127.0.0.1:33376
[conn 2] request-line: GET /gateway HTTP/1.1
[conn 2] host: 127.0.0.1:8081  subprotocol: 'binary'
[relay] accept #3 from 127.0.0.1:33378
[conn 3] request-line: GET /gateway HTTP/1.1
[conn 3] host: 127.0.0.1:8081  subprotocol: 'binary'
```

— and then carried the two payloads with no destination marker of any
kind:

```
[conn 2] frame #2 binary len=13 ... ascii="hello-C-dest1"
[conn 3] frame #2 binary len=13 ... ascii="hello-C-dest2"
```

So exactly one of "reach a relay" and "tell the relay where to forward"
is expressible, never both. This is the requirement that forces a proxy
protocol: **the destination has to be named by the client somewhere the
relay can read it**, and stock sockfs cannot put it there.

One workaround exists and should be recorded as rejected: because the
URL is re-read from `SOCKFS.websocketArgs` at each `createPeer`, the
engine could rewrite it via `EM_ASM` immediately before the first send to
each new destination, encoding the destination in the path. That depends
on peer-creation timing inside library internals, silently mis-routes if
the ordering assumption ever breaks, and mis-routing is corrupting rather
than merely broken — a datagram delivered to the wrong server. Not worth
it when a `--js-library` bridge is honest about the same information.

### Gap 2 — HawkNL's sockets are `AF_INET6`; sockfs cannot build a URL from one (decisive)

HawkNL creates `PF_INET6` datagram sockets unconditionally
(`realsocket = socket(PF_INET6, SOCK_DGRAM, IPPROTO_UDP);`,
[libs/hawknl/src/sock.c:847](../../libs/hawknl/src/sock.c#L847)) and
encodes IPv4 as IPv4-mapped
([libs/hawknl/src/sock.c:1898](../../libs/hawknl/src/sock.c#L1898)).
Emscripten reads such a `sockaddr_in6` correctly and renders the address
as `::ffff:1.2.3.4` (`library.js:851-861`, `library.js:785-788`), then
concatenates it into a URL without brackets (`library_sockfs.js:193`),
producing `ws://::ffff:1.2.3.4:23400/`. That is not a valid URL. The
`WebSocket` constructor throws, the throw is caught, and it surfaces as
`EHOSTUNREACH` (`library_sockfs.js:219-221`).

Measured in headless Chromium, and identically under Node:

```
-- A: HawkNL-style AF_INET6 socket, default 'ws://' prefix URL --
  socket(PF_INET6, SOCK_DGRAM) = 3
  bind(port=0) OK; getsockname -> family=10 port=0
  sendto(AF_INET6 ::ffff:127.0.0.1 port 8081, 7 bytes) = -1 errno=23 (Host is unreachable)
  [peers] count=0 saddr=:: sport=0 recv_queue=0

-- B: AF_INET socket, default 'ws://' prefix URL --
  sendto(AF_INET 127.0.0.1 port 8081, 7 bytes) = 7 errno=0 (-)
  [peers]   key=127.0.0.1:8081 url=ws://127.0.0.1:8081/ readyState=0 queued=2
```

The `AF_INET` control succeeds against the same relay on the same port,
so the failure is the address family, not the environment. Confirmed
independently at the library level:

```
$ node -e "const W=require('ws'); new W('ws://::ffff:127.0.0.1:8081/')"
node ws REJECTED: SyntaxError Invalid URL: ws://::ffff:127.0.0.1:8081/
```

So **selecting `NL_IP` under Emscripten as-is cannot send one datagram.**
This answers #27's first checklist item without an engine build: the join
does not get anywhere, because `nlWrite` fails at the first `sendto`. It
is not fixable by configuration. The full-URL form sidesteps it — the
address never reaches the URL builder — but that is exactly the form that
loses the destination (gap 1). Patching HawkNL to `AF_INET` under
`__EMSCRIPTEN__` would fix this gap and not gap 1, so it does not change
the verdict.

### Gap 3 — sockfs injects a handshake datagram into the payload stream

For any bound datagram socket, sockfs queues a 10-byte message ahead of
the application's data on every new peer connection:
`ff ff ff ff 'p' 'o' 'r' 't' <hi> <lo>`, announcing the bound port
(`library_sockfs.js:241-250`). Its own receive side strips it
(`:312-322`); nothing else does.

OLX always binds (gap facts above), so this always fires. Measured on the
wire, with the client bound to `LX_PORT` the way `CClient.cpp:325` binds
it:

```
[conn 1] frame #1 binary len=10 EMSCRIPTEN PORT-HANDSHAKE (announced bound port 23400)
[conn 1] frame #2 binary len=7 first32=68656c6c6f2d42 ascii="hello-B"
```

Two consequences. First, the announced port is `23400` for *every*
browser client — it is the client's own configured port, not an identity,
so it is useless for the per-client distinction #30 needs. Second, if a
relay forwards those 10 bytes as a datagram, OLX does not ignore them: a
datagram starting with four `0xFF` is precisely its connectionless-packet
marker (`CServer.cpp:756`, `CClient.cpp:1021`), so it is parsed as a
malformed connectionless packet rather than discarded. A relay must
therefore know about and strip this message — which means even the
"just run websockify" option already requires a documented proxy
protocol. It is not a neutral pipe.

### Gap 4 — no backpressure signal

`sendmsg` returns `length` unconditionally, both when it sends and when
it queues, and never looks at `bufferedAmount`
(`library_sockfs.js:646`, `:655`). Measured in the browser after two
1399-byte sends that both reported success:

```
  sendto(AF_INET 127.0.0.1 port 8081, 1399 bytes) = 1399 errno=0 (-)
  sendto(AF_INET 127.0.0.1 port 8081, 1399 bytes) = 1399 errno=0 (-)
  [peers]   key=127.0.0.1:8081 ... queued=0 bufferedAmount=2798
```

`bufferedAmount` is visible in JS and invisible to C. OLX's throttle
compares a rate it derives from bytes handed to the socket against a cap
(`cl->getChannel()->getOutgoingRate() > maxRateForClient(cl)`,
[src/server/CServer_Send.cpp:725](../../src/server/CServer_Send.cpp#L725),
cap at [:396](../../src/server/CServer_Send.cpp#L396)),
so under congestion it would measure a queue it cannot see. #29 already
lists this; the spike confirms the number exists and that stock sockfs
does not expose it. A bridge can.

### Gap 5 — readiness is a constant, not a signal

For a datagram socket sockfs computes `dest = null` and then treats
"connection-less sockets are always ready" literally, so `POLLIN` is set
whether or not anything is queued (`library_sockfs.js:376-388`), and
`select()`'s timeout is documented as ignored for SOCKFS
(`library_syscall.js:537`). Measured on an empty socket, returning
instantly despite a 500 ms timeout:

```
  [ready:A-empty] poll()=1 revents=0x5 (POLLIN=1 POLLOUT=1) select(rd,500ms)=1 isset=1
```

This is the checklist item from #27 about HawkNL's `select()` calls at
[libs/hawknl/src/sock.c:1331](../../libs/hawknl/src/sock.c#L1331) and
[:1453](../../libs/hawknl/src/sock.c#L1453). The answer is **harmless
here, but unusable as a signal.** Harmless because OLX's reads are
non-blocking and unconditional per frame, and because the one poll it
does run already passes timeout 0 and only pushes an event nobody
consumes. Unusable because any future code that reads `select()` as
"there is data" will be wrong; the only way to know is to read and get
`EAGAIN`, which the probe also confirms:

```
  [drain:A] recvfrom = -1 errno=6 (Resource temporarily unavailable)
```

### Not a gap, but a trap worth writing down

`Module.websocket` cannot be configured after startup. SOCKFS captures
`Module['websocket']` at mount time (`library_sockfs.js:27`); if the
application did not supply one, it captures a fresh `{}` and line 31 then
creates a *different* object on `Module`. Measured:

```
  [cfg] Module.websocket === SOCKFS.websocketArgs ? false  (Module.websocket keys: ["on"])
  [cfg] after setting Module.websocket.url late, SOCKFS.websocketArgs.url = undefined
```

So the gateway URL must be set in the pre-js or shell **before** the
runtime starts. Relevant to #28's item about exposing the gateway URL
through `build/wasm/shell/shell.html`, whichever transport wins.

### `-sPROXY_POSIX_SOCKETS` is not an option either

The other emulation route replaces the socket syscalls with a synchronous
RPC bridge to a native `websocket_to_posix_proxy` process
(`tools/system_libs.py:2396` selects `libsockets_proxy`;
`system/lib/websocket/websocket_to_posix_socket.c`). Each call blocks the
caller on a futex until the reply arrives:

```
  while (!b->operationCompleted) {
    emscripten_futex_wait(&b->operationCompleted, 0, 1e9);
  }
```

(`system/lib/websocket/websocket_to_posix_socket.c:148-149`). That needs
pthreads and `SharedArrayBuffer`, cannot run on the main browser thread,
and therefore needs cross-origin isolation (COOP/COEP) headers — which
GitHub Pages, where #28 notes the client is published, does not send. It
also does not compose with this build's `-sASYNCIFY=1`
(`build/wasm/CMakeLists.txt:312`), which is the mechanism the port uses
to yield instead of block. And on the far side it performs arbitrary real
socket syscalls on the host, so it is an open proxy by construction.
Rejected on all three counts.

## Verdict

**Emulation is insufficient. A proxy protocol is required.**

Gaps 1 and 2 are each independently decisive: without a fix for 2 no
datagram leaves the tab, and without a fix for 1 no relay can route the
datagram that does. Gap 3 means even the minimal "run websockify" answer
needs a documented wire rule. Gaps 4 and 5 are quality-of-implementation
problems that a bridge fixes for free and sockfs cannot.

The good news is that the *shape* sockfs presents to C is right — one
socket, many peers, boundaries preserved, per-peer source address
reported — which is evidence that the model OLX needs is implementable in
a browser. It is the wire format, not the abstraction, that is
unavailable.

## What this changes for #28 and #29

**#28 — relay gateway.** Its "Depends on" question is answered: the
browser-facing side **can and must use its own framing**, not
websockify's. Concretely:

- Drop websockify compatibility from the scope. It buys nothing, because
  #29 has to write a bridge regardless, and the 10-byte port handshake
  (gap 3) means websockify was never drop-in anyway.
- The destination must be named by the client at connection setup — URL
  path or query, resolved against an allowlist — because there is no way
  to derive it from anything sockfs sends. #28's existing "no
  client-supplied host and port" requirement survives intact and is now
  the *only* workable form, not merely the safer one.
- One UDP socket per WebSocket connection remains correct and unchanged,
  and so does the plan to verify it with `tests/headless/` before a
  browser is involved.
- Setting the gateway URL in `shell.html` must happen before runtime
  start, per the trap above.

**#29 — browser transport.** The "configure `NL_IP`" branch is dead;
budget the bridge. Two further notes:

- Do not build on sockfs at all, even for the parts that work. A
  `--js-library` bridge over **one** WebSocket to the relay, with an
  explicit per-datagram destination header, is less code than steering
  sockfs's URL per peer, and it can expose `bufferedAmount` for the
  watermark that #29 already requires (gap 4). sockfs would also open one
  WebSocket per destination, so one join that touches the masterserver
  and seven NAT ports (`CClient.cpp:1365`) would open nine WebSocket
  connections and consume nine relay UDP sockets.
- The address-encoding item gets easier than #29 assumes. The port-only
  encoding it worries about is the *loopback* driver's
  ([libs/hawknl/src/loopback.c:676](../../libs/hawknl/src/loopback.c#L676));
  HawkNL's IP driver already round-trips `a.b.c.d:port` text
  ([libs/hawknl/src/sock.c:1848](../../libs/hawknl/src/sock.c#L1848)).
  So if the bridge is added as a **third HawkNL driver** alongside
  `NL_IP` and `NL_LOOP_BACK`, selected under `__EMSCRIPTEN__` in place of
  the current unconditional `nlSelectNetwork(NL_LOOP_BACK)`
  ([src/common/Networking.cpp:322](../../src/common/Networking.cpp#L322)),
  then address parsing, printing, comparison and the poll-group plumbing
  all come along for free, and no new text encoding is needed for P0.
  Putting the bridge at `NetworkSocket::InternSocket`
  ([src/common/Networking.cpp:604](../../src/common/Networking.cpp#L604))
  instead — the seam `WASM-NETWORKING.md` names — leaves
  `StringToNetAddr`/`NetAddrToString`/`AreNetAddrEqual` still routed
  through the loopback driver and its port-only addresses, so that
  address work would have to be redone by hand. This is a
  recommendation from reading both drivers, not something the spike
  measured; it should be sanity-checked at the start of #29 rather than
  taken on faith.

**#30 is unaffected in substance,** but note that sockfs's announced port
(gap 3) is not a usable identity even if a websockify-style path were
revived — it is the client's own `Network.Port`, identical across
clients. #30's second option, a gateway-supplied identity, is the only
one that carries information the server does not already have.

## What was actually run

A standalone probe, deliberately not part of the engine build. It lives
outside the repository, in this session's scratch directory
(`.../scratchpad/spike/`), and is not committed anywhere:

- `probe.c` — opens datagram sockets the way `sock_Open` does
  (`PF_INET6`, `SOCK_DGRAM`, always bound), sends to IPv4-mapped
  destinations, inspects `SOCKFS` internals via `EM_ASM`, and drains with
  `recvfrom`.
- `wsrelay.py` — a ~180-line RFC 6455 server that logs every frame it
  receives verbatim and echoes a prefix back. Not a relay; an observer.
- `serve.py`, `probe.html` — static hosting plus a `POST /log` sink, so
  the browser run's stdout is captured deterministically instead of
  scraped from console output.

Built with the emsdk this repo fetches, at the same `-sASYNCIFY=1` the
real build uses:

```
emcc probe.c -o probe_web.js -sASYNCIFY=1 -sENVIRONMENT=web \
     -sASSERTIONS=1 -sEXPORTED_RUNTIME_METHODS=out,UTF8ToString -sEXIT_RUNTIME=0
emcc probe.c -o probe_node.js -sASYNCIFY=1 -sENVIRONMENT=node \
     -sASSERTIONS=1 -sEXPORTED_RUNTIME_METHODS=out,UTF8ToString
```

Run in headless Chromium (the target environment) and, as a cross-check,
in the emsdk's Node. Every quoted block above is verbatim probe or relay
output. The two environments agreed on every finding except `bind()`,
where Node really does start a listen server and the browser does not —
which is the one difference that does not matter here, since the browser
cannot host either way.

No engine build was needed, and none was made; the ~6-minute engine link
would not have added evidence, because `nlWrite` fails at the first
`sendto` (gap 2) for reasons visible in 40 lines of C.

## Where this is uncertain

Stated plainly, because a confident wrong answer here costs #28 and #29 a
rewrite.

- **Not measured: whether a hostname rescues the prefix form.** Emscripten
  maps unresolvable names to fake `172.29.x.y` addresses and reverse-maps
  them on the way into `sendto` (`library.js:914-949`,
  `library_syscall.js:325`), so an `AF_INET` socket addressing a resolved
  name does produce `ws://thename:port/`. My expectation is that this does
  *not* help HawkNL, because its address would be `::ffff:172.29.0.1`,
  which is not a key in `DNS.address_map.names`, so the reverse lookup
  misses and the URL stays malformed. **That is inference, not
  measurement.** The experiment that settles it is one more probe case:
  `getaddrinfo` a name with `AF_INET6`, `sendto` the resulting
  `sockaddr_in6`, and check whether the peer's URL comes out as
  `ws://thename:port/`. Ten minutes. It would only revive the
  per-server-websockify topology, which gap 1 rejects on other grounds, so
  it does not threaten the verdict — but it would change what "stock
  `NL_IP` is broken" means precisely.
- **Not measured: a real join.** #27 asked how far a join gets against a
  native dedicated server. The spike answers the underlying question
  instead, at the syscall layer, and infers that the join cannot start.
  If anyone wants the end-to-end negative confirmed, it needs the engine
  build plus `nlSelectNetwork(NL_IP)` on a throwaway branch.
- **Not evaluated: the cost of patching HawkNL to `AF_INET`** under
  `__EMSCRIPTEN__`. It would fix gap 2 only, so it cannot change the
  verdict, but it might be a component of the bridge if the bridge is
  built as a HawkNL driver.
- **Not measured: latency, background-tab behaviour, or throughput.**
  Out of scope for a yes/no, and all of it belongs to #29 once a
  transport exists.
- **The driver-vs-`InternSocket` recommendation for #29 is a reading of
  the code, not an experiment.** Treat it as a starting hypothesis.
