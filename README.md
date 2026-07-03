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

| Feature | Part |
|---------|------|
| `ft_traceroute` binary, C, Makefile | mandatory |
| `--help` | mandatory |
| IPv4 host argument | mandatory |
| UDP traceroute (TTL, ICMP replies) | mandatory |
| Hop output without reverse DNS | mandatory |
| Output like real `traceroute` | mandatory |
| `-n` | bonus |
| `-f first_ttl` | bonus |
| `-m max_ttl` | bonus |
| `-q nqueries` | bonus |
| `-p port` | bonus |
| Reverse DNS on hops | bonus |
| ICMP annotations (`!N`, `!H`, …) | bonus |

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
