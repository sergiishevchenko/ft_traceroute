# Hops — Path Discovery by TTL

**Sources:** `srcs/main.c`, `srcs/socket.c`, `srcs/display.c`,
`srcs/send.c`, `srcs/recv.c`, `srcs/args.c`  
**Types:** `t_traceroute`, `t_probe` in `includes/ft_traceroute.h`  
**Related:** [probing.md](probing.md) (send/recv/RTT internals),
[networking.md](networking.md) (sockets & `IP_TTL`),
[udp.md](udp.md) (why UDP / port matching),
[output.md](output.md) (line layout & annotations),
[cli.md](cli.md) (`-f` / `-m` / `-q`)

## Overview

A traceroute run is a walk along **hops**: for each successive IP
Time-To-Live value, the program sends UDP probes, waits for ICMP errors,
prints one line, and decides whether to stop or advance. This document
is the hop-centric view of that walk — what a hop is, how TTL maps to
hop numbers, how the outer loop is bounded, what each hop’s probes
mean, how the hop line is printed, and when the walk ends.

Lower-level send/recv mechanics live in [probing.md](probing.md). This
file focuses on the **per-hop unit of work** and the path map that
results from many of those units.

---

## What is a hop?

### Definition

In this project, a **hop** is:

1. One iteration of the TTL loop in `main()`.
2. The network device that generated the matching ICMP reply for probes
   sent with that TTL (when someone answers).
3. One stdout line produced by `print_hop()`.

Hop number `N` means: “probes left with `IP_TTL = N`; whoever replied
is hop `N`.”

| Hop # | Typical responder | ICMP usually seen |
|-------|-------------------|-------------------|
| 1 | First router (LAN gateway, ISP edge, …) | Time Exceeded |
| 2 … k−1 | Intermediate routers | Time Exceeded |
| k (last) | Destination host | Port Unreachable |
| any | Nobody within `wait_time` | — (printed as `*`) |

### Hop number ≡ TTL

`tr->ttl` is used in two places with the same value:

| Use | Where |
|-----|--------|
| `setsockopt(..., IP_TTL, &tr->ttl, ...)` | Before probes of this hop |
| `printf("%2d", tr->ttl)` | Start of the hop line |

There is no separate “hop index” field. Changing `-f` / `-m` changes
both the printed numbers and how far packets are allowed to travel.

### Hop vs router vs destination

- **Intermediate hop:** a router that *drops* the probe because TTL
  expired. It never delivers the UDP datagram to an application.
- **Final hop:** the target host that *receives* the UDP datagram and
  replies that the destination port is closed.
- **Silent hop:** no usable ICMP matched within the timeout. The path
  still advances; the map just has a hole (`*`).

The printed IP is always the **ICMP sender** (`from.sin_addr` from
`recvfrom`), not the inner quoted destination. Until the last hop, that
is usually a router’s address; on the last hop it is usually the
traceroute target (or a middlebox speaking for it).

---

## Why TTL discovers hops

### The IP rule

Every IPv4 packet carries an 8-bit TTL. Each router that forwards the
packet **must** decrement it. When TTL would go to 0 (or reaches 0
before forward), the router **must not** forward the packet. It discards
it and **should** send ICMP **Time Exceeded** (type 11) to the original
source. The source address of that ICMP message is the router’s own IP
on the interface that generates the error — exactly the hop identity
traceroute wants.

### Incremental mapping

```
TTL=1:  Host → [R1 drops] → ICMP Time Exceeded from R1
TTL=2:  Host → R1 → [R2 drops] → ICMP Time Exceeded from R2
TTL=3:  Host → R1 → R2 → [R3 drops] → ICMP Time Exceeded from R3
…
TTL=n:  Host → … → Target receives UDP → ICMP Port Unreachable from Target
```

Raising TTL by one each outer-loop iteration lets probes survive one
more router until they finally reach the destination.

### Why the destination looks different

When TTL is large enough, the packet arrives at the target with TTL > 0.
The host does not send Time Exceeded for a successful delivery. Instead,
because the UDP destination port is almost certainly unused
(default base 33434 + sequence), the host replies with ICMP
**Destination Unreachable — Port Unreachable** (type 3, code 3). That
code is the program’s stop signal (`check_reached`).

Details on why UDP and how ports are matched: [udp.md](udp.md).

---

## Session fields that control hops

### Bounds and density (`t_traceroute`)

| Field | Set by | Default | Meaning for hops |
|-------|--------|---------|------------------|
| `first_ttl` | init / `-f` | 1 | First hop number / TTL |
| `max_hops` | init / `-m` | 30 | Last hop number / TTL allowed |
| `nprobes` | init / `-q` | 3 | Probes sent at each hop |
| `ttl` | main loop | — | Current hop (mutated each iteration) |
| `wait_time` | init only | 5.0 s | Max wait per probe (not CLI) |
| `no_dns` | `-n` | false | How hop addresses are printed |
| `seq` | send_probe | 0 | Global probe counter (unique ports across hops) |
| `reached` | init | false | Present but unused by current stop logic |

### Per-probe data (`t_probe`) for one hop

`main()` keeps a stack array `t_probe probes[MAX_PROBES]`. At the start
of each hop it zeroes only the first `nprobes` entries. After the inner
loop, those entries are the complete record for that hop line.

| Field | Filled when | Role on the hop line |
|-------|-------------|----------------------|
| `port` | send | Matching key for ICMP quote |
| `send_time` / `recv_time` | send / recv | RTT inputs |
| `rtt` | recv success | Printed as `%.3f ms` |
| `addr_str` | recv success | Hop IP from ICMP sender |
| `received` | recv | `1` → print RTT; `0` → print `*` |
| `icmp_type` / `icmp_code` | recv | Annotations + stop check |

---

## The hop loop — `main.c`

### Startup (before any hop)

```
init_traceroute(&tr)     // defaults: first_ttl=1, max_hops=30, nprobes=3
parse_args(&tr, ...)     // may override -f / -m / -q / -n / -p
resolve_host(&tr)
create_sockets(&tr)
print_header(&tr)        // shows max_hops, not first_ttl
tr.ttl = tr.first_ttl
```

### Per-hop body

```
while (tr.ttl <= tr.max_hops):
    set_ttl(&tr, tr.ttl)

    memset(probes, 0, sizeof(t_probe) * tr.nprobes)

    for p = 0 .. nprobes-1:
        send_probe(&tr, &probes[p])
        recv_probe(&tr, &probes[p])    // up to wait_time

    print_hop(&tr, probes)

    if check_reached(probes, tr.nprobes):
        break

    tr.ttl++

close_sockets(&tr)
return 0
```

### Ordering guarantees

1. **TTL is set once per hop**, before any probe of that hop. All
   `nprobes` datagrams share the same TTL.
2. **Probes are sequential**, not pipelined: probe `p` is fully awaited
   before probe `p+1` is sent. Only one expected UDP port is outstanding
   at a time, which simplifies matching.
3. **The hop line is printed only after all probes finish** (including
   timeouts). Partial lines are never flushed mid-hop.
4. **Stop is checked after print**, so the destination hop still appears
   on stdout before `break`.

### Cost of one hop (wall clock)

| Situation | Approximate time |
|-----------|------------------|
| Healthy hop, all probes reply quickly | sum of RTTs (often ms–tens of ms) |
| Fully silent hop (`* * *`) | up to `nprobes × wait_time` (default **15 s**) |
| `-q 1` silent hop | up to **5 s** |
| Mix of replies and stars | replies fast + `wait_time` per star |

A long path with several firewalled hops can take well over a minute
even when the destination is eventually reached.

---

## Bounds: `-f`, `-m`, and edge cases

### CLI mapping

| Option | Field | Range | Default |
|--------|-------|-------|---------|
| `-f first_ttl` | `first_ttl` | 1–255 | 1 |
| `-m max_ttl` | `max_hops` | 1–255 | 30 |
| `-q nqueries` | `nprobes` | 1–10 (`MAX_PROBES`) | 3 |

Validation lives in `parse_optval()` ([cli.md](cli.md)). Invalid values
abort before any hop runs.

### Intended uses

| Goal | Example | Effect |
|------|---------|--------|
| Skip near hops | `-f 5` | First printed line is ` 5`; hops 1–4 never probed |
| Cap far hops | `-m 10` | Never send with TTL > 10 |
| Both | `-f 3 -m 8` | Probes TTL 3,4,5,6,7,8 only |
| Faster / quieter | `-q 1` | One probe (and one RTT or `*`) per hop |
| More samples | `-q 5` | Five probes per hop (more ECMP visibility) |

### Header vs first line

```
traceroute to host (ip), <max_hops> hops max, 60 byte packets
```

- `max_hops` **is** in the header.
- `first_ttl` is **not**. With `-f 5`, users still see “30 hops max”
  (unless `-m` changed it); the first hop line simply starts at ` 5`.

### Edge: `first_ttl > max_hops`

There is **no** cross-check that `first_ttl <= max_hops`. Example:

```
./ft_traceroute -f 10 -m 5 example.com
```

After the header, `tr.ttl = 10` and `10 <= 5` is false → the loop body
never runs → exit 0 with only the header printed. Useful to know when
debugging odd CLI combinations.

### Edge: destination closer than `-f`

If the real path has 4 hops but the user passes `-f 6`, the first probes
already have TTL 6. They may reach the destination immediately (Port
Unreachable on the first printed hop) or reveal a mid-path router if
TTL 6 still expires somewhere. Early hops are simply never mapped.

### Edge: `-m` smaller than real path length

The walk stops at `max_hops` even without Port Unreachable. Exit status
is still `0`. Output ends with whatever the last hop showed (often
stars or intermediate routers).

---

## Setting TTL for the hop — `set_ttl()`

```c
setsockopt(tr->send_sock, IPPROTO_IP, IP_TTL, &ttl, sizeof(ttl));
```

### When

Once per outer-loop iteration, **immediately before** zeroing probes and
sending. Not between individual probes of the same hop.

### What it affects

Only packets leaving through `send_sock` (UDP). The kernel stamps the
chosen TTL on each outbound IP header until the option is changed again
for the next hop.

### What it does not affect

- The raw ICMP **receive** socket: no TTL option needed; it only
  listens for replies generated by others.
- Already-in-flight packets from a previous hop (there should be none
  with sequential probing, aside from late ICMP that may still arrive
  and be ignored by port matching).

### Failure

`setsockopt` failure → `fatal_error("setsockopt IP_TTL")`. Continuing
with a stale TTL would attribute the wrong router to the printed hop
number, so the program aborts rather than produce a false map.

---

## Life of one hop (probe by probe)

### 1. Prepare

```c
set_ttl(&tr, tr.ttl);
memset(probes, 0, sizeof(t_probe) * tr.nprobes);
```

All `received` flags start at 0; addresses and RTTs are empty until a
match.

### 2. Send (`send_probe`)

For each probe index `p`:

1. Choose unique dest port from base `tr->port` and global `tr->seq`.
2. Zero 32-byte payload; build `sockaddr_in` to `dest_addr` + that port.
3. `gettimeofday` → `probe->send_time`.
4. `sendto` on the UDP socket (TTL already set for this hop).
5. Increment `tr->seq` (even if `sendto` failed).

`sendto` failure prints to stderr but does **not** abort the hop; that
probe usually becomes `*` after recv timeout.

Port formula and packet layout: [udp.md](udp.md), [probing.md](probing.md).

### 3. Receive (`recv_probe`)

Wait up to `wait_time` with adaptive `select()`:

- Accept only ICMP type 11 (Time Exceeded) or type 3 (Unreachable).
- Require inner quoted protocol = UDP and dest port = this probe’s port.
- On match: store ICMP type/code, hop IP (`inet_ntoa(from.sin_addr)`),
  `received = 1`, `rtt = calc_rtt(...)`.
- On timeout / no match: leave `received = 0`.

Unrelated ICMP on the raw socket is discarded without resetting the
full timeout (remaining time shrinks). See [probing.md](probing.md).

### 4. Print (`print_hop`)

One line for the whole hop (see next section).

### 5. Decide

`check_reached`: if **any** probe has Port Unreachable → `break`.
Otherwise `tr.ttl++` and repeat.

---

## Interpreting replies at a hop

### Intermediate hop (Time Exceeded)

| Field | Typical value |
|-------|----------------|
| `icmp_type` | 11 (`FT_ICMP_TIMXCEED`) |
| `icmp_code` | usually 0 (in-transit) |
| `addr_str` | router IP |
| Annotation | none |
| Loop | continue |

### Final hop (Port Unreachable)

| Field | Typical value |
|-------|----------------|
| `icmp_type` | 3 (`FT_ICMP_UNREACH`) |
| `icmp_code` | 3 (`FT_UNREACH_PORT`) |
| `addr_str` | destination (usually) |
| Annotation | none (code 3 is silent in `print_annotation`) |
| Loop | stop after this line |

### Other Destination Unreachable codes

Printed with annotations (`!N`, `!H`, `!P`, `!F`, `!S`, `!X`, or
`!<code>`). They **do not** satisfy `check_reached`, so the traceroute
**continues** to higher TTLs. Operators still see that a router reported
a hard error at this hop.

| Code | Macro | Annotation |
|------|-------|------------|
| 0 | `FT_UNREACH_NET` | `!N` |
| 1 | `FT_UNREACH_HOST` | `!H` |
| 2 | `FT_UNREACH_PROTO` | `!P` |
| 3 | `FT_UNREACH_PORT` | (none; stop) |
| 4 | `FT_UNREACH_FRAG` | `!F` |
| 5 | `FT_UNREACH_SR` | `!S` |
| 9 / 10 / 13 | admin prohibited | `!X` |
| other type 3 | — | `!<code>` |

Full display rules: [output.md](output.md).

### Timeout (`*`)

`received == 0`. Common causes:

- Router rate-limits or suppresses ICMP Time Exceeded.
- Firewall drops probes or return ICMP.
- Path blackhole / asymmetric return path.
- Truncated ICMP quotes failing length checks (appear as timeout).
- `sendto` failed earlier.

A hop of only stars never stops the walk by itself.

### Mixed results on one hop

Probes are independent. One hop line can mix addresses, RTTs, stars,
and annotations, e.g.:

```
 7  203.0.113.1  10.100 ms  *  203.0.113.1  11.200 ms !H
```

Stop still requires **at least one** Port Unreachable among the probes.

---

## Printing a hop — `print_hop()`

### Layout

```
<hop>  <addr?>  <rtt> ms [annotation]  …  \n
```

Algorithm:

1. `printf("%2d", tr->ttl)` — width 2, right-aligned (` 1` … `10` …).
2. `last_addr = ""`.
3. For each probe `i` in `0 .. nprobes-1` (send order, never sorted by RTT):
   - If `!received` → print `  *` and continue.
   - If `addr_str != last_addr` → `print_addr(...)`, update `last_addr`.
   - Print `  %.3f ms`.
   - `print_annotation(icmp_type, icmp_code)`.
4. Print `\n`.

### Address suppression (same hop, same IP)

Classic traceroute prints the router identity once, then only RTTs for
later probes from the **same** IP. `last_addr` implements that.

### ECMP / load balancing (same hop, different IPs)

Equal-cost multipath can send probes with the same TTL through
different next hops. Each **new** address is printed before its RTT:

```
 4  198.51.100.1  3.000 ms  203.0.113.9  4.000 ms  4.100 ms
```

Here probes 2 and 3 shared the second address; the third address print
is suppressed.

### DNS vs `-n`

| Mode | `print_addr` output |
|------|---------------------|
| Default | `  hostname (a.b.c.d)` via `getnameinfo` |
| PTR miss | `  a.b.c.d (a.b.c.d)` |
| `-n` | `  a.b.c.d` only |

DNS runs **after** all probes of the hop finished, so it delays the
line but does not change RTT values already measured.

### Hop number formatting

| `ttl` | Printed prefix |
|-------|----------------|
| 1–9 | leading space (` 1` … ` 9`) |
| 10–99 | two digits (`10` … `30` typical) |
| 100–255 | still `"%2d"` (width expands as needed for larger values) |

---

## Stop condition — `check_reached()`

```c
received
&& icmp_type == FT_ICMP_UNREACH   /* 3 */
&& icmp_code == FT_UNREACH_PORT   /* 3 */
```

Called **after** `print_hop` with the current hop’s probe array.

| Situation | Continues? |
|-----------|------------|
| Any Port Unreachable on this hop | No — `break` |
| Only Time Exceeded | Yes |
| `!H` / `!N` / other annotations | Yes |
| All `*` | Yes |
| `ttl == max_hops` and not reached | No more iterations; fall through to cleanup |
| Never reached destination | Exit `0` anyway |

Notes:

- **One** matching Port Unreachable among `nprobes` is enough.
- Time Exceeded never stops, even if the address equals the target IP
  (unusual, but possible with weird middleboxes).
- `tr->reached` is initialized to `false` and is **not** assigned by the
  current main loop; stop uses the function return value only.

---

## Worked examples

### Full path (defaults, conceptual)

```
traceroute to 1.1.1.1 (1.1.1.1), 30 hops max, 60 byte packets
 1  192.168.1.1  1.234 ms  1.100 ms  1.050 ms
 2  *  *  *
 3  203.0.113.1  12.000 ms  198.51.100.2  11.500 ms  11.800 ms
 4  1.1.1.1  15.200 ms  15.100 ms  15.050 ms
```

| Line | What happened |
|------|----------------|
| 1 | Three Time Exceeded from the LAN gateway; same IP → address once |
| 2 | No matching ICMP in 5 s × 3; continue |
| 3 | ECMP: two router IPs at TTL 3 |
| 4 | Port Unreachable from target; printed then loop stops |

### With `-f 3 -m 8 -q 1 -n`

- Header still says hops max from `-m` (8).
- Only TTL 3…8 are probed; one probe each.
- Addresses are numeric only.
- If Port Unreachable appears at hop 5, lines 6–8 are never printed.

### Destination never reached

```
sudo ./ft_traceroute -n -m 5 10.255.255.1
```

Often five hop lines (many `*`), then exit 0. No fatal error: “not
reached” is a normal outcome when max TTL is exhausted.

### Annotated hop that does not stop

```
 6  198.51.100.50  20.000 ms !H  20.100 ms !H  19.900 ms !H
 7  …
```

Host unreachable is shown, but `check_reached` is false → TTL 7 still
runs.

---

## End-to-end timeline (single hop, `-q 1`)

Assume `ttl = 2`, `seq = 1`, base port 33434.

1. `set_ttl(2)` — subsequent UDP packets leave with TTL 2.
2. `send_probe` — dest port 33435, stamp `send_time`, `sendto`.
3. First router decrements TTL → 1 and forwards.
4. Second router decrements TTL → 0, drops, sends ICMP Time Exceeded.
5. `recv_probe` matches quoted UDP port 33435; stores router IP + RTT.
6. `print_hop` → ` 2  <router>  x.xxx ms`.
7. `check_reached` false (type 11) → `ttl = 3`.

When TTL finally reaches the host:

5b. ICMP Port Unreachable, code 3; address usually equals target.
6b. Hop line without `!`.
7b. `check_reached` true → `break`.

---

## Relationship to other modules

```
CLI (-f/-m/-q/-n)
    │
    ▼
resolve + sockets
    │
    ▼
┌───────────────────────────────────────┐
│  for each hop (ttl):                  │
│    set_ttl          ← networking      │
│    send/recv × q    ← probing / udp   │
│    print_hop        ← output          │
│    check_reached    ← stop            │
└───────────────────────────────────────┘
```

| Concern | Document |
|---------|----------|
| Option parsing & ranges | [cli.md](cli.md) |
| Sockets & `IP_TTL` | [networking.md](networking.md) |
| UDP ports & matching | [udp.md](udp.md) |
| Send/recv/RTT details | [probing.md](probing.md) |
| Line format & annotations | [output.md](output.md) |
| Manual tests for hops | [TESTING.md](TESTING.md) (N4, N5, N6, …) |

---

## Module map

| Piece | File | Role |
|-------|------|------|
| Hop loop + `check_reached` | `main.c` | Orchestrate TTL walk and stop |
| `set_ttl` | `socket.c` | Apply hop TTL before probes |
| `send_probe` | `send.c` | One UDP probe for current hop |
| `recv_probe` / `parse_icmp` | `recv.c` | Wait for matching ICMP; hop IP |
| `calc_rtt` | `time.c` | Milliseconds for hop line |
| `print_hop` / annotations | `display.c` | One stdout line per hop |
| `-f` / `-m` / `-q` / `-n` | `args.c` | Configure hop bounds & display |

---

## Quick reference

| Topic | Rule in this codebase |
|-------|------------------------|
| Hop number | Equal to current `IP_TTL` / `tr->ttl` |
| Default walk | TTL 1 … 30, 3 probes each |
| Intermediate reply | ICMP Time Exceeded → print, continue |
| Final reply | ICMP Port Unreachable → print, stop |
| Soft errors (`!H`, …) | Print annotation, continue |
| Silence | `*`, continue |
| Max TTL hit | Stop loop, exit 0 |
| `first_ttl > max_hops` | Header only, no hop lines |
