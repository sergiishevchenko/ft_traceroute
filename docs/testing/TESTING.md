# ft_traceroute — Testing Guide

Manual test plan for validating the project against the subject requirements and expected runtime behavior.

## 1. Test environment

| Requirement | Details |
|-------------|---------|
| OS | Linux (kernel >= 4.0). Evaluation is done on a 64-bit Debian-based VM |
| Privileges | `sudo` or root is required for network tests |
| Tools | `gcc`, `make`, `traceroute` (system binary for comparison) |
| Network | Internet access or a reachable local host |

Recommended setup:

```bash
make re
which traceroute
```

## 2. Build tests

| # | Command | Expected result |
|---|---------|-----------------|
| B1 | `make` | Builds `ft_traceroute` without warnings or errors |
| B2 | `make clean` | Removes `objs/` |
| B3 | `make fclean` | Removes `objs/` and `ft_traceroute` |
| B4 | `make re` | Full rebuild succeeds |
| B5 | `ls -l ft_traceroute` | Binary exists and is executable |

## 3. CLI tests (no root required)

These tests validate argument parsing and error handling before socket creation.

In CLI error tests below, `example.com` is only a dummy destination. The program exits on option validation before DNS/network, so any string works as the last argument.

### 3.1 Help and basic usage

| # | Command | Expected stdout | Expected stderr | Exit code |
|---|---------|-----------------|-----------------|-----------|
| C1 | `./ft_traceroute --help` | Usage block with `--help`, `-n`, `-f`, `-m`, `-q`, `-p` | empty | `0` |
| C2 | `./ft_traceroute` | empty | `missing host operand` | `1` |
| C3 | `./ft_traceroute example.com google.com` | empty | `too many arguments` | `1` |
| C4 | `./ft_traceroute -z example.com` | empty | `bad option` | `1` |
| C5 | `./ft_traceroute --unknown example.com` | empty | `bad option` | `1` |

### 3.2 Option validation

| # | Command | Expected result |
|---|---------|-----------------|
| C6 | `./ft_traceroute -f` | `option requires an argument -- 'first_ttl'` |
| C7 | `./ft_traceroute -m` | `option requires an argument -- 'max_ttl'` |
| C8 | `./ft_traceroute -q` | `option requires an argument -- 'nqueries'` |
| C9 | `./ft_traceroute -p` | `option requires an argument -- 'port'` |
| C10 | `./ft_traceroute -f 0 example.com` | `invalid value for 'first_ttl' (must be 1-255)` |
| C11 | `./ft_traceroute -f 256 example.com` | `invalid value for 'first_ttl' (must be 1-255)` |
| C12 | `./ft_traceroute -m 0 example.com` | `invalid value for 'max_ttl' (must be 1-255)` |
| C13 | `./ft_traceroute -q 0 example.com` | `invalid value for 'nqueries' (must be 1-10)` |
| C14 | `./ft_traceroute -q 11 example.com` | `invalid value for 'nqueries' (must be 1-10)` |
| C15 | `./ft_traceroute -p 0 example.com` | `invalid value for 'port' (must be 1-65535)` |
| C16 | `./ft_traceroute -f abc example.com` | `invalid value for 'first_ttl'` |
| C17 | `./ft_traceroute example.com` (without sudo) | `root privileges required` |

## 4. Network tests (root required)

Run all commands below with `sudo`. Results depend on your network path, firewall, and DNS.

| # | Command | Real? | What to check |
|---|---------|-------|---------------|
| N1 | `sudo ./ft_traceroute 1.1.1.1` | yes (needs net) | Header + hop lines, exit `0` |
| N2 | `sudo ./ft_traceroute google.com` | yes (needs net/DNS) | Header: `google.com (<resolved_ip>)` |
| N3 | `sudo ./ft_traceroute -n 8.8.8.8` | yes (needs net) | Hop lines show IPs only |
| N4 | `sudo ./ft_traceroute -m 5 1.1.1.1` | yes (needs net) | At most 5 hop lines; may stop earlier if destination is reached |
| N5 | `sudo ./ft_traceroute -f 3 -m 8 1.1.1.1` | yes (needs net) | First hop number is `3` |
| N6 | `sudo ./ft_traceroute -q 1 1.1.1.1` | yes (needs net) | One probe result per hop line |
| N7 | `sudo ./ft_traceroute -p 40000 1.1.1.1` | yes (needs net) | Runs normally with custom base port |
| N8 | `sudo ./ft_traceroute invalid.host.42test` | yes | DNS error on stderr, exit `1` (must use sudo — root is checked before resolve) |
| N9 | `sudo ./ft_traceroute 999.999.999.999` | yes | Resolution error, exit `1`, no segfault |
| N10 | `sudo ./ft_traceroute -n 1.1.1.1` | yes (needs net) | Stops after destination replies (Port Unreachable), usually before hop 30 |
| N11 | `sudo ./ft_traceroute -n -m 5 10.255.255.1` | depends | Often mostly `*`, then stops at max TTL; exact hops depend on your LAN |

## 5. Output format tests

Compare output with the system `traceroute` on **Linux** (evaluation VM). On macOS the system binary differs.

### 5.1 Header

Expected format:

```text
traceroute to <host> (<ip>), <max_hops> hops max, 60 byte packets
```

Example:

```bash
sudo ./ft_traceroute -m 30 1.1.1.1
```

Check:
- host name preserved from argv
- resolved IPv4 shown in parentheses
- max hops matches `-m` (default `30`)
- packet size is `60`

### 5.2 Hop line layout

Expected patterns:

```text
 1  <addr>  <rtt> ms  <rtt> ms  <rtt> ms
 2  *  *  *
 3  <addr>  <rtt> ms  <addr2>  <rtt> ms  <rtt> ms
```

Check:
- hop number is right-aligned in 2 characters
- two spaces before first probe result
- timeout displayed as `  *`
- RTT printed as `  %.3f ms`
- with reverse DNS: `  hostname (ip)`
- with `-n`: `  ip` only

### 5.3 Comparison with system traceroute

Run both commands and compare structure (not exact RTT values):

```bash
sudo ./ft_traceroute -n -m 15 -q 3 8.8.8.8 > /tmp/ft.out
sudo traceroute -n -m 15 -q 3 8.8.8.8 > /tmp/sys.out
diff -u /tmp/sys.out /tmp/ft.out
```

Acceptable differences:
- RTT values may differ by up to ±30 ms per hop
- reverse DNS hostnames may differ in timing or availability

Unacceptable differences:
- different hop count for the same target and options
- missing `*` on timeouts
- different indentation/layout
- crash or hang

## 6. Mandatory vs bonus checklist

| Requirement | Part | How to test | Pass criteria |
|-------------|------|-------------|---------------|
| Binary `ft_traceroute` | mandatory | `make` | Binary builds |
| `--help` | mandatory | `./ft_traceroute --help` | Usage printed, exit `0` |
| IPv4 host argument | mandatory | `sudo ./ft_traceroute 1.1.1.1` | Traceroute runs |
| UDP probes + ICMP replies | mandatory | N1, N10 | Intermediate hops and destination detected |
| No reverse DNS in mandatory mode | mandatory | `sudo ./ft_traceroute -n <host>` | IPs only on hop lines |
| Output like real traceroute | mandatory | Section 5 | Layout matches system tool |
| No forbidden functions | mandatory | Code review | No `fcntl`, `poll`, `ppoll` |
| No system `traceroute` call | mandatory | Code review | No `exec*` / `system` usage |
| `-n` | bonus | N3 | Skips reverse DNS |
| `-f` | bonus | N5 | Starts at given TTL |
| `-m` | bonus | N4 | Limits max hops |
| `-q` | bonus | N6 | Changes probes per hop |
| `-p` | bonus | N7 | Custom base UDP port works |
| Reverse DNS on hops | bonus | N2 without `-n` | `hostname (ip)` when resolvable |
| ICMP annotations | bonus | Reach host returning `!H`, `!N`, etc. | Annotation printed after RTT |

## 7. Error handling and stability

| # | Test | Expected result |
|---|------|-----------------|
| S1 | `sudo ./ft_traceroute -n -m 5 1.1.1.1` (run 10 times) | No segfault, exit `0` each time |
| S2 | Start a long trace, press `Ctrl+C` | Process terminates |
| S3 | `sudo ./ft_traceroute -f 1 -m 1 -q 10 1.1.1.1` | Max probes handled, no crash |
| S4 | `sudo ./ft_traceroute -p 65500 -m 5 -q 3 1.1.1.1` | Port wrap-around does not crash |
| S5 | `sudo valgrind --leak-check=full ./ft_traceroute -m 3 -n 127.0.0.1` | No leaks from project code (optional) |

## 8. Automated test script

```bash
./test.sh              # build + CLI tests
sudo ./test.sh --network   # also run network tests
```

Exit code `0` if all executed tests pass.

## 9. Evaluation rehearsal

Before defense, verify in this order:

1. `make re` on Linux VM
2. `./ft_traceroute --help`
3. Error cases without arguments
4. `sudo ./ft_traceroute -n <reachable_ipv4>`
5. `sudo ./ft_traceroute <hostname>`
6. Compare output with `sudo traceroute -n <same_host>`
7. Test each bonus flag: `-f`, `-m`, `-q`, `-p`, `-n`
8. Explain code flow: args → resolve → sockets → TTL loop → send/recv → display

## 10. Known limitations (not bugs)

- IPv4 only
- UDP probes only (no ICMP/TCP mode)
- Requires root for raw ICMP socket
- Reverse DNS depends on network and PTR records
- RTT values are environment-dependent
