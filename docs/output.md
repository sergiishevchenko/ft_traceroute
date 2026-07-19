# Output — Display Formatting

**Source:** `srcs/display.c`  
**Related:** `tr->no_dns` from CLI (`-n`), probe results from `recv.c`,
constants `FT_ICMP_*` / `FT_UNREACH_*` in `includes/ft_traceroute.h`

## Purpose

The display layer turns filled `t_probe` arrays into human-readable
traceroute output that is familiar to users of the classic UNIX utility.
It does **not** send or receive packets; it only formats:

1. A single session header.
2. One line per TTL hop after all probes for that hop have finished.

Matching system `traceroute` closely matters for evaluation: hop
numbering, `*` timeouts, hostname `(ip)` form, and annotations such as
`!H` / `!N`.

---

## When display runs

```
create_sockets()
print_header(&tr)          ← once

for each ttl:
    ... send/recv all probes ...
    print_hop(&tr, probes) ← once per hop
```

Output is progressive: each hop line appears as soon as that hop’s
probes complete, not in one dump at the end. That lets the user watch
the path grow in real time.

Normal progress goes to **stdout**. Fatal errors from other modules go
to **stderr**; `display.c` itself does not call `fatal_error()`.

---

## Session header — `print_header()`

```c
printf("traceroute to %s (%s), %d hops max, %d byte packets\n",
    tr->host, tr->resolved_ip, tr->max_hops, tr->packet_size);
```

| Placeholder     | Source | Example |
|-----------------|--------|---------|
| `%s` host       | Original argv string | `google.com` |
| `%s` IP         | `resolved_ip` after DNS | `142.250.185.78` |
| `%d` hops max   | `max_hops` (`-m`) | `30` |
| `%d` byte packets | `packet_size` (always 60 in this project) | `60` |

### Examples

```
traceroute to google.com (142.250.185.78), 30 hops max, 60 byte packets
traceroute to 1.1.1.1 (1.1.1.1), 15 hops max, 60 byte packets
```

When the user already passed a literal IP, hostname and resolved IP
are often identical. The header still always prints both fields.

`first_ttl` is **not** shown in the header. If `-f 5` is used, the first
hop line simply starts at ` 5` rather than ` 1`.

---

## Hop line — `print_hop()`

### High-level layout

```
<hop>  <addr?>  <rtt> ms [annotation]  <addr?>  <rtt> ms ...
```

Concrete format choices in code:

1. Print hop number with `"%2d"` (right-aligned width 2, no trailing
   spaces yet).
2. Iterate probes `i = 0 … nprobes-1` in order (never reordered by RTT).
3. End with `"\n"`.

### Per-probe logic

```
if probe not received:
    print "  *"
    continue

if probe.addr_str != last_addr:
    print_addr(probe.addr_str, tr->no_dns)
    last_addr = probe.addr_str

print "  %.3f ms"
print_annotation(icmp_type, icmp_code)
```

`last_addr` starts as an empty string each hop, so the **first
successful** probe on a hop always prints its address.

### Why suppress repeated addresses?

Classic traceroute prints the router identity once, then only RTTs for
subsequent probes that came from the **same** IP. If ECMP / anycast /
load balancing makes probes bounce off different routers at the same
TTL, each new address is printed before its RTT so the line remains
unambiguous.

| Probes reply from | Typical printed shape |
|-------------------|------------------------|
| Same IP three times | ` 1  host (ip)  1.1 ms  1.0 ms  1.2 ms` |
| Timeout all | ` 2  *  *  *` |
| Mix timeout + reply | ` 3  router (ip)  5.0 ms  *  5.1 ms` |
| Two different IPs | ` 4  a (ip1)  3.0 ms  b (ip2)  4.0 ms  4.1 ms` |

In the last row, the third probe reused `ip2`, so the address is not
printed again before `4.1 ms`.

---

## Address formatting — `print_addr()`

### With reverse DNS (default)

```c
inet_pton(AF_INET, ip_str, &sa.sin_addr);
getnameinfo(..., hostname, ..., 0);
printf("  %s (%s)", hostname, ip_str);
```

Displayed form: two spaces, then `hostname (dotted-ip)`.

If `getnameinfo` fails (no PTR record, DNS error, timeout inside the
resolver), the code falls back to:

```
  <ip> (<ip>)
```

so the line still has a stable two-field shape.

Reverse lookups can be slow on some networks and will delay printing
that hop line — probes for the hop are already finished, but DNS adds
latency before `printf` returns.

### Without reverse DNS (`-n` / `tr->no_dns`)

```c
printf("  %s", ip_str);
```

Only the numeric address is shown (still preceded by two spaces). This
matches `traceroute -n` behavior and is preferred for scripting or
offline comparisons.

### Comparison

| Mode | Example fragment |
|------|------------------|
| Default | `  gateway.home (192.168.1.1)` |
| `-n` | `  192.168.1.1` |
| PTR miss | `  203.0.113.5 (203.0.113.5)` |

---

## RTT formatting

Successful probes print:

```c
printf("  %.3f ms", probes[i].rtt);
```

- Always three fractional digits.
- Unit label is lowercase `ms` with a leading space before the number
  pair (`  12.345 ms`).
- Value comes from `calc_rtt()` in the probing layer; display does not
  recompute times.

Timeouts never print `ms`; they print `  *` only.

---

## ICMP annotations — `print_annotation()`

Called after every successful probe’s RTT. Annotations describe
**Destination Unreachable** variants other than the normal “we reached
the host” case.

### Gate

```c
if (icmp_type != FT_ICMP_UNREACH)  /* not type 3 */
    return;
```

Therefore:

- **Time Exceeded (type 11)** → no annotation (normal intermediate hop).
- **Unreachable (type 3)** → may annotate depending on code.

### Code → string map

| ICMP code | Macro | Printed | Meaning |
|-----------|-------|---------|---------|
| 0 | `FT_UNREACH_NET` | ` !N` | Network unreachable |
| 1 | `FT_UNREACH_HOST` | ` !H` | Host unreachable |
| 2 | `FT_UNREACH_PROTO` | ` !P` | Protocol unreachable |
| 3 | `FT_UNREACH_PORT` | *(nothing)* | Port unreachable — destination reached |
| 4 | `FT_UNREACH_FRAG` | ` !F` | Fragmentation needed |
| 5 | `FT_UNREACH_SR` | ` !S` | Source route failed |
| 9, 10, 13 | `FT_UNREACH_ADMIN*` | ` !X` | Administratively prohibited |
| other ≠ 3 | — | ` !<code>` | Unknown / uncommon code |

Leading space before `!` keeps tokens separated from the `ms` field.

### Why Port Unreachable is silent

Port Unreachable is the **success** signal for UDP traceroute. Printing
`!3` or similar would look like an error. The program instead stops the
main loop after this hop (`check_reached` in `main.c`).

### Annotations vs stopping the scan

Display annotations are informational. Except for Port Unreachable
(handled in `main`), the traceroute **continues** to higher TTLs even
if a hop printed `!H` or `!N`. Operators still see that a router
refused the packet for a specific reason.

### Examples

```
 6  198.51.100.1  20.100 ms  19.800 ms  20.050 ms
 7  203.0.113.1  40.000 ms !H  *  41.200 ms !H
 8  target.example (198.51.100.10)  42.000 ms  41.500 ms  41.700 ms
```

Hop 6: normal Time Exceeded replies.  
Hop 7: host unreachable annotations; still not Port Unreachable.  
Hop 8: destination; no `!` on Port Unreachable; loop stops after the line.

---

## Full sample session

Command:

```bash
sudo ./ft_traceroute -n -q 3 example.com
```

Illustrative output:

```
traceroute to example.com (93.184.216.34), 30 hops max, 60 byte packets
 1  192.168.1.1  1.204 ms  0.980 ms  0.912 ms
 2  10.0.0.1  8.100 ms  7.950 ms  8.020 ms
 3  *  *  *
 4  198.51.100.1  15.200 ms  203.0.113.9  16.100 ms  15.800 ms
 5  93.184.216.34  20.001 ms  19.870 ms  19.920 ms
```

Reading that sample:

- `-n` → no hostnames.
- Hop 3 → all three probes timed out.
- Hop 4 → ECMP: second probe came from a different router IP, so the
  address was printed again.
- Hop 5 → target IP; Port Unreachable internally; program exits.

With DNS enabled, hop 1 might look like:

```
 1  router.lan (192.168.1.1)  1.204 ms  0.980 ms  0.912 ms
```

---

## Spacing and alignment details

| Element | Spacing rule in code |
|---------|----------------------|
| Hop number | `"%2d"` — width 2, no extra spaces after |
| Address | printed with a leading `"  "` inside `print_addr` / `-n` path |
| Timeout star | `"  *"` |
| RTT | `"  %.3f ms"` |
| Annotation | `" !X"` style (space then bang) |

There is **no** trailing space trimming; small differences versus another
traceroute binary in exact column alignment can still appear, but the
token sequence matches the usual mental model.

Hop numbers `1`–`9` appear with a leading space (` 1`, ` 9`); `10`–`30`
fill the two-character field (`10`, `30`).

---

## Buffering / flushing

Each hop ends with `\n`. On a terminal, stdout is typically
**line-buffered**, so each hop becomes visible immediately. There is no
explicit `fflush(stdout)`.

If stdout is redirected to a file or pipe, it may be fully buffered;
hop lines can appear in larger chunks. That does not change content,
only when bytes hit the disk/pipe.

---

## What this module does *not* do

- Does not decide whether the destination was reached (that is
  `check_reached` in `main.c`).
- Does not perform reverse DNS when `-n` is set.
- Does not print errors for send/recv failures (send path may print to
  stderr itself; timeouts are only `*`).
- Does not print a summary footer after the last hop.
- Does not localize messages; output is English / symbolic like
  traditional traceroute.

---

## API summary

| Function | Visibility | Role |
|----------|------------|------|
| `print_header` | public | Session banner |
| `print_hop` | public | One TTL line from `t_probe[]` |
| `print_addr` | static | IP with optional reverse DNS |
| `print_annotation` | static | `!N` / `!H` / … for unreachable codes |

---

## Interaction cheat sheet

| Input field | Affects |
|-------------|---------|
| `tr->host`, `resolved_ip`, `max_hops`, `packet_size` | Header |
| `tr->ttl` | Leading hop number |
| `tr->nprobes` | How many probe columns |
| `tr->no_dns` | Hostname resolution on/off |
| `probes[i].received` | RTT vs `*` |
| `probes[i].addr_str` | Printed hop identity |
| `probes[i].rtt` | Millisecond field |
| `probes[i].icmp_type/code` | Optional annotation |
