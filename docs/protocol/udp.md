# UDP — Probe Protocol (Deep Dive)

**Sources:** `srcs/socket.c`, `srcs/send.c`, `srcs/recv.c`, `srcs/resolve.c`  
**Related header:** `includes/ft_traceroute.h` (`<netinet/udp.h>`, `DEFAULT_PORT`, `PAYLOAD_SIZE`)  
**Related docs:** [networking.md](../modules/networking.md), [probing.md](../modules/probing.md), [hops.md](hops.md), [output.md](../modules/output.md)

## Overview

`ft_traceroute` discovers the path to a remote IPv4 host by sending
**UDP** (User Datagram Protocol) datagrams with a controlled IP TTL.
Intermediate routers and the destination answer with **ICMP** errors.
This document is the detailed UDP reference: protocol properties, why
classic traceroute chose UDP, exact on-wire layout, how this codebase
builds and identifies probes, and the edge cases that produce `*`.

```
┌──────────────────┐   UDP probe, TTL = n    ┌──────────────────┐
│  ft_traceroute   │ ──────────────────────► │  router / host   │
│  send_sock       │                         └────────┬─────────┘
│  SOCK_DGRAM/UDP  │                                  │
└────────▲─────────┘                                  │
         │     ICMP Time Exceeded (type 11)           │
         │  or Dest Unreachable / Port Unreach (3/3)  │
         └────────────────────────────────────────────┘
              recv_sock  SOCK_RAW / IPPROTO_ICMP
```

Outbound traffic is always UDP. Inbound control traffic is always ICMP.
Those two directions use two different sockets; see
[networking.md](../modules/networking.md).

---

## What is UDP?

UDP (RFC 768) is a minimal transport protocol layered on IP. It adds
ports and an optional checksum; it does **not** add reliability,
ordering, congestion control, or a connection handshake.

### Properties that matter for traceroute

| Property | Protocol meaning | Effect in this project |
|----------|------------------|------------------------|
| Connectionless | No SYN/ACK; peer state is optional | One `sendto()` = one probe |
| Unreliable | No ACK, no retransmit | Lost probes → `*`; expected |
| Message-oriented | Each `sendto` is one datagram | Packet size is fixed and known |
| Port demux | 16-bit src + dest ports | Dest port uniquely IDs each probe |
| 8-byte header | src, dest, length, checksum | Fits entirely in ICMP error quotes |
| IP protocol 17 | `IPPROTO_UDP` | Checked when parsing ICMP quotes |

### UDP vs TCP vs ICMP-echo (probe modes)

Classic traceroute and this subject use **UDP probes**. Other tools
offer alternatives; this project does not implement them.

| Mode | Outbound | "Reached" signal | Notes |
|------|----------|------------------|-------|
| **UDP** (this project) | UDP to high port | ICMP Port Unreachable | Kernel builds UDP; simple |
| TCP traceroute | TCP SYN to port 80/443 | SYN-ACK or RST | Often passes firewalls better |
| ICMP traceroute | ICMP Echo Request | Echo Reply | Same family as `ping` |

UDP was chosen historically (Van Jacobson's traceroute) because:

1. It needs no privileged raw send socket for the probe itself — a
   normal datagram socket is enough; only receiving ICMP needs raw.
2. An unused high port almost always triggers Port Unreachable at the
   destination without talking to a real service.
3. The full 8-byte UDP header is small enough that ICMP errors are
   required (by RFC) to quote at least IP header + 8 bytes of the
   original datagram — exactly one UDP header.

---

## Why UDP to a high port?

Default base destination port: **33434** (`DEFAULT_PORT`).

### Intermediate hops (TTL expires)

Routers decrement TTL and, when it hits 0, discard the packet and
should emit **ICMP Time Exceeded**. They do not need a UDP listener.
The transport protocol of the dropped packet does not change this
behavior. UDP, TCP, or ICMP payloads all provoke Time Exceeded the
same way.

### Final hop (destination reached)

When TTL is large enough for the UDP datagram to arrive at the target:

1. The IP stack delivers it to the UDP demux layer.
2. Nothing is bound to that high port (almost always).
3. The host generates **ICMP Destination Unreachable, code 3 — Port
   Unreachable**.
4. That ICMP is sourced from the destination's own IP.

Port Unreachable is therefore the **success** signal for UDP
traceroute, not a tool failure. See [output.md](../modules/output.md) — code 3
prints no `!` annotation; `check_reached()` in `main.c` stops the loop.

### Why not a well-known port?

Sending to port 53 (DNS), 123 (NTP), etc. risks:

- a real application answering (or silently accepting) the datagram;
- no Port Unreachable, so traceroute never knows it reached the host;
- looking like traffic to a live service (undesirable for a path probe).

33434 and the following range were chosen as historically unused
ephemeral-ish ports. With `-p` the operator can pick any base in
1–65535; the uniqueness rule still applies.

### Why unique ports per probe?

ICMP errors quote the original UDP header. Matching the quoted
**destination port** maps a reply to exactly one outstanding probe
without embedding a magic ID in the payload and without relying on
ICMP Echo identifier/sequence fields (those belong to ping-style ICMP
probes, not to this design).

---

## On-wire packet layout

### Full IPv4 + UDP probe (what leaves the host)

```
 0                   1                   2                   3
 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|Version|  IHL  |Type of Service|          Total Length         |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|         Identification        |Flags|      Fragment Offset    |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|  Time to Live |   Protocol=17 |         Header Checksum       |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                       Source Address                          |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                    Destination Address                        |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|          Source Port          |       Destination Port        |  <- UDP
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|             Length            |            Checksum           |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                                                               |
|                     32-byte zero payload                      |
|                                                               |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
```

### Size accounting

| Piece | Bytes | Constant / note |
|-------|-------|-----------------|
| IPv4 header (no options) | 20 | Assumed; IHL = 5 |
| UDP header | 8 | Filled by kernel |
| Payload | 32 | `PAYLOAD_SIZE` |
| **Total** | **60** | `DEFAULT_PACKET_SIZE` |

```
60 = 20 + 8 + 32
```

`tr->packet_size` is initialized to 60 and printed in the header line
("60 byte packets"). The send path always transmits 32 payload bytes;
there is no CLI to change payload size in the current code.

### UDP header field reference

| Offset | Size | Field | Who fills it | Used by this project? |
|--------|------|-------|--------------|------------------------|
| 0 | 2 | Source port | Kernel (ephemeral) | No — not matched |
| 2 | 2 | Destination port | Userspace via `sin_port` | **Yes — probe identity** |
| 4 | 2 | Length | Kernel (`8 + 32 = 40`) | No |
| 6 | 2 | Checksum | Kernel (IPv4: may be 0 if off) | No |

Userspace only controls the destination port (and the payload bytes).
Source port, length, and checksum are left to the kernel on a
`SOCK_DGRAM` socket.

### Payload

```c
char	payload[PAYLOAD_SIZE];   /* 32 */

memset(payload, 0, sizeof(payload));
sendto(tr->send_sock, payload, sizeof(payload), 0, ...);
```

- Content is unused for path discovery or matching.
- Some traceroute implementations put timestamps or sequence bytes in
  the payload; this one does not.
- Non-zero payload would not change ICMP matching, which only needs the
  UDP header.

---

## Layering: what userspace does vs the kernel

```
userspace                          kernel
─────────                          ──────
choose dest IP + dest UDP port
choose payload (zeros)
setsockopt(IP_TTL)
sendto(payload only)
                                   build UDP header
                                   build IPv4 header (TTL, proto 17, …)
                                   transmit

                                   receive ICMP error
recvfrom(raw ICMP socket)
parse outer IP + ICMP + quoted IP + quoted UDP
match dest port
```

Because outbound probes use `SOCK_DGRAM` / `IPPROTO_UDP`, the program
never crafts an IP or UDP header on send. TTL is the only IP field
explicitly set from userspace (`IP_TTL`). That keeps the send path
short and within the subject's allowed APIs.

---

## Send socket

**File:** `srcs/socket.c`

```c
tr->send_sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
if (tr->send_sock < 0)
	fatal_error("cannot create UDP socket");
```

| Aspect | Detail |
|--------|--------|
| Domain | `AF_INET` — IPv4 only |
| Type | `SOCK_DGRAM` — datagram, not stream or raw |
| Protocol | `IPPROTO_UDP` |
| Privilege | Normal; does **not** require root by itself |
| Bind | Not required; kernel picks source address/port |
| Peer | Set per call via `sendto`'s destination |

Root is still mandatory for the **ICMP** receive socket created earlier
in the same function. Order: ICMP first, then UDP. If UDP creation
fails, `fatal_error` exits; process teardown closes the ICMP fd.

### TTL on the UDP socket

```c
setsockopt(tr->send_sock, IPPROTO_IP, IP_TTL, &ttl, sizeof(ttl));
```

Called once per hop from the main loop **before** that hop's probes.
Every UDP datagram sent until the next `set_ttl` carries that TTL.
The receive socket does not set TTL; it only listens for ICMP.

Failure → `fatal_error("setsockopt IP_TTL")`. Continuing with a wrong
TTL would attribute the wrong router to the printed hop number.

---

## Destination port strategy

### Base port

| Source | Value | Meaning |
|--------|-------|---------|
| `DEFAULT_PORT` | `33434` | Classic traceroute default |
| CLI `-p port` | 1–65535 | Override base (`args.c`) |
| `tr->port` | int | Session base stored after init/parse |

Port **0** is rejected by CLI validation. The send formula also never
emits 0 (see wrap below).

### Sequence counter

| Field | Init | Update |
|-------|------|--------|
| `tr->seq` | `0` in `init_traceroute()` | `tr->seq++` at end of every `send_probe()` |

`seq` is **global across the whole run**, not reset per hop. Probe
ports therefore keep climbing for the entire traceroute.

### Formula

```c
probe->port = ((tr->port + tr->seq - 1) % 65535) + 1;
dest.sin_port = htons(probe->port);
```

With `port = 33434` and `seq` starting at 0:

| `seq` before send | Expression | Dest port |
|-------------------|------------|-----------|
| 0 | `((33434+0-1)%65535)+1` | **33434** |
| 1 | `((33434+1-1)%65535)+1` | **33435** |
| 2 | … | **33436** |
| 3 | … | **33437** |

Example: 3 probes/hop (`-q 3`), start at hop 1:

| Hop | Probe | `seq` | Dest port |
|-----|-------|-------|-----------|
| 1 | 1st | 0 | 33434 |
| 1 | 2nd | 1 | 33435 |
| 1 | 3rd | 2 | 33436 |
| 2 | 1st | 3 | 33437 |
| … | … | … | … |

### Wrap-around

`% 65535` then `+ 1` maps results into **1..65535** (never 0).

After enough probes that `tr->port + tr->seq - 1` exceeds 65535, ports
wrap. Collisions with a still-outstanding probe are unlikely in
practice (one probe waits at a time; previous hops are done), but the
formula still keeps the port in the legal range for `htons` / wire use.

### Network byte order

`probe->port` is stored in **host** byte order (plain `int`).
`dest.sin_port = htons(probe->port)` converts for the wire.
When matching, `parse_icmp` does `ntohs(udp_dport)` before comparing
to `probe->port`.

---

## Sending a probe — `send_probe()` step by step

**File:** `srcs/send.c`

```c
void	send_probe(t_traceroute *tr, t_probe *probe)
{
	struct sockaddr_in	dest;
	char				payload[PAYLOAD_SIZE];

	probe->port = ((tr->port + tr->seq - 1) % 65535) + 1;
	memset(payload, 0, sizeof(payload));
	memset(&dest, 0, sizeof(dest));
	dest.sin_family = AF_INET;
	dest.sin_addr = tr->dest_addr.sin_addr;
	dest.sin_port = htons(probe->port);
	gettimeofday(&probe->send_time, NULL);
	if (sendto(tr->send_sock, payload, sizeof(payload), 0,
			(struct sockaddr *)&dest, sizeof(dest)) < 0)
	{
		fprintf(stderr, "%s: sendto: %s\n", PROGRAM_NAME, strerror(errno));
	}
	tr->seq++;
}
```

### Ordered steps

1. **Compute identity** — `probe->port` from base + `seq`.
2. **Zero payload** — 32 bytes of `\0`.
3. **Build destination** — same IPv4 as resolution; unique UDP port.
4. **Stamp send time** — immediately before `sendto` for RTT accuracy.
5. **Transmit** — only the payload; kernel adds UDP + IP.
6. **Advance `seq`** — even if `sendto` failed, so the next probe gets
   a new expected port (failed send typically times out as `*`).

### Fields set / left alone

| `t_probe` field | After `send_probe` |
|-----------------|--------------------|
| `port` | set |
| `send_time` | set |
| `received` | still 0 (cleared by hop `memset`) |
| `recv_time`, `rtt`, `addr_str`, ICMP type/code | filled by recv or stay 0 |

### Error policy

`sendto` failure prints to stderr and **does not abort** the process.
That probe will almost certainly show as `*` after the wait. Other
probes in the hop still run.

### Interaction with the hop loop

From [hops.md](hops.md) / `main.c`: probes are **serial** — send, then
fully wait for that probe's reply (or timeout), then the next probe.
There is no pipeline of concurrent outstanding UDP ports. That makes
port matching trivial: at most one expected port is "active" in
`recv_probe` at a time (late ICMP from a previous probe can still
appear and is ignored if the port does not match).

---

## Matching replies via quoted UDP

### What ICMP embeds

RFC 792 / modern practice: an ICMP error includes the IP header of the
offending datagram plus at least the first **8 octets** of that
datagram's data — for UDP, that is the entire UDP header.

```
Outer IPv4 header          ← recvfrom buffer starts here (Linux)
  ICMP header (≥ 8 bytes)
    type @ outer_ihl + 0
    code @ outer_ihl + 1
    Quoted original IPv4   ← the probe's IP header as seen by the hop
      protocol @ +9        must be 17 (UDP)
    Quoted UDP header      ← first 8 bytes of original UDP datagram
      dest port @ +2       compared to probe->port
```

### Byte-level parse (`parse_icmp` in `recv.c`)

```
buf[0]                 → outer version/IHL
outer_ihl              = (buf[0] & 0x0f) * 4
icmp_type              = buf[outer_ihl]
icmp_code              = buf[outer_ihl + 1]
inner_offset           = outer_ihl + 8
inner_ihl              = (buf[inner_offset] & 0x0f) * 4
inner_proto            = buf[inner_offset + 9]
udp_offset             = inner_offset + inner_ihl
udp_dport (network)    = bytes at udp_offset + 2 .. +3
udp_dport (host)       = ntohs(...)
```

### Validation checklist

| # | Check | Failure means |
|---|-------|---------------|
| 1 | `len >= outer_ihl + 8 + 20 + 8` | Truncated; ignore |
| 2 | type is 11 (Time Exceeded) or 3 (Unreachable) | Other ICMP; ignore |
| 3 | `inner_proto == IPPROTO_UDP` | Quote is not our UDP probe |
| 4 | `len >= udp_offset + 4` | Dest-port bytes missing |
| 5 | `ntohs(udp_dport) == expected_port` | Different probe / noise |

Any failure returns `-1`. `recv_probe` loops with a **shrinking**
remaining timeout so unrelated ICMP does not reset the full wait
window (default 5.0 s).

### Why dest port is enough

- Chosen by us, unique per probe in normal operation.
- Present in every compliant ICMP quote that includes 8 bytes of UDP.
- Independent of payload content and of the kernel-chosen source port.
- Does not require raw-IP send or custom IP ID tracking.

### What is not matched

| Candidate | Why unused here |
|-----------|-----------------|
| UDP source port | Kernel-chosen; not stored in `t_probe` |
| UDP length / checksum | Irrelevant to identity |
| Payload bytes | Always zeros; not an ID |
| Outer ICMP identifier | Not an Echo message |
| Inner IP ID | Not recorded at send time |

### Address recorded on match

```c
inet_ntoa(from.sin_addr)  →  probe->addr_str
```

`from` is the **ICMP sender** — the router that emitted Time Exceeded,
or the destination that emitted Port Unreachable — not the UDP
destination address of the probe (except on the final hop, where they
coincide).

---

## End-to-end timeline of one probe

```
main: set_ttl(n)
main: send_probe
        ├─ port = f(base, seq)
        ├─ gettimeofday(send_time)
        ├─ sendto(32 zero bytes)     ──UDP/IP TTL=n──► network
        └─ seq++

main: recv_probe
        ├─ select(recv_sock, remaining)
        ├─ recvfrom → ICMP packet
        ├─ parse_icmp(match port?)
        │     no  → loop (remaining shrinks)
        │     yes → addr_str, icmp type/code, recv_time, rtt
        └─ or timeout → received = 0

main: (next probe or print_hop)
```

Path outcomes:

| Where UDP dies / arrives | ICMP back | Printed hop |
|--------------------------|-----------|-------------|
| Router N (TTL expired) | Time Exceeded from N | Hop N's IP + RTT |
| Destination (port closed) | Port Unreachable from dest | Final hop; stop |
| Dropped / ICMP filtered | nothing | `*` |
| Quote too short / wrong port | ignored → often `*` | `*` |

---

## ICMP outcomes tied to UDP probes

| Type | Code | Macro | Meaning for a UDP probe | Display / control |
|------|------|-------|-------------------------|-------------------|
| 11 | 0 | `FT_ICMP_TIMXCEED` | TTL expired at a router | Address + RTT; continue |
| 3 | 3 | `FT_UNREACH_PORT` | UDP reached host; port closed | Address + RTT; **stop** |
| 3 | 0 | `FT_UNREACH_NET` | Network unreachable | `!N`; continue |
| 3 | 1 | `FT_UNREACH_HOST` | Host unreachable | `!H`; continue |
| 3 | 2 | `FT_UNREACH_PROTO` | Protocol unreachable | `!P`; continue |
| 3 | 4 | `FT_UNREACH_FRAG` | Fragmentation needed | `!F`; continue |
| 3 | 5 | `FT_UNREACH_SR` | Source route failed | `!S`; continue |
| 3 | 9/10/13 | `FT_UNREACH_ADMIN*` | Administratively prohibited | `!X`; continue |

Port Unreachable is the only Destination Unreachable code that both
prints without `!` and ends the hop walk.

---

## Resolution and UDP

**File:** `srcs/resolve.c`

```c
hints.ai_family   = AF_INET;
hints.ai_socktype = SOCK_DGRAM;
```

`getaddrinfo` is asked for an address suitable for a datagram socket,
consistent with UDP probes. The service name is `NULL` — destination
port is **not** chosen at resolve time. `tr->dest_addr` keeps the IP;
`send_probe` overwrites `sin_port` on a local copy each time.

---

## Constants and struct fields (UDP-related)

### Macros (`ft_traceroute.h`)

| Macro | Value | Role |
|-------|-------|------|
| `DEFAULT_PORT` | 33434 | Base UDP dest port |
| `DEFAULT_PACKET_SIZE` | 60 | Header text / assumed wire size |
| `PAYLOAD_SIZE` | 32 | Bytes passed to `sendto` |
| `RECV_BUFF_SIZE` | 512 | ICMP receive buffer |
| `MAX_PROBES` | 10 | Cap for `-q` / probe array |

### `t_traceroute`

| Field | UDP role |
|-------|----------|
| `send_sock` | UDP datagram FD |
| `port` | Base destination port |
| `seq` | Global counter → unique ports |
| `packet_size` | Reported size (60); not a send length override |
| `dest_addr` | Target IPv4; port field unused after resolve |

### `t_probe`

| Field | UDP role |
|-------|----------|
| `port` | Exact UDP dest port of this probe (match key) |
| `send_time` | Stamp at `sendto` |
| `icmp_type` / `icmp_code` | From ICMP that quoted this UDP |

---

## Edge cases and operational notes

### Firewalls and filtering

- UDP egress allowed but ICMP Time Exceeded blocked → hops show `*`.
- Destination rate-limits or suppresses Port Unreachable → may never
  stop until `max_hops`.
- Stateful firewalls may treat high-port UDP as suspicious and drop it.

### Truncated ICMP quotes

Some middleboxes return fewer than IP + 8 bytes of the original
datagram. Length checks in `parse_icmp` then fail → reply looks like a
timeout even though ICMP arrived.

### ECMP / load balancing

Different probes at the same TTL can take different paths and return
different router IPs. Port uniqueness still matches each probe; display
prints each new address (see [hops.md](hops.md) / [output.md](../modules/output.md)).

### Late replies

Because probes are serial, a late ICMP for port P while waiting for
port P+1 is discarded (port mismatch). The wait deadline is not reset.

### `sendto` errors

Permission, unreachable route, or network down can make `sendto` fail.
The hop still prints; that slot is usually `*`.

### IPv4 header options

Size math assumes a 20-byte IP header. If the host somehow sent IP
options, `DEFAULT_PACKET_SIZE` in the header text would disagree with
the true wire length. This code path does not add options.

---

## Limits and non-goals

1. **IPv4 + UDP only** — no IPv6, TCP SYN, or ICMP-echo probe modes.
2. **No application semantics** — ports are probe IDs, not services;
   payload is zeros.
3. **Identification via dest port only** — no payload magic, no IP ID.
4. **No concurrent probe pipeline** — one outstanding wait at a time.
5. **No payload/size CLI** — fixed 32-byte payload / 60-byte reported size.
6. ICMP filtering or short quotes produce `*` even when UDP left the host.

---

## Module map

| Piece | File | Role |
|-------|------|------|
| UDP socket create | `socket.c` | `SOCK_DGRAM` / `IPPROTO_UDP` |
| TTL on UDP socket | `socket.c` | `setsockopt(IP_TTL)` |
| Emit probe | `send.c` | port, payload, `sendto` |
| Match via quoted UDP | `recv.c` | `parse_icmp` dest-port check |
| Resolve hint | `resolve.c` | `SOCK_DGRAM` for `getaddrinfo` |
| Base port CLI | `args.c` | `-p` |
| Stop on Port Unreachable | `main.c` | `check_reached` |
