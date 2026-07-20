# Data Model — Constants, Structures, and System Types

**Header:** `includes/ft_traceroute.h`  
**Related:** [DOCUMENTATION.md](DOCUMENTATION.md), [cli.md](cli.md), [probing.md](probing.md)

This document explains every `#define` and every field of the project
structures, plus the two standard libc types they embed:
`struct timeval` and `struct sockaddr_in`.

---

## Constants

### Program defaults and limits

| Macro | Value | Meaning |
|-------|-------|---------|
| `PROGRAM_NAME` | `"ft_traceroute"` | Name used in error messages, help text, and `stderr` prefixes |
| `DEFAULT_PORT` | `33434` | Base UDP destination port (classic traceroute default). Each probe uses `base + seq` |
| `DEFAULT_MAX_HOPS` | `30` | Default maximum TTL / hop count when `-m` is not given |
| `DEFAULT_NPROBES` | `3` | Default number of UDP probes per TTL when `-q` is not given |
| `DEFAULT_PACKET_SIZE` | `60` | Packet size printed in the header only (`20` IP + `8` UDP + `32` payload) |
| `DEFAULT_WAIT_TIME` | `5.0` | Seconds to wait for an ICMP reply to one probe |
| `PAYLOAD_SIZE` | `32` | Size of the UDP payload buffer filled in `send_probe()` |
| `RECV_BUFF_SIZE` | `512` | Byte size of the buffer passed to `recvfrom()` for ICMP |
| `MAX_PROBES` | `10` | Hard upper bound for `-q` and for the stack array `t_probe probes[MAX_PROBES]` |

Packet size identity:

```
60 = 20 (IPv4 header, no options) + 8 (UDP header) + 32 (payload)
```

### ICMP types (`type` field)

| Macro | Value | Meaning |
|-------|-------|---------|
| `FT_ICMP_UNREACH` | `3` | Destination Unreachable — host/network unreachable, or destination reached |
| `FT_ICMP_TIMXCEED` | `11` | Time Exceeded — intermediate router discarded the packet (TTL expired) |

### ICMP Destination Unreachable codes (`code` when type = 3)

| Macro | Value | Meaning | Output |
|-------|-------|---------|--------|
| `FT_UNREACH_NET` | `0` | Network unreachable | ` !N` |
| `FT_UNREACH_HOST` | `1` | Host unreachable | ` !H` |
| `FT_UNREACH_PROTO` | `2` | Protocol unreachable | ` !P` |
| `FT_UNREACH_PORT` | `3` | Port unreachable — **destination reached**; traceroute stops | *(none)* |
| `FT_UNREACH_FRAG` | `4` | Fragmentation needed | ` !F` |
| `FT_UNREACH_SR` | `5` | Source route failed | ` !S` |
| `FT_UNREACH_ADMIN9` | `9` | Administratively prohibited | ` !X` |
| `FT_UNREACH_ADMIN10` | `10` | Administratively prohibited | ` !X` |
| `FT_UNREACH_ADMIN13` | `13` | Administratively prohibited | ` !X` |

**Logic in code:**

- `FT_ICMP_TIMXCEED` → normal intermediate hop.
- `FT_ICMP_UNREACH` + `FT_UNREACH_PORT` → stop after this hop.
- Other `FT_UNREACH_*` codes → annotations only (`display.c`).

---

## `t_probe` — one probe attempt

```c
typedef struct s_probe
{
	struct timeval	send_time;
	struct timeval	recv_time;
	double			rtt;
	char			addr_str[INET_ADDRSTRLEN];
	int				received;
	int				icmp_type;
	int				icmp_code;
	int				port;
}	t_probe;
```

`t_probe` describes **one UDP probe** at the current TTL. On each hop,
`main()` allocates `t_probe probes[MAX_PROBES]`, zeroes the used slice,
then for each probe calls `send_probe()` → `recv_probe()` → later
`print_hop()`.

| Field | Type | Meaning |
|-------|------|---------|
| `send_time` | `struct timeval` | Timestamp taken with `gettimeofday()` just before `sendto()` |
| `recv_time` | `struct timeval` | Timestamp taken after a matching ICMP reply is received |
| `rtt` | `double` | Round-trip time in milliseconds (`recv_time − send_time`) |
| `addr_str` | `char[INET_ADDRSTRLEN]` | IP address of the answering node as a string (e.g. `"192.168.1.1"`) |
| `received` | `int` | `1` if a matching reply arrived; `0` on timeout / non-match → printed as `*` |
| `icmp_type` | `int` | ICMP type from the reply (`11` Time Exceeded or `3` Unreachable) |
| `icmp_code` | `int` | ICMP code; with type `3` drives annotations (`!N`, `!H`, …) or stop (`PORT`) |
| `port` | `int` | UDP destination port of this probe (`33434 + seq`); used to match the ICMP quote |

### Field lifecycle

```
send_probe()                 recv_probe()                      print_hop()
────────────                 ────────────                      ───────────
port      ← dest UDP port    parse_icmp() checks that port     received → "*" or data
send_time ← before sendto()  recv_time ← after recvfrom()      rtt → "12.345 ms"
                             icmp_type / icmp_code ← ICMP      addr_str → host/IP
                             received = 1 or 0
```

`port` is the matching key: the ICMP error embeds a copy of the original
UDP header; `parse_icmp()` accepts the reply only if the quoted
destination port equals `probe->port`.

`received` + `icmp_type` + `icmp_code` feed `check_reached()`: if any
probe got Unreachable / Port Unreachable, the destination was reached.

---

## `t_traceroute` — session state

```c
typedef struct s_traceroute
{
	char				*host;
	char				resolved_ip[INET_ADDRSTRLEN];
	struct sockaddr_in	dest_addr;
	int					send_sock;
	int					recv_sock;
	int					port;
	int					max_hops;
	int					nprobes;
	int					packet_size;
	double				wait_time;
	int					ttl;
	int					seq;
	bool				reached;
	bool				no_dns;
	int					first_ttl;
}	t_traceroute;
```

`t_traceroute` is the **global state of one traceroute run**. A single
instance lives on the stack in `main()` and is passed to every module
(`parse_args`, `resolve_host`, `send_probe`, `recv_probe`, `print_hop`, …).

Where `t_probe` stores the result of one send/recv, `t_traceroute`
stores the target, sockets, CLI options, and loop counters.

| Field | Type | Meaning |
|-------|------|---------|
| `host` | `char *` | Target string from `argv` (e.g. `"google.com"`); not copied — points into argv |
| `resolved_ip` | `char[INET_ADDRSTRLEN]` | Printable IPv4 after DNS (`resolve_host`), used in the header |
| `dest_addr` | `struct sockaddr_in` | Binary destination address; `sin_addr` is copied into each `sendto()` |
| `send_sock` | `int` | UDP socket FD for sending probes (`-1` until opened) |
| `recv_sock` | `int` | Raw ICMP socket FD for receiving replies (requires root) |
| `port` | `int` | Base UDP port (`-p`, default 33434); combined with `seq` per probe |
| `max_hops` | `int` | Maximum TTL (`-m`, default 30) — upper bound of the hop loop |
| `nprobes` | `int` | Probes per hop (`-q`, default 3) |
| `packet_size` | `int` | Size shown in the header only (60); actual payload is `PAYLOAD_SIZE` (32) |
| `wait_time` | `double` | Per-probe reply timeout in seconds (default 5.0) |
| `ttl` | `int` | Current TTL in the hop loop; applied with `set_ttl()` before each hop |
| `seq` | `int` | Global probe counter; incremented in `send_probe()` to build unique ports |
| `reached` | `bool` | Initialized to `false` but **unused** in the current main loop; stop uses `check_reached()`’s return value |
| `no_dns` | `bool` | `-n` flag: skip reverse DNS and print IPs only |
| `first_ttl` | `int` | Starting TTL (`-f`, default 1) |

### Field lifecycle

```
parse_args()           resolve_host()          create_sockets()
────────────           ──────────────          ───────────────
host, no_dns,          resolved_ip,            send_sock, recv_sock
first_ttl,             dest_addr
max_hops, nprobes,
port

main loop (each hop):
─────────────────────
ttl ← first_ttl .. max_hops
set_ttl(tr, ttl)       → IP_TTL on send_sock
send_probe()           → seq++, probe port = base + seq
recv_probe()           → wait_time
print_hop()            → ttl, nprobes, no_dns
check_reached()        → break (does not write tr->reached)
```

---

## Embedded system structures

Two fields above are not project-defined types: they come from libc /
POSIX headers.

### `struct timeval` (`<sys/time.h>`)

Used by `t_probe.send_time` and `t_probe.recv_time`.

```c
struct timeval {
	time_t      tv_sec;   /* seconds since Unix epoch */
	suseconds_t tv_usec;  /* microseconds (0–999999) */
};
```

| Field | Type | Meaning |
|-------|------|---------|
| `tv_sec` | `time_t` | Whole seconds since 1970-01-01 UTC |
| `tv_usec` | `suseconds_t` | Fractional part in microseconds |

**How the project uses it:**

1. `gettimeofday(&probe->send_time, NULL)` in `send_probe()`.
2. `gettimeofday(&probe->recv_time, NULL)` in `recv_probe()`.
3. `calc_rtt()` (`srcs/time.c`) converts the difference to milliseconds:

```c
rtt_ms = (recv.tv_sec - send.tv_sec) * 1000.0
       + (recv.tv_usec - send.tv_usec) / 1000.0;
```

`struct timeval` also appears locally in `recv_probe()` as the `select()`
timeout (`tv`) and as wall-clock markers (`start` / `now`) to enforce
`wait_time`.

### `struct sockaddr_in` (`<netinet/in.h>`)

Used by `t_traceroute.dest_addr` (and locally as send/receive address
arguments).

```c
struct sockaddr_in {
	sa_family_t     sin_family; /* address family: AF_INET */
	in_port_t       sin_port;   /* port in network byte order */
	struct in_addr  sin_addr;   /* IPv4 address */
	/* padding / zeroed bytes follow on many platforms */
};
```

| Field | Type | Meaning |
|-------|------|---------|
| `sin_family` | `sa_family_t` | Must be `AF_INET` for IPv4 |
| `sin_port` | `in_port_t` | Destination (or source) port in **network** byte order (`htons` / `ntohs`) |
| `sin_addr` | `struct in_addr` | 32-bit IPv4 address (`sin_addr.s_addr`) |

**How the project uses it:**

1. `resolve_host()` copies the resolved address into `tr->dest_addr`
   via `getaddrinfo()` / `memcpy`.
2. `send_probe()` builds a temporary `struct sockaddr_in dest`:
   - `sin_family = AF_INET`
   - `sin_addr = tr->dest_addr.sin_addr`
   - `sin_port = htons(probe->port)`
3. `recvfrom()` fills a local `from` `sockaddr_in`; its `sin_addr` becomes
   `probe->addr_str` via `inet_ntoa()`.
4. `print_addr()` rebuilds a `sockaddr_in` from the IP string for
   reverse DNS (`getnameinfo`).

`struct in_addr` itself is simply:

```c
struct in_addr {
	uint32_t s_addr; /* IPv4 address in network byte order */
};
```

---

## How the pieces fit together

```
t_traceroute                          t_probe[nprobes]
────────────                          ────────────────
target + options + sockets            one result per UDP send
dest_addr ───────────────► sendto()   port matches ICMP quote
wait_time  ───────────────► select()  send_time / recv_time → rtt
ttl / nprobes ────────────► hop loop  received / icmp_* → print & stop
```

`t_traceroute` answers *where* and *with which parameters* to probe.
`t_probe` answers *what came back* for each individual packet at the
current TTL.
