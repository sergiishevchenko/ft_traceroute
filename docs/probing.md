# Probing — Send, Receive & RTT

**Sources:** `srcs/main.c`, `srcs/send.c`, `srcs/recv.c`, `srcs/time.c`  
**Types:** `t_traceroute`, `t_probe` in `includes/ft_traceroute.h`

## Overview

Probing is the heart of traceroute. Given a resolved destination and open
sockets, the program discovers each hop by:

1. Setting the IP TTL to the hop number.
2. Sending one or more UDP probes to unlikely destination ports.
3. Waiting for ICMP errors that quote those probes.
4. Recording who replied and how long it took.
5. Stopping when the destination says the UDP port is unreachable.

This document explains the orchestration in `main.c` and the three
support modules that implement send, receive/parse, and RTT math.

---

## Why this algorithm works

IP routers must decrement TTL and drop the packet when it reaches 0.
When they drop it, they should send **ICMP Time Exceeded** to the
original sender. That ICMP message is sourced from the router’s own IP —
so each successive TTL reveals the next router on the path.

When TTL is finally large enough to reach the target host, the UDP
datagram arrives. Because the destination port is almost certainly not
listening, the host replies with **ICMP Destination Unreachable —
Port Unreachable**. That is the signal that the path has been fully
mapped.

```
TTL=1:  Host → [R1 drops] → ICMP Time Exceeded from R1
TTL=2:  Host → R1 → [R2 drops] → ICMP Time Exceeded from R2
TTL=n:  Host → … → Target → ICMP Port Unreachable from Target
```

---

## Data used during probing

### `t_traceroute` (session)

| Field        | Role during probing |
|--------------|---------------------|
| `dest_addr`  | Target IPv4 for every `sendto` |
| `send_sock`  | UDP socket |
| `recv_sock`  | Raw ICMP socket |
| `port`       | Base UDP port (CLI `-p`) |
| `seq`        | Global increasing counter; drives unique probe ports |
| `ttl`        | Current hop number |
| `nprobes`    | Probes per hop |
| `wait_time`  | Per-probe receive timeout (seconds, default 5.0) |
| `first_ttl` / `max_hops` | Loop bounds |
| `reached`    | Present in the struct; stop detection uses `check_reached()` return value rather than this flag in the current main loop |

### `t_probe` (one probe attempt)

| Field        | Meaning |
|--------------|---------|
| `send_time`  | `gettimeofday` just before `sendto` |
| `recv_time`  | `gettimeofday` just after a matching `recvfrom` |
| `rtt`        | Round-trip time in milliseconds |
| `addr_str`   | IP of the ICMP sender (hop) |
| `received`   | `1` if matched reply, `0` on timeout |
| `icmp_type`  | 11 (Time Exceeded) or 3 (Unreachable), etc. |
| `icmp_code`  | e.g. 3 = Port Unreachable |
| `port`       | UDP destination port used for this probe |

`main()` allocates `t_probe probes[MAX_PROBES]` on the stack and zeroes
`nprobes` entries at the start of each hop.

---

## Main loop — `main.c`

### Startup sequence

```
init_traceroute(&tr)
parse_args(&tr, argc, argv)
resolve_host(&tr)
create_sockets(&tr)
print_header(&tr)
tr.ttl = tr.first_ttl
```

### Per-hop loop

```
while (tr.ttl <= tr.max_hops):
    set_ttl(&tr, tr.ttl)
    memset(probes, 0, sizeof(t_probe) * tr.nprobes)

    for p = 0 .. nprobes-1:
        send_probe(&tr, &probes[p])
        recv_probe(&tr, &probes[p])     # blocking up to wait_time

    print_hop(&tr, probes)

    if check_reached(probes, tr.nprobes):
        break

    tr.ttl++

close_sockets(&tr)
return 0
```

### Sequential probes (not pipelined)

Each probe is fully sent and awaited before the next one starts. This
simplifies matching (only one outstanding expected port) and matches a
straightforward reading of the subject. The downside is wall-clock cost:
worst case roughly `nprobes × wait_time` seconds of silence per hop when
everything times out (e.g. `3 × 5s = 15s` per starred hop).

### Destination reached — `check_reached()`

Returns true if **any** probe in the hop satisfies:

```
received == 1
&& icmp_type == FT_ICMP_UNREACH        /* 3 */
&& icmp_code == FT_UNREACH_PORT        /* 3 */
```

Notes:

- A single Port Unreachable among three probes is enough to stop.
- Time Exceeded never stops the loop.
- Other unreachable codes (`!H`, `!N`, …) are shown by the display layer
  but **do not** break the loop; the program keeps increasing TTL until
  Port Unreachable or `max_hops`.
- If the destination never answers, the loop ends at `max_hops` and
  exits 0.

---

## Sending — `send_probe()` (`send.c`)

### Goals

1. Emit a UDP datagram toward the resolved target.
2. Give it a **unique destination port** so the ICMP error’s embedded
   UDP header can be matched later.
3. Stamp `send_time` as late as possible before the syscall for accurate
   RTT.

### Port selection

```c
probe->port = ((tr->port + tr->seq - 1) % 65535) + 1;
```

With defaults (`port = 33434`, `seq` starting at 0):

| seq | destination port |
|-----|------------------|
| 0   | 33434            |
| 1   | 33435            |
| 2   | 33436            |
| …   | …                |

The modulo expression wraps within 1–65535 so the port stays valid even
after many hops × probes. After each successful setup (even if `sendto`
fails), `tr->seq` is incremented so the next probe never reuses the same
expected port identity in normal operation.

### Payload and length

- Buffer: `char payload[PAYLOAD_SIZE]` with `PAYLOAD_SIZE == 32`, filled
  with zeros.
- Displayed size: `DEFAULT_PACKET_SIZE` (60) =
  20-byte IP + 8-byte UDP + 32-byte payload (standard IPv4 header
  without options).

Payload content does not affect hop discovery; size mainly affects
path MTU / filtering behavior and the header line users see.

### Destination address

A local `sockaddr_in dest` is filled each call:

- `sin_family = AF_INET`
- `sin_addr = tr->dest_addr.sin_addr` (from resolution)
- `sin_port = htons(probe->port)`

### Send path

```c
gettimeofday(&probe->send_time, NULL);
sendto(tr->send_sock, payload, sizeof(payload), 0,
       (struct sockaddr *)&dest, sizeof(dest));
tr->seq++;
```

If `sendto` fails, the program prints
`ft_traceroute: sendto: <strerror>` to stderr but **continues**. The
probe will typically time out in `recv_probe()` (`received = 0` → `*`).

### Fields set by send

| Field       | Set? |
|-------------|------|
| `port`      | yes  |
| `send_time` | yes  |
| `received`  | left 0 until recv |
| others      | cleared by hop’s `memset` |

---

## Receiving — `recv_probe()` (`recv.c`)

### Goals

1. Wait at most `tr->wait_time` seconds for a **matching** ICMP packet.
2. Ignore unrelated ICMP traffic (other processes, other probes’ late
   replies if any, non-UDP-quoted errors).
3. Fill address, ICMP type/code, and RTT when a match arrives.
4. Mark timeout cleanly when nothing useful arrives in time.

### Adaptive timeout with `select()`

The subject forbids `poll`, `ppoll`, and `fcntl`. The implementation
uses `select()` in a loop:

1. Record `start` with `gettimeofday`.
2. Each iteration:
   - Compute `remaining = wait_time − elapsed`.
   - If `remaining <= 0`, set `probe->received = 0` and return.
   - Convert `remaining` into `struct timeval tv` for `select`.
3. `select(recv_sock + 1, &fds, NULL, NULL, &tv)`.
4. If `select` returns `<= 0` (timeout or error), treat as no reply.
5. If readable, `recvfrom` into a 512-byte buffer (`RECV_BUFF_SIZE`).
6. Stamp `recv_time`, call `parse_icmp`. On match, fill fields and return
   success. On mismatch, **loop again** with a reduced remaining time
   (false positives do not restart the full 5 seconds).

This adaptive shrinking is important: a noisy ICMP socket might deliver
many unrelated messages; discarding them must not reset the deadline.

### Return values

| Situation | `probe->received` | Function return |
|-----------|-------------------|-----------------|
| Match     | `1`               | `1`             |
| Timeout / select fail | `0`     | `0` (via assignment expression) |

### Packet parsing — `parse_icmp()`

Linux raw ICMP `recvfrom` data is interpreted as:

```
+---------------------------+
| Outer IPv4 header         |  IHL = (buf[0] & 0x0f) * 4
+---------------------------+
| ICMP header (8+ bytes)    |  type @ outer_ihl, code @ outer_ihl+1
+---------------------------+
| Inner IPv4 header         |  quote of the original datagram
|   protocol @ +9           |  must be IPPROTO_UDP
+---------------------------+
| Inner UDP header          |  dest port @ +2 (network byte order)
+---------------------------+
```

#### Validation checklist

1. **Length:** need at least outer IP + 8 (ICMP) + 20 (min inner IP) + 8
   (UDP). Additional check that UDP dest-port bytes are inside `len`.
2. **ICMP type:** only `FT_ICMP_TIMXCEED` (11) or `FT_ICMP_UNREACH` (3).
   Other types (echo reply, redirect, …) are ignored.
3. **Inner protocol:** byte 9 of the inner IP header must be UDP (17).
4. **Port match:** `ntohs(udp_dport) == expected_port` (`probe->port`).

Any failure returns `-1`; the wait loop continues until timeout.

#### Why match on UDP dest port?

ICMP error messages embed the start of the offending packet. The
destination port we chose at send time travels out in the UDP header
and comes back inside that quote. Matching it is the reliable way to
associate a reply with a specific probe without embedding a custom
payload ID (and without needing the ICMP identifier fields, which are
not used the same way as in ping).

#### What is stored on success

```c
probe->icmp_type = ...
probe->icmp_code = ...
probe->addr_str  = inet_ntoa(from.sin_addr)  // ICMP sender, i.e. the hop
probe->received  = 1
probe->rtt       = calc_rtt(&send_time, &recv_time)
```

`from.sin_addr` is the address of the machine that generated the ICMP
message — the router or destination — not necessarily equal to the
traceroute target until the final hop.

### Minimum length note

Some routers send ICMP errors with truncated quotes (less than full
UDP header). Extremely short quotes may fail the length checks and appear
as timeouts (`*`) even when an ICMP arrived. This is a known class of
middlebox quirks.

---

## RTT — `calc_rtt()` (`time.c`)

```c
double calc_rtt(struct timeval *send_tv, struct timeval *recv_tv)
{
    return (recv_tv->tv_sec - send_tv->tv_sec) * 1000.0
         + (recv_tv->tv_usec - send_tv->tv_usec) / 1000.0;
}
```

- Result unit: **milliseconds** as `double`.
- Display later formats with `%.3f ms`.
- `recv_time` is taken after `recvfrom` returns, so RTT includes a little
  userspace overhead after the packet is already available.
- Negative differences are not expected if clocks are monotonic for the
  process; `gettimeofday` can theoretically step backward on system
  clock updates (rare during a short probe).

There is no smoothing, average, or jitter calculation — each probe’s
RTT is independent, like traditional traceroute.

---

## ICMP types and control flow

Constants from the header:

| Macro              | Value | Role |
|--------------------|-------|------|
| `FT_ICMP_TIMXCEED` | 11    | Intermediate hop |
| `FT_ICMP_UNREACH`  | 3     | Destination unreachable family |
| `FT_UNREACH_PORT`  | 3     | Destination reached (stop) |
| `FT_UNREACH_NET`   | 0     | Annotation `!N` only |
| `FT_UNREACH_HOST`  | 1     | Annotation `!H` only |
| `FT_UNREACH_PROTO` | 2     | Annotation `!P` only |
| `FT_UNREACH_FRAG`  | 4     | Annotation `!F` only |
| `FT_UNREACH_SR`    | 5     | Annotation `!S` only |
| `FT_UNREACH_ADMIN9/10/13` | 9/10/13 | Annotation `!X` only |

**Parsing** accepts types 11 and 3 (any code).  
**Stopping** requires type 3 code 3.  
**Printing** annotations for other codes is left to `display.c`.

---

## Timing & performance expectations

| Scenario | Approximate wait |
|----------|------------------|
| Healthy hop, 3 probes | 3 × RTT (often milliseconds to tens of ms) |
| Fully silent hop (`* * *`) | up to `nprobes × wait_time` (default 15 s) |
| `-q 1` | up to 5 s per silent hop |
| Path of 15 hops with some stars | can take well over a minute |

These characteristics match a simple serial traceroute implementation.

---

## End-to-end example (one hop)

Assume `-q 1`, current `ttl = 2`, `seq = 1`, base port 33434.

1. `set_ttl(2)` — UDP packets leave with TTL 2.
2. `send_probe`:
   - dest port = 33435
   - 32 zero bytes via `sendto`
   - `send_time` recorded
3. First router forwards (TTL → 1); second router drops (TTL → 0),
   sends ICMP Time Exceeded.
4. `recv_probe` / `select` wakes; `parse_icmp` sees type 11, matching
   embedded port 33435; stores router IP and RTT.
5. `print_hop` prints ` 2  router…  x.xxx ms`.
6. `check_reached` is false (type 11) → continue with `ttl = 3`.

When TTL finally reaches the host:

4b. Host returns ICMP type 3 code 3.  
5b. Hop line prints without `!` annotation for that code.  
6b. `check_reached` → break → `close_sockets` → exit 0.

---

## Interaction with other modules

| Module        | Interaction |
|---------------|-------------|
| CLI           | Supplies `port`, `nprobes`, `first_ttl`, `max_hops` |
| Networking    | `set_ttl`, sockets, `dest_addr` |
| Display       | Consumes filled `t_probe[]` after each hop |
| Utils         | Not used on the soft `sendto` failure path |

---

## Limitations & intentional simplifications

1. **No parallel outstanding probes** — simpler matching, slower on timeouts.
2. **No ICMP checksum verification** in userspace — relies on the kernel.
3. **No identification via payload magic** — only UDP dest port.
4. **IPv4 / UDP traceroute only** — no TCP or ICMP-probe modes.
5. **Late replies** for a previous probe are ignored by later probes’
   port checks (and wasted as “unrelated” if they fall into another
   probe’s wait window with the wrong port).
6. **`tr->reached`** exists in the struct but the stop condition is
   evaluated via `check_reached()`’s boolean return, not by assigning
   that field in the current code.

---

## Quick reference

| Function         | File     | Responsibility |
|------------------|----------|----------------|
| `main` loop      | main.c   | TTL / probe orchestration |
| `check_reached`  | main.c   | Port Unreachable → stop |
| `send_probe`     | send.c   | UDP emit + port + send timestamp |
| `recv_probe`     | recv.c   | `select` wait + match + fill probe |
| `parse_icmp`     | recv.c   | Validate & extract ICMP / port |
| `calc_rtt`       | time.c   | Millisecond RTT from two timevals |
