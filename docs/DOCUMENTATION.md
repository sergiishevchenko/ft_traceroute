# ft_traceroute — Technical Documentation

## Overview

`ft_traceroute` is a from-scratch reimplementation of the classic UNIX
`traceroute` utility in C. It discovers the sequence of routers (hops)
between the local machine and a remote IPv4 host by sending UDP probes
with increasing IP Time-To-Live (TTL) values and interpreting the ICMP
error messages those probes provoke.

The project targets the 42 school subject: allowed/forbidden libc
functions, root-required raw ICMP, UDP-based tracing, and output that
closely matches system `traceroute`.

### What this document covers

This file is the **project-wide** technical reference: algorithm,
architecture, data model, modules, protocols, CLI, build, and subject
constraints.

For deep dives into each concern, see:

| Document | Topic |
|----------|--------|
| [structures.md](structures.md) | Constants, `t_probe` / `t_traceroute`, `timeval`, `sockaddr_in` |
| [cli.md](cli.md) | Argument parsing, validation, root check |
| [networking.md](networking.md) | DNS resolution, sockets, TTL |
| [udp.md](udp.md) | UDP deep dive: wire format, ports, matching, edge cases |
| [hops.md](hops.md) | Hop discovery, TTL loop, bounds, stop rules |
| [probing.md](probing.md) | Main loop, send/recv, RTT, stop condition |
| [output.md](output.md) | Header, hop lines, reverse DNS, annotations |
| [TESTING.md](TESTING.md) | Manual test plan |
| [en.subject.pdf](en.subject.pdf) | Official subject PDF |

---

## How traceroute works

### The TTL trick

Every IPv4 packet carries an 8-bit TTL. Each router that forwards the
packet decrements TTL by one. When TTL reaches 0, the router **must not**
forward the packet; it discards it and should send an ICMP
**Time Exceeded** message back to the original sender. The source address
of that ICMP message is the router’s own IP — which is exactly the hop
identity traceroute wants to display.

By sending the first probe with TTL = 1, the program maps the first
router. TTL = 2 maps the second, and so on, until the datagram finally
survives all the way to the destination.

### Why UDP to a high port?

This implementation (like traditional traceroute) uses **UDP** probes
aimed at an unlikely destination port (base **33434** by default):

1. Intermediate routers still generate Time Exceeded when TTL expires —
   they do not need a UDP listener.
2. When the packet reaches the target host, that port is almost never
   accepting datagrams, so the host replies with ICMP **Destination
   Unreachable / Port Unreachable**.
3. That Port Unreachable reply is the “destination reached” signal.

Each probe uses a distinct destination port (`base + sequence`) so that
when an ICMP error embeds a copy of the original UDP header, the program
can match the reply to the exact probe that caused it.

### Algorithm (step by step)

1. Resolve the target hostname or IPv4 string to a `sockaddr_in`.
2. Open a UDP socket (send) and a raw ICMP socket (receive).
3. For `ttl` from `first_ttl` to `max_hops`:
   1. `setsockopt(IP_TTL)` on the send socket.
   2. Send `nprobes` UDP datagrams (default 3), each with a unique dest
      port; record send timestamps.
   3. For each probe, wait up to `wait_time` seconds (`select` +
      `recvfrom`) for a matching ICMP error; compute RTT.
   4. Print the hop line (addresses, RTTs, `*`, annotations).
   5. If any probe received Port Unreachable → stop.
4. Close sockets and exit successfully.

### Path diagram

```
Host ──TTL=1──► Router1 ──ICMP Time Exceeded──────────────────────► Host
Host ──TTL=2──► Router1 ──► Router2 ──ICMP Time Exceeded──────────► Host
Host ──TTL=3──► Router1 ──► Router2 ──► Target
                                         └── ICMP Port Unreachable──► Host
```

### What `*` means

If no matching ICMP arrives before the per-probe timeout, that probe is
shown as `*`. Common causes: ICMP filtered by a firewall, rate-limited
routers, asymmetric routing that drops return traffic, or a silent hop.
The program still advances TTL; one timed-out hop does not abort the
trace (unless `max_hops` is reached).

---

## Architecture

### Repository layout

```
ft_traceroute/
├── Makefile
├── README.md
├── includes/
│   └── ft_traceroute.h      Structs, prototypes, constants
├── srcs/
│   ├── main.c               Entry point, init, TTL loop, stop check
│   ├── args.c               CLI parsing and validation
│   ├── resolve.c            Hostname / IP → sockaddr_in
│   ├── socket.c             Socket create, TTL, close
│   ├── send.c               UDP probe transmission
│   ├── recv.c               ICMP wait, parse, probe match
│   ├── time.c               RTT calculation
│   ├── display.c            Header, hop lines, DNS, annotations
│   └── utils.c              fatal_error()
├── objs/                    Object files (build)
├── test.sh                  Helper test script
└── docs/
    ├── DOCUMENTATION.md     This file
    ├── structures.md        Constants, structs, timeval, sockaddr_in
    ├── cli.md
    ├── networking.md
    ├── probing.md
    ├── output.md
    ├── TESTING.md
    └── en.subject.pdf
```

### Module dependency (runtime order)

```
main
 ├─ args          (CLI → t_traceroute fields)
 ├─ resolve       (host → dest_addr, resolved_ip)
 ├─ socket        (recv_sock, send_sock; set_ttl per hop)
 ├─ send          (UDP probes)
 ├─ recv          (ICMP + parse; uses time.calc_rtt)
 ├─ display       (stdout formatting)
 └─ utils         (fatal abort path)
```

Layers are intentionally thin: each `.c` file owns a small surface of
prototypes declared in `ft_traceroute.h`. There is no dynamic allocation
of session state — `t_traceroute` and `t_probe[]` live on the stack in
`main`.

### Design choices

| Choice | Rationale |
|--------|-----------|
| UDP + ICMP (not TCP SYN / ICMP echo) | Matches classic traceroute and the subject |
| IPv4 only (`AF_INET`) | Subject scope |
| Serial probes (send then wait) | Simple port matching; no probe pipeline |
| Unique UDP dest port per probe | Identifies replies via ICMP quote |
| `select` for timeouts | `poll` / `ppoll` / `fcntl` forbidden |
| Root check in CLI | Fails fast before DNS/sockets |
| Zero payload | Content unused for path discovery |

---

## Constants (`ft_traceroute.h`)

| Macro | Value | Meaning |
|-------|-------|---------|
| `PROGRAM_NAME` | `"ft_traceroute"` | Prefix for errors / help |
| `DEFAULT_PORT` | `33434` | Base UDP destination port |
| `DEFAULT_MAX_HOPS` | `30` | Default `-m` |
| `DEFAULT_NPROBES` | `3` | Default `-q` |
| `DEFAULT_PACKET_SIZE` | `60` | Size shown in header |
| `DEFAULT_WAIT_TIME` | `5.0` | Seconds per probe wait |
| `PAYLOAD_SIZE` | `32` | UDP payload bytes |
| `RECV_BUFF_SIZE` | `512` | ICMP receive buffer |
| `MAX_PROBES` | `10` | Hard cap for `-q` and stack array |

ICMP-related macros: `FT_ICMP_UNREACH` (3), `FT_ICMP_TIMXCEED` (11),
and `FT_UNREACH_*` codes for annotations / stop detection.

Packet size identity:

```
60 = 20 (IPv4 header, no options) + 8 (UDP header) + 32 (payload)
```

---

## Data Structures

### `t_traceroute` — session state

Holds everything needed for one traceroute run.

| Field | Type | Purpose | Set by |
|-------|------|---------|--------|
| `host` | `char *` | Target string from argv (not owned/copied) | CLI |
| `resolved_ip` | `char[INET_ADDRSTRLEN]` | Printable IPv4 | resolve |
| `dest_addr` | `sockaddr_in` | Binary target address | resolve |
| `send_sock` | `int` | UDP FD (`-1` until open) | socket |
| `recv_sock` | `int` | Raw ICMP FD | socket |
| `port` | `int` | Base destination port | init / `-p` |
| `max_hops` | `int` | Loop upper bound | init / `-m` |
| `nprobes` | `int` | Probes per hop | init / `-q` |
| `packet_size` | `int` | Header display size (60) | init |
| `wait_time` | `double` | Per-probe timeout (seconds) | init |
| `ttl` | `int` | Current hop / TTL | main loop |
| `seq` | `int` | Global probe counter → unique ports | send |
| `reached` | `bool` | Present in struct; stop uses `check_reached()` return | init |
| `no_dns` | `bool` | Skip reverse DNS on hops | `-n` |
| `first_ttl` | `int` | Starting TTL | init / `-f` |

### `t_probe` — one probe attempt

One element per probe within a hop. The hop’s array is zeroed before
each TTL iteration.

| Field | Type | Purpose |
|-------|------|---------|
| `send_time` | `struct timeval` | Just before `sendto` |
| `recv_time` | `struct timeval` | Just after matching `recvfrom` |
| `rtt` | `double` | Round-trip time in milliseconds |
| `addr_str` | `char[INET_ADDRSTRLEN]` | ICMP sender IP (the hop) |
| `received` | `int` | `1` reply, `0` timeout |
| `icmp_type` | `int` | e.g. 11 or 3 |
| `icmp_code` | `int` | e.g. 3 = Port Unreachable |
| `port` | `int` | UDP dest port used for matching |

---

## Program Flow

```
main()
 ├── init_traceroute()         Zero state + defaults
 ├── parse_args()              Options, host, root check
 ├── resolve_host()            getaddrinfo → dest_addr
 ├── create_sockets()          SOCK_RAW ICMP + SOCK_DGRAM UDP
 ├── print_header()            "traceroute to …"
 └── while ttl ∈ [first_ttl, max_hops]
      ├── set_ttl(ttl)         setsockopt(IP_TTL)
      ├── memset probes
      ├── for p in 0 … nprobes-1
      │    ├── send_probe()    unique port, sendto, send_time
      │    └── recv_probe()    select until match or timeout
      ├── print_hop()          addresses, RTTs, *, annotations
      └── if check_reached()   Port Unreachable → break
           else ttl++
 └── close_sockets()
 └── return 0
```

### Stop condition — `check_reached()`

Returns true if **any** probe in the hop has:

```
received == 1
&& icmp_type == 3          /* Destination Unreachable */
&& icmp_code == 3          /* Port Unreachable */
```

Time Exceeded and other unreachable codes do **not** stop the loop.
If the destination never answers, the program ends after `max_hops`
and still returns exit code 0.

### Timing cost

Probes are **serial**: each `recv_probe` finishes before the next
`send_probe`. A fully silent hop with defaults can take roughly
`3 × 5s = 15` seconds of wall time before the next hop starts.

---

## Module Details

Summaries below. Full narratives live in the linked feature docs.

### `main.c` — Orchestration

- `init_traceroute()` — defaults listed in [Defaults](#defaults).
- Outer TTL loop and inner probe loop.
- `check_reached()` — Port Unreachable detection.
- Always closes sockets on the normal path.

See [probing.md](probing.md).

### `args.c` — Argument parsing

- Parses `--help`, `-n`, `-f`, `-m`, `-q`, `-p`, and exactly one host.
- Numeric options go through `ft_atoi_safe()` (digits only) and range
  checks.
- Requires `getuid() == 0` after parsing (help works without root).
- Errors to stderr; process exits on failure.

See [cli.md](cli.md).

### `resolve.c` — DNS / address resolution

- `getaddrinfo` with `AF_INET` + `SOCK_DGRAM`.
- First result only → `dest_addr`.
- Printable IP via `inet_ntoa` → `resolved_ip`.
- Failures use `gai_strerror` and exit.

See [networking.md](networking.md).

### `socket.c` — Socket management

| Socket | Domain | Type | Protocol | Role |
|--------|--------|------|----------|------|
| `recv_sock` | AF_INET | SOCK_RAW | IPPROTO_ICMP | Capture ICMP errors |
| `send_sock` | AF_INET | SOCK_DGRAM | IPPROTO_UDP | Emit probes |

- `set_ttl()` — `setsockopt(..., IP_TTL, ...)` before each hop.
- `close_sockets()` — close both FDs if `>= 0`.
- Creation failure → `fatal_error()`.

See [networking.md](networking.md).

### `send.c` — Probe transmission

1. Port: `((port + seq - 1) % 65535) + 1`
2. 32-byte zero payload
3. `gettimeofday` → `send_time`
4. `sendto()` toward `dest_addr.sin_addr` and probe port
5. Increment `seq`

`sendto` errors are printed but non-fatal; the probe usually times out.

See [probing.md](probing.md).

### `recv.c` — Response reception

Uses `select()` with an **adaptive** remaining timeout so unrelated ICMP
noise does not reset the full deadline.

On receipt, parse the raw packet:

```
┌─────────────────────────────────┐
│ Outer IP header (IHL × 4)       │
├─────────────────────────────────┤
│ ICMP header (≥ 8 bytes)         │  ← type + code
├─────────────────────────────────┤
│ Inner IP header (IHL × 4)       │  ← original failing packet
│   protocol must be UDP          │
├─────────────────────────────────┤
│ Inner UDP header                │  ← dest port for matching
└─────────────────────────────────┘
```

Accept only Time Exceeded or Destination Unreachable with an embedded
UDP dest port equal to `probe->port`. Populate `addr_str`, RTT, type,
and code.

See [probing.md](probing.md).

### `time.c` — RTT calculation

```
RTT_ms = (recv.tv_sec − send.tv_sec) × 1000.0
       + (recv.tv_usec − send.tv_usec) / 1000.0
```

Returned as `double`; printed later with `%.3f ms`.

### `display.c` — Output formatting

**Header:**

```
traceroute to <host> (<IP>), <max_hops> hops max, <packet_size> byte packets
```

**Hop line behavior:**

- Hop number: `"%2d"`.
- Timeout → `  *`.
- Print address only when it changes within the hop (ECMP-aware).
- Default: `hostname (ip)` via `getnameinfo`; `-n`: IP only.
- Unreachable annotations: `!N`, `!H`, `!P`, `!F`, `!S`, `!X`, or
  `!<code>` (Port Unreachable prints nothing).

See [output.md](output.md).

### `utils.c` — Fatal errors

```c
fprintf(stderr, "%s: %s: %s\n", PROGRAM_NAME, msg, strerror(errno));
exit(EXIT_FAILURE);
```

Used for hard failures (socket/`setsockopt`), not for soft CLI/DNS
messages that format their own stderr text.

---

## Sockets and Protocols

| Role | Type | Protocol | Direction |
|------|------|----------|-----------|
| Send probes | SOCK_DGRAM | IPPROTO_UDP | Outbound to target |
| Receive errors | SOCK_RAW | IPPROTO_ICMP | Inbound ICMP to local host |

TTL is controlled on the UDP socket only. The raw ICMP socket receives
kernel-delivered error packets that typically include the outer IP
header (Linux), which the parser expects.

**Privileges:** opening `SOCK_RAW` / `IPPROTO_ICMP` requires root (or
equivalent capability). `parse_args` enforces `getuid() == 0`.

---

## ICMP Response Types

| Type | Code | Meaning | Program action |
|------|------|---------|----------------|
| 11 | 0 | Time Exceeded | Intermediate hop; print RTT; continue |
| 3 | 3 | Port Unreachable | Destination reached; print RTT (no `!`); stop after hop |
| 3 | 0 | Network Unreachable | Print `!N`; continue TTL loop |
| 3 | 1 | Host Unreachable | Print `!H`; continue |
| 3 | 2 | Protocol Unreachable | Print `!P`; continue |
| 3 | 4 | Fragmentation Needed | Print `!F`; continue |
| 3 | 5 | Source Route Failed | Print `!S`; continue |
| 3 | 9 / 10 / 13 | Admin Prohibited | Print `!X`; continue |
| 3 | other | Other unreachable | Print `!<code>`; continue |
| other | * | Ignored at parse | Wait / retry until timeout |

---

## Command-Line Interface

```
Usage:
  ft_traceroute [-n] [-m max_ttl] [-q nqueries] [-p port] [-f first_ttl] host
```

| Option | Effect | Range | Default |
|--------|--------|-------|---------|
| `--help` | Print help, exit 0 | — | — |
| `-n` | No reverse DNS on hop lines | flag | off |
| `-f first_ttl` | Starting TTL | 1–255 | 1 |
| `-m max_ttl` | Maximum hops | 1–255 | 30 |
| `-q nqueries` | Probes per hop | 1–10 | 3 |
| `-p port` | Base UDP destination port | 1–65535 | 33434 |
| `host` | Hostname or IPv4 (required) | — | — |

Notes:

- Exactly one host operand; options may appear before or after it.
- Combined short options (`-nm`) are not supported.
- Prefer `first_ttl ≤ max_hops`; otherwise no hop lines are produced
  after the header.
- Root is required for normal runs; `--help` works unprivileged.

Full validation details: [cli.md](cli.md).

---

## Defaults

| Parameter | Value | Configurable via CLI? |
|-----------|-------|------------------------|
| First TTL | 1 | yes (`-f`) |
| Max hops | 30 | yes (`-m`) |
| Probes per hop | 3 | yes (`-q`) |
| Base port | 33434 | yes (`-p`) |
| Wait timeout | 5.0 s | no |
| Packet size (display) | 60 bytes | no |
| Reverse DNS | enabled | disable with `-n` |

---

## Output format

### Header example

```
traceroute to google.com (142.250.185.78), 30 hops max, 60 byte packets
```

### Hop examples

```
 1  gateway.lan (192.168.1.1)  1.204 ms  0.980 ms  0.912 ms
 2  *  *  *
 3  10.0.0.1  8.100 ms  203.0.113.9  9.200 ms  8.050 ms
 4  93.184.216.34  20.001 ms  19.870 ms  19.920 ms
```

With `-n`, hostnames are omitted (`  192.168.1.1` instead of
`gateway.lan (192.168.1.1)`).

More formatting rules: [output.md](output.md).

---

## Build

Requirements: `gcc`, `make`, Linux-like environment with raw sockets,
root for execution.

```bash
make          # build ft_traceroute
make clean    # remove objs/
make fclean   # remove objs/ and binary
make re       # fclean + all
```

Compiler flags: `-Wall -Wextra -Werror`.  
Include path: `-Iincludes`.  
Sources listed in the Makefile are compiled into `objs/` then linked
into `./ft_traceroute`.

---

## Usage Examples

```bash
# Typical run
sudo ./ft_traceroute google.com

# Literal IP, no DNS on hops
sudo ./ft_traceroute -n 8.8.8.8

# Shorter path, one probe per hop
sudo ./ft_traceroute -n -m 15 -q 1 1.1.1.1

# Skip early hops, custom base port
sudo ./ft_traceroute -f 5 -p 40000 example.com

# Help (no root)
./ft_traceroute --help
```

---

## Mandatory vs bonus (subject map)

| Feature | Part | Notes |
|---------|------|-------|
| Binary + Makefile (`all`/`clean`/`fclean`/`re`) | mandatory | |
| `--help` | mandatory | |
| IPv4 host / hostname via `getaddrinfo` | mandatory | |
| UDP traceroute with TTL + ICMP | mandatory | Time Exceeded + Port Unreachable |
| Hop output comparable to real traceroute | mandatory | Header, layout, `*`; RTT tolerance typically ±30 ms vs reference |
| Hop lines without reverse DNS | mandatory baseline | Bonus enables DNS by default; use `-n` for IP-only |
| `-n`, `-f`, `-m`, `-q`, `-p` | bonus | |
| Reverse DNS `host (ip)` | bonus | `getnameinfo` |
| Annotations `!N` `!H` … | bonus | |

Always confirm against [en.subject.pdf](en.subject.pdf) for the grading
scale in force for your session.

---

## Allowed Functions (Mandatory)

From the subject / evaluation constraints (non-exhaustive naming of the
usual set):

`getpid`, `getuid`, `getaddrinfo`, `gettimeofday`, `getnameinfo`,
`gethostbyaddr`, `inet_ntoa`, `inet_pton`, `freeaddrinfo`, `exit`,
`select`, `setsockopt`, `recvfrom`, `sendto`, `ntohs`, `htons`,
`bind`, `socket`, `close`, `strerror`, `gai_strerror`,
printf-family functions, and libft-equivalent functions
(`read`, `write`, `malloc`, `free`).

This codebase uses a subset of the above (e.g. `getaddrinfo`,
`getnameinfo`, `select`, sockets API, `gettimeofday`, …).

## Forbidden Functions

`fcntl`, `poll`, `ppoll` — strictly prohibited.

Timeouts therefore use `select()` exclusively (see `recv.c`).

---

## Limitations & known behaviors

1. **IPv4 only** — no IPv6 traceroute.
2. **UDP mode only** — no TCP or ICMP-echo probe modes.
3. **Serial probes** — slower on blackhole hops than pipelined variants.
4. **No `first_ttl ≤ max_hops` check** in CLI — misconfiguration yields
   header only.
5. **Truncated ICMP quotes** from some routers may fail length checks and
   appear as `*`.
6. **Firewalls dropping ICMP** produce stars even when UDP egress works.
7. **`tr->reached`** is initialized but the loop stops via
   `check_reached()`’s return value, not by writing that field.
8. **First A record only** — multi-homed names always use the first
   `getaddrinfo` result for the whole run.

---

## Related documentation

| File | Description |
|------|-------------|
| [cli.md](cli.md) | CLI layer in depth |
| [networking.md](networking.md) | Resolution & sockets in depth |
| [probing.md](probing.md) | Probe engine in depth |
| [output.md](output.md) | Display layer in depth |
| [TESTING.md](TESTING.md) | Manual tests and expected results |
| [en.subject.pdf](en.subject.pdf) | Official subject |
| [../README.md](../README.md) | Project README (build & quick start) |
