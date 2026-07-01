# ft_traceroute — Technical Documentation

## Overview

`ft_traceroute` is a reimplementation of the UNIX `traceroute` utility.
It traces the route IP packets take from the local machine to a remote host
by exploiting the IP Time-To-Live (TTL) mechanism.

## How traceroute works

1. A UDP datagram is sent to the target host on an unlikely destination port
   (starting at 33434) with TTL set to 1.
2. The first router on the path decrements TTL to 0, discards the packet,
   and replies with an **ICMP Time Exceeded** message.
3. The program records the router's IP address and the round-trip time (RTT).
4. TTL is incremented by 1 and the process repeats.
5. When the packet finally reaches the destination host, it replies with
   **ICMP Destination Unreachable (Port Unreachable)** because the destination
   port is not in use.
6. Three probes are sent per TTL hop by default.

```
Host ──TTL=1──► Router1 ──ICMP Time Exceeded──► Host
Host ──TTL=2──► Router1 ──► Router2 ──ICMP Time Exceeded──► Host
Host ──TTL=3──► Router1 ──► Router2 ──► Target ──ICMP Port Unreachable──► Host
```

## Architecture

```
ft_traceroute/
├── Makefile
├── includes/
│   └── ft_traceroute.h      Main header (structs, prototypes, constants)
└── srcs/
    ├── main.c                Entry point, main loop
    ├── args.c                Argument parsing and validation
    ├── resolve.c             DNS resolution (hostname → IPv4)
    ├── socket.c              Socket creation, TTL management, cleanup
    ├── send.c                UDP probe transmission
    ├── recv.c                ICMP response reception and parsing
    ├── time.c                RTT calculation
    ├── display.c             Output formatting (header, hops, annotations)
    └── utils.c               Error handling helpers
```

## Data Structures

### t_traceroute

Holds the global program state.

| Field          | Type               | Purpose                              |
|----------------|--------------------|--------------------------------------|
| `host`         | `char *`           | Target hostname/IP from argv         |
| `resolved_ip`  | `char[]`           | Resolved IPv4 string                 |
| `dest_addr`    | `sockaddr_in`      | Target socket address                |
| `send_sock`    | `int`              | UDP socket file descriptor           |
| `recv_sock`    | `int`              | Raw ICMP socket file descriptor      |
| `port`         | `int`              | Base destination port (default 33434)|
| `max_hops`     | `int`              | Maximum TTL (default 30)             |
| `nprobes`      | `int`              | Probes per hop (default 3)           |
| `packet_size`  | `int`              | Total packet size for display (60)   |
| `wait_time`    | `double`           | Timeout per probe in seconds (5.0)   |
| `ttl`          | `int`              | Current TTL value                    |
| `seq`          | `int`              | Global probe sequence counter        |
| `reached`      | `bool`             | Whether destination was reached      |
| `no_dns`       | `bool`             | Skip reverse DNS (`-n` flag)         |
| `first_ttl`    | `int`              | Starting TTL (`-f` flag, default 1)  |

### t_probe

Holds per-probe state for a single TTL hop.

| Field       | Type            | Purpose                             |
|-------------|-----------------|-------------------------------------|
| `send_time` | `struct timeval` | Timestamp when probe was sent       |
| `recv_time` | `struct timeval` | Timestamp when reply was received   |
| `rtt`       | `double`        | Round-trip time in milliseconds     |
| `addr_str`  | `char[]`        | Responding router's IP (string)     |
| `received`  | `int`           | 1 if reply received, 0 if timeout   |
| `icmp_type` | `int`           | ICMP message type                   |
| `icmp_code` | `int`           | ICMP message code                   |
| `port`      | `int`           | UDP destination port used           |

## Program Flow

```
main()
 ├── init_traceroute()         Set defaults
 ├── parse_args()              Parse CLI options and host
 ├── resolve_host()            Resolve hostname → sockaddr_in
 ├── create_sockets()          Open raw ICMP + UDP sockets
 ├── print_header()            Print "traceroute to ..." line
 └── loop TTL = first_ttl → max_hops
      ├── set_ttl()            setsockopt(IP_TTL)
      ├── loop probes 0 → nprobes-1
      │    ├── send_probe()    sendto() UDP packet
      │    └── recv_probe()    select() + recvfrom() ICMP reply
      ├── print_hop()          Format and print hop line
      └── check_reached()      Break if PORT_UNREACHABLE received
```

## Module Details

### args.c — Argument Parsing

Parses command-line arguments. Handles `--help`, validates flags (`-n`, `-f`,
`-m`, `-q`, `-p`), extracts the target host, and verifies root privileges
via `getuid()`. Exits with an error for unknown flags or missing arguments.

### resolve.c — DNS Resolution

Converts the target hostname or IP string into a `struct sockaddr_in` using
`getaddrinfo()` with `AF_INET` and `SOCK_DGRAM` hints. Stores the resolved
IP as a printable string via `inet_ntoa()`. Frees the addrinfo list after use.

### socket.c — Socket Management

Creates two sockets:
- **Receive socket**: `SOCK_RAW` with `IPPROTO_ICMP` to capture ICMP replies.
- **Send socket**: `SOCK_DGRAM` with `IPPROTO_UDP` to send UDP probes.

`set_ttl()` applies `setsockopt(IP_TTL)` on the send socket before each hop.

### send.c — Probe Transmission

Sends a 32-byte zero-filled UDP payload to the destination. Each probe uses
a unique destination port (`base_port + seq`) for identification. The send
timestamp is recorded via `gettimeofday()` immediately before `sendto()`.

Packet size breakdown: 20 (IP header) + 8 (UDP header) + 32 (payload) = 60 bytes.

### recv.c — Response Reception

Uses `select()` with an adaptive timeout (remaining time recalculated on each
iteration) to wait for ICMP replies. On receipt, the ICMP packet is parsed:

```
┌─────────────────────────────────┐
│ Outer IP header (IHL × 4 bytes) │
├─────────────────────────────────┤
│ ICMP header (8 bytes)           │  ← type + code extracted here
│   type, code, checksum, unused  │
├─────────────────────────────────┤
│ Inner IP header (IHL × 4 bytes) │  ← original packet that caused the error
├─────────────────────────────────┤
│ Inner UDP header (8 bytes)      │  ← destination port extracted here
│   src_port, dst_port, ...       │
└─────────────────────────────────┘
```

The probe is identified by matching the inner UDP destination port against the
expected port. Unrelated ICMP packets are discarded and the loop retries
until the timeout expires.

### time.c — RTT Calculation

Computes RTT in milliseconds from two `struct timeval` timestamps:

```
RTT = (recv.tv_sec − send.tv_sec) × 1000.0
    + (recv.tv_usec − send.tv_usec) / 1000.0
```

### display.c — Output Formatting

**Header**: `traceroute to <host> (<IP>), <max_hops> hops max, <packet_size> byte packets`

**Hop line**: `%2d  <addr>  <rtt> ms  <rtt> ms  <rtt> ms`

- Hop number is right-aligned in a 2-character field.
- If the responding address changes within the same hop, the new address
  is printed before its RTT.
- Timeouts are displayed as `*`.
- ICMP unreachable codes produce annotations: `!N` (network), `!H` (host),
  `!P` (protocol), `!F` (fragmentation needed), `!S` (source route failed),
  `!X` (admin prohibited).

When reverse DNS is enabled (default), `getnameinfo()` resolves each hop's
IP to a hostname, displayed as `hostname (IP)`.

### utils.c — Error Handling

`fatal_error()` prints the error context and `strerror(errno)` to stderr,
then exits with `EXIT_FAILURE`.

## Sockets and Protocols

| Socket   | Type       | Protocol       | Purpose               |
|----------|------------|----------------|------------------------|
| send     | SOCK_DGRAM | IPPROTO_UDP    | Send UDP probes        |
| recv     | SOCK_RAW   | IPPROTO_ICMP   | Receive ICMP responses |

Raw ICMP socket requires root privileges. The program checks `getuid() == 0`
at startup and exits if not running as root.

## ICMP Response Types

| ICMP Type | Code | Meaning                | Action             |
|-----------|------|------------------------|---------------------|
| 11        | 0    | Time Exceeded          | Intermediate hop    |
| 3         | 3    | Port Unreachable       | Destination reached |
| 3         | 0    | Network Unreachable    | Print `!N`          |
| 3         | 1    | Host Unreachable       | Print `!H`          |
| 3         | 2    | Protocol Unreachable   | Print `!P`          |
| 3         | 4    | Fragmentation Needed   | Print `!F`          |
| 3         | 5    | Source Route Failed    | Print `!S`          |
| 3         | 9/10/13 | Admin Prohibited    | Print `!X`          |

## Command-Line Interface

```
Usage:
  ft_traceroute [-n] [-m max_ttl] [-q nqueries] [-p port] [-f first_ttl] host

Arguments:
  host          The host to traceroute to

Options:
  --help        Read this help and exit
  -n            Do not resolve IP addresses to hostnames
  -f first_ttl  Start from the first_ttl hop (default 1)
  -m max_ttl    Set the max number of hops (default 30)
  -q nqueries   Set the number of probes per hop (default 3)
  -p port       Set the destination port base (default 33434)
```

## Defaults

| Parameter      | Value  |
|----------------|--------|
| First TTL      | 1      |
| Max hops       | 30     |
| Probes per hop | 3      |
| Base port      | 33434  |
| Wait timeout   | 5.0s   |
| Packet size    | 60 bytes |

## Build

```bash
make          # build ft_traceroute
make clean    # remove object files
make fclean   # remove object files and binary
make re       # full rebuild
```

## Usage Examples

```bash
sudo ./ft_traceroute google.com
sudo ./ft_traceroute 8.8.8.8
sudo ./ft_traceroute -n -m 15 -q 1 1.1.1.1
sudo ./ft_traceroute -f 5 -p 40000 example.com
```

## Allowed Functions (Mandatory)

`getpid`, `getuid`, `getaddrinfo`, `gettimeofday`, `getnameinfo`,
`gethostbyaddr`, `inet_ntoa`, `inet_pton`, `freeaddrinfo`, `exit`,
`select`, `setsockopt`, `recvfrom`, `sendto`, `ntohs`, `htons`,
`bind`, `socket`, `close`, `strerror`, `gai_strerror`,
printf-family functions, and libft-equivalent functions
(`read`, `write`, `malloc`, `free`).

## Forbidden Functions

`fcntl`, `poll`, `ppoll` — strictly prohibited.
