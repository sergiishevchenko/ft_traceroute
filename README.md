# ft_traceroute

A reimplementation of the UNIX `traceroute` utility in C. Traces the network
path to a remote host using UDP probes and ICMP replies.

## Project structure

```
ft_traceroute/
├── Makefile
├── includes/
│   └── ft_traceroute.h      structs, constants, function prototypes
├── srcs/
│   ├── main.c               entry point, TTL loop, stop condition
│   ├── args.c               CLI parsing, validation, --help
│   ├── resolve.c            hostname/IPv4 resolution (getaddrinfo)
│   ├── socket.c             UDP + raw ICMP sockets, TTL setup
│   ├── send.c               UDP probe transmission
│   ├── recv.c               ICMP receive, parsing, probe matching
│   ├── time.c               RTT calculation
│   ├── display.c            header, hop output, DNS, annotations
│   └── utils.c              fatal error helper
└── docs/
    ├── en.subject.pdf       project subject
    └── DOCUMENTATION.md     technical documentation
```

## How it works

1. Parse CLI options and verify root privileges (`getuid`).
2. Resolve the target hostname or IPv4 address with `getaddrinfo` (`AF_INET`).
3. Open two sockets:
   - UDP socket for sending probes
   - raw ICMP socket for receiving replies
4. For each TTL from `first_ttl` to `max_ttl`:
   - set `IP_TTL` on the UDP socket with `setsockopt`
   - send `nqueries` UDP probes (32-byte payload, 60-byte total packet size)
   - use destination ports `base_port + seq` so each probe can be identified
   - wait for replies with `select()` and read them via `recvfrom()`
   - parse ICMP errors (`Time Exceeded` for intermediate hops, `Port Unreachable` at destination)
   - match replies to probes using the embedded UDP destination port
   - compute RTT from `gettimeofday()` timestamps and print the hop line
5. Stop when the destination replies with `ICMP Port Unreachable`, or when `max_ttl` is reached.
6. Close sockets and exit.

Timeouts are shown as `*`.

## Mandatory vs bonus

| Feature | Part | Description |
|---------|------|-------------|
| `ft_traceroute` binary, C, Makefile | mandatory | Compiled program with standard build targets (`all`, `clean`, `fclean`, `re`) |
| `--help` | mandatory | Only allowed option in mandatory part; prints usage and exits |
| IPv4 host argument | mandatory | Single target: IPv4 address or hostname, resolved via `getaddrinfo` |
| UDP traceroute (TTL, ICMP replies) | mandatory | Send UDP probes with increasing TTL; handle `Time Exceeded` and `Port Unreachable` |
| Hop output without reverse DNS | mandatory | Each hop line shows router IP only, no `getnameinfo` lookup |
| Output like real `traceroute` | mandatory | Header, hop layout, `*` on timeout; ±30 ms RTT difference tolerated |
| `-n` | bonus | Disable reverse DNS; print IP addresses only on hop lines |
| `-f first_ttl` | bonus | Set starting TTL/hop number (1–255, default 1) |
| `-m max_ttl` | bonus | Set maximum number of hops (1–255, default 30) |
| `-q nqueries` | bonus | Set number of probes sent per hop (1–10, default 3) |
| `-p port` | bonus | Set base destination UDP port; each probe uses `port + seq` (default 33434) |
| Reverse DNS on hops | bonus | Resolve each hop IP to hostname via `getnameinfo`, shown as `host (ip)` |
| ICMP annotations (`!N`, `!H`, …) | bonus | Append codes on ICMP unreachable replies (`!N` network, `!H` host, etc.) |

## Requirements

- C compiler (`gcc`)
- `make`
- Linux with raw socket support
- Root privileges (required to open ICMP raw socket)

## Build

```bash
make
```

Other targets: `make clean`, `make fclean`, `make re`.

## Usage

```bash
sudo ./ft_traceroute [options] host
```

### Options

| Flag | Description | Default |
|------|-------------|---------|
| `--help` | Show help and exit | |
| `-n` | Do not resolve IPs to hostnames | off |
| `-f first_ttl` | Start from this hop (1-255) | 1 |
| `-m max_ttl` | Maximum number of hops (1-255) | 30 |
| `-q nqueries` | Probes per hop (1-10) | 3 |
| `-p port` | Base destination UDP port (1-65535) | 33434 |

## Examples

```bash
sudo ./ft_traceroute google.com
sudo ./ft_traceroute -n 1.1.1.1
sudo ./ft_traceroute -f 5 -m 20 -q 5 example.com
sudo ./ft_traceroute -p 40000 example.com
```

## Limitations

- IPv4 only
- UDP-based probes only (no ICMP mode)
- Requires root privileges
