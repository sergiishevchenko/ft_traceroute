# ft_traceroute

A small `traceroute` implementation in C (42 school project style) that traces
the network path to a host using UDP probes and ICMP replies.

## What it does

- Sends UDP probes with increasing TTL values.
- Receives ICMP responses (`Time Exceeded` / `Destination Unreachable`).
- Prints hop-by-hop route with per-probe RTT in milliseconds.
- Supports reverse DNS lookup (or numeric-only mode with `-n`).
- Stops when the destination is reached (`ICMP port unreachable`).

## Features

- IPv4 target resolution via `getaddrinfo`.
- Configurable:
  - start TTL (`-f`)
  - max hops (`-m`)
  - probes per hop (`-q`)
  - base destination port (`-p`)
- Classic traceroute-style output:
  - `*` for timeout
  - annotations like `!N`, `!H`, `!P`, `!F`, `!S`, `!X`, `!<code>`

## Requirements

- C compiler (`gcc`)
- `make`
- Unix-like system with raw socket support
- Root privileges (required to open ICMP raw socket)

## Build

```bash
make
```

This produces the executable:

```bash
./ft_traceroute
```

Useful Make targets:

- `make` - build
- `make clean` - remove object files
- `make fclean` - remove object files and binary
- `make re` - full rebuild

## Usage

```bash
sudo ./ft_traceroute [options] host
```

### Options

- `--help` - show help and exit
- `-n` - do not resolve IP addresses to hostnames
- `-f first_ttl` - start from hop `first_ttl` (range: `1-255`, default: `1`)
- `-m max_ttl` - maximum hops (range: `1-255`, default: `30`)
- `-q nqueries` - probes per hop (range: `1-10`, default: `3`)
- `-p port` - base destination UDP port (range: `1-65535`, default: `33434`)

## Examples

```bash
# Basic trace
sudo ./ft_traceroute google.com

# Numeric output only (no reverse DNS)
sudo ./ft_traceroute -n 1.1.1.1

# Start at hop 5, max 20 hops, 5 probes per hop
sudo ./ft_traceroute -f 5 -m 20 -q 5 example.com

# Use custom base port
sudo ./ft_traceroute -p 40000 example.com
```

## How it works (brief)

1. Resolve target hostname to IPv4 address.
2. Create:
   - UDP send socket
   - ICMP raw receive socket
3. For each TTL from `first_ttl` to `max_ttl`:
   - set `IP_TTL` on the UDP socket
   - send `nqueries` probes to incremented destination ports
   - wait for ICMP replies using `select()` with timeout
   - match replies to probes via embedded UDP destination port
4. Print hop results and stop when destination is reached.

## Current scope / limitations

- IPv4 only.
- UDP-based traceroute only (no ICMP/UDP mode switch).
- No advanced traceroute flags beyond the options listed above.
- Requires elevated privileges.

## Project layout

- `includes/` - headers
- `srcs/` - source files
- `Makefile` - build rules
