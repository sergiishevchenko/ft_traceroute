# Networking — Host Resolution & Sockets

**Sources:** `srcs/resolve.c`, `srcs/socket.c`  
**Related header:** `includes/ft_traceroute.h`  
**Called from:** `main()` after `parse_args()`, before `print_header()`

## Overview

After CLI parsing, the program still cannot send a single probe. It needs:

1. A concrete IPv4 destination (`sockaddr_in`) for every `sendto()`.
2. Two live sockets: one to emit UDP probes, one to capture ICMP errors.

This document covers both steps. They are intentionally separate modules:
resolution can fail on bad hostnames without touching the network stack’s
raw sockets, and sockets can fail independently (permissions, resource
limits) after a successful DNS lookup.

```
parse_args()
    │
    ▼
resolve_host()      ← hostname / IP string → dest_addr + resolved_ip
    │
    ▼
create_sockets()    ← open SOCK_RAW (ICMP) + SOCK_DGRAM (UDP)
    │
    ▼
print_header() / TTL loop (set_ttl each hop)
    │
    ▼
close_sockets()
```

---

## Why two sockets?

Classic UDP traceroute works by **provoking ICMP error messages**:

| Direction | Protocol | What happens |
|-----------|----------|--------------|
| Outbound  | UDP      | Probe with controlled IP TTL and unused dest port |
| Inbound   | ICMP     | Router: Time Exceeded; Host: Port Unreachable |

Linux delivers ICMP to a **raw ICMP** socket. User-space UDP sockets do
not receive those errors as normal datagrams on the send socket in the
way this program needs them, so a dedicated `SOCK_RAW` / `IPPROTO_ICMP`
receiver is required.

Root (or `CAP_NET_RAW`) is mandatory for that raw socket. The CLI layer
already rejects non-root runs; `create_sockets()` still checks for
creation failure and aborts via `fatal_error()` if something goes wrong.

---

## Host resolution — `resolve_host()`

**File:** `srcs/resolve.c`  
**Input:** `tr->host` (from argv; may be `"google.com"` or `"8.8.8.8"`)  
**Output:** `tr->dest_addr`, `tr->resolved_ip`

### Purpose

Convert a human hostname or textual IPv4 address into:

- a binary `struct sockaddr_in` used for every probe’s destination
  address (the UDP **port** on that structure is overwritten per probe
  in `send.c`);
- a printable dotted-quad string used in the traceroute header.

### Step-by-step

1. **Zero and configure hints** for `getaddrinfo()`:
   ```c
   hints.ai_family   = AF_INET;      /* IPv4 only — no AAAA */
   hints.ai_socktype = SOCK_DGRAM;   /* consistent with UDP probes */
   ```
   Service name is passed as `NULL` because the destination port is not
   chosen at resolve time.

2. **Call** `getaddrinfo(tr->host, NULL, &hints, &res)`.

3. **On failure** (`ret != 0`):
   ```
   ft_traceroute: <host>: <gai_strerror(ret)>
   ```
   then `exit(EXIT_FAILURE)`. Typical cases: unknown name,
   temporary DNS failure, invalid address literal.

4. **On success**, copy the first result:
   ```c
   memcpy(&tr->dest_addr, res->ai_addr, sizeof(tr->dest_addr));
   ```
   Only `res`’s first `addrinfo` node is used. If a name has multiple A
   records, traceroute always follows that first address for the whole
   run (no round-robin across records).

5. **Format printable IP**:
   ```c
   snprintf(tr->resolved_ip, ..., "%s", inet_ntoa(tr->dest_addr.sin_addr));
   ```
   `inet_ntoa()` returns a pointer to a static buffer; the string is
   copied immediately into `tr->resolved_ip[INET_ADDRSTRLEN]`.

6. **Free** the linked list with `freeaddrinfo(res)`.

### What is stored in `dest_addr`

| Member        | Typical content after resolve      |
|---------------|------------------------------------|
| `sin_family`  | `AF_INET`                          |
| `sin_addr`    | Target IPv4                        |
| `sin_port`    | Whatever `getaddrinfo` provided (unused later; send path sets port) |
| `sin_zero`    | padding                            |

`send_probe()` builds a **fresh** `sockaddr_in` each time, copying only
`sin_addr` from `tr->dest_addr` and setting `sin_port` to the probe port.
Resolution therefore fixes **where** packets go; probing fixes **which
UDP port** identifies each probe.

### IPv4-only design

`AF_INET` means:

- IPv6-only hosts fail at resolve time.
- Dual-stack names that return AAAA first still work if an A record is
  present — `getaddrinfo` with `AF_INET` asks only for IPv4.

This matches the subject’s IPv4 traceroute scope.

### Failure examples

```bash
sudo ./ft_traceroute invalid.host.42test
# ft_traceroute: invalid.host.42test: Name or service not known

sudo ./ft_traceroute 999.999.999.999
# resolution / address error via gai_strerror
```

---

## Socket creation — `create_sockets()`

**File:** `srcs/socket.c`

### Receive socket (ICMP)

```c
tr->recv_sock = socket(AF_INET, SOCK_RAW, IPPROTO_ICMP);
```

- Kernel delivers **IP packets containing ICMP** to this descriptor.
- On Linux, `recvfrom()` typically returns the packet **including the
  outer IP header**. The receive parser in `recv.c` relies on that
  layout (it reads IHL from the first byte).
- No `bind()` is required: the socket receives ICMP destined to the
  local host (including errors generated for our outbound UDP probes).

### Send socket (UDP)

```c
tr->send_sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
```

- Normal datagram socket; kernel builds IP and UDP headers.
- TTL is controlled with `setsockopt(IP_TTL)` rather than crafting a
  raw IP header — simpler and allowed by the subject’s function list.
- Payload content is irrelevant to path discovery (zeros are fine);
  size affects only the stated packet length (60 bytes).

### Error handling

Either `socket()` failure calls:

```c
fatal_error("cannot create ICMP socket");
/* or */
fatal_error("cannot create UDP socket");
```

`fatal_error()` (in `utils.c`) prints
`ft_traceroute: <msg>: <strerror(errno)>` to stderr and exits. Common
errno values: `EPERM` / `EACCES` if capabilities are wrong, `EMFILE` /
`ENFILE` if descriptor limits are hit.

Order of creation: **ICMP first, then UDP**. If UDP creation fails,
the ICMP socket is not explicitly closed here (process exit cleans up).
Both descriptors start as `-1` in `init_traceroute()` so
`close_sockets()` is safe if called later.

---

## TTL control — `set_ttl()`

```c
setsockopt(tr->send_sock, IPPROTO_IP, IP_TTL, &ttl, sizeof(ttl));
```

Called once per hop, immediately before the inner probe loop, with
`ttl` equal to the current hop number (`tr->ttl`).

### What IP_TTL does

Each IP packet the UDP socket sends carries this TTL. Every router that
forwards the packet decrements it. When TTL hits 0, that router drops
the packet and should emit **ICMP Time Exceeded** back to us — which is
how traceroute learns the hop’s address.

Raising TTL by one each outer-loop iteration lets probes travel one hop
farther until they finally reach the destination.

### Failure

`setsockopt` failure → `fatal_error("setsockopt IP_TTL")`. This is rare
on a healthy system but treated as fatal because continuing with a
stale TTL would produce a wrong map of the path.

### Important detail

TTL is set on the **send** socket only. The receive socket does not need
a matching TTL; it only listens for ICMP generated by others.

---

## Cleanup — `close_sockets()`

```c
if (tr->recv_sock >= 0) close(tr->recv_sock);
if (tr->send_sock >= 0) close(tr->send_sock);
```

Invoked at the end of a normal `main()` run after the TTL loop. Guards
against `-1` so partial initialization would still be safe. Descriptors
are not reset to `-1` after close (not needed; process is exiting).

There is no `atexit` handler; a fatal exit from resolution or socket
creation skips this cleanup, which is acceptable because the OS reclaims
FDs on process termination.

---

## Lifecycle relative to probing

```
create_sockets()
print_header()
for ttl in first_ttl … max_hops:
    set_ttl(ttl)                 ← networking API
    for each probe:
        send_probe()             ← uses send_sock + dest_addr.sin_addr
        recv_probe()             ← uses recv_sock
    print_hop()
close_sockets()
```

Sockets remain open for the entire trace. Opening/closing per probe
would be unnecessarily expensive and is not how classic traceroute
behaves.

---

## Relationship to program state

| Field          | Set by              | Used by                          |
|----------------|---------------------|----------------------------------|
| `host`         | CLI                 | `resolve_host`, header           |
| `dest_addr`    | `resolve_host`      | `send_probe` (address)           |
| `resolved_ip`  | `resolve_host`      | `print_header`                   |
| `recv_sock`    | `create_sockets`    | `recv_probe`                     |
| `send_sock`    | `create_sockets`    | `send_probe`, `set_ttl`          |

---

## Design notes & limitations

1. **No source address / interface binding** — the OS chooses the
   egress interface via the routing table.
2. **No IPv6** — subject scope is IPv4.
3. **No SO_RCVTIMEO on the socket** — receive timeouts are implemented
   with `select()` in `recv.c`, which is the allowed approach (and
   avoids forbidden `poll` / `fcntl` patterns).
4. **Raw ICMP vs “ping sockets”** — some systems offer non-root ICMP
   via specialised APIs; this project uses classic root + raw sockets
   as required by the 42 subject.
5. **Firewall / container issues** — Docker or host firewalls that drop
   ICMP Time Exceeded produce `*` timeouts even when UDP leaves the
   machine; that is environmental, not a socket-setup bug.

---

## Quick reference

| Function           | Module      | Role |
|--------------------|-------------|------|
| `resolve_host`     | resolve.c   | DNS / address literal → `dest_addr` |
| `create_sockets`   | socket.c    | Open ICMP + UDP sockets |
| `set_ttl`          | socket.c    | Apply hop TTL before probes |
| `close_sockets`    | socket.c    | Close both FDs at end |
| `fatal_error`      | utils.c     | Shared abort path for socket failures |
