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

### 3.1 Help and basic usage

| # | Command | Expected stdout | Expected stderr | Exit code |
|---|---------|-----------------|-----------------|-----------|
| C1 | `./ft_traceroute --help` | Usage block with `--help`, `-n`, `-f`, `-m`, `-q`, `-p` | empty | `0` |
| C2 | `./ft_traceroute` | empty | `missing host operand` | `1` |
| C3 | `./ft_traceroute host1 host2` | empty | `too many arguments` | `1` |
| C4 | `./ft_traceroute -z host` | empty | `bad option` | `1` |
| C5 | `./ft_traceroute --unknown host` | empty | `bad option` | `1` |

### 3.2 Option validation

| # | Command | Expected result |
|---|---------|-----------------|
| C6 | `./ft_traceroute -f host` | `option requires an argument -- 'first_ttl'` |
| C7 | `./ft_traceroute -m host` | `option requires an argument -- 'max_ttl'` |
| C8 | `./ft_traceroute -q host` | `option requires an argument -- 'nqueries'` |
| C9 | `./ft_traceroute -p host` | `option requires an argument -- 'port'` |
| C10 | `./ft_traceroute -f 0 host` | `invalid value for 'first_ttl' (must be 1-255)` |
| C11 | `./ft_traceroute -f 256 host` | `invalid value for 'first_ttl' (must be 1-255)` |
| C12 | `./ft_traceroute -m 0 host` | `invalid value for 'max_ttl' (must be 1-255)` |
| C13 | `./ft_traceroute -q 0 host` | `invalid value for 'nqueries' (must be 1-10)` |
| C14 | `./ft_traceroute -q 11 host` | `invalid value for 'nqueries' (must be 1-10)` |
| C15 | `./ft_traceroute -p 0 host` | `invalid value for 'port' (must be 1-65535)` |
| C16 | `./ft_traceroute -f abc host` | `invalid value for 'first_ttl'` |
| C17 | `./ft_traceroute host` (without sudo) | `root privileges required` |

## 4. Network tests (root required)

Run all commands below with `sudo`.

### 4.1 Basic traceroute

| # | Command | What to check |
|---|---------|---------------|
| N1 | `sudo ./ft_traceroute 1.1.1.1` | Header printed, hop lines shown, program exits normally |
| N2 | `sudo ./ft_traceroute google.com` | Hostname resolved, header shows `host (ip)` |
| N3 | `sudo ./ft_traceroute -n 8.8.8.8` | Hop lines show IP only, no hostname lookup |
| N4 | `sudo ./ft_traceroute -m 5 1.1.1.1` | Stops after 5 hops if destination not reached |
| N5 | `sudo ./ft_traceroute -f 3 -m 8 1.1.1.1` | First printed hop number is `3` |
| N6 | `sudo ./ft_traceroute -q 1 1.1.1.1` | One probe per hop line |
| N7 | `sudo ./ft_traceroute -p 40000 1.1.1.1` | Traceroute still works with custom base port |

### 4.2 Invalid host

| # | Command | Expected result |
|---|---------|-----------------|
| N8 | `sudo ./ft_traceroute invalid.host.42test` | DNS resolution error on stderr, exit `1` |
| N9 | `sudo ./ft_traceroute 999.999.999.999` | Resolution error, no segfault |

### 4.3 Stop condition

| # | Scenario | Expected result |
|---|----------|-----------------|
| N10 | Reachable public host | Program stops when destination replies with ICMP Port Unreachable |
| N11 | Unreachable / filtered host | Timeouts shown as `*`, program stops at `max_ttl` |

## 5. Output format tests

Compare output with the system `traceroute` on Linux.

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
| S1 | Run 10 times on same host | No segfault, no unexpected exit |
| S2 | Interrupt with `Ctrl+C` during trace | Process terminates (OS handles signal) |
| S3 | `./ft_traceroute -f 1 -m 1 -q 10 1.1.1.1` | Handles max probes without crash |
| S4 | `./ft_traceroute -p 65500 -m 30 -q 10 1.1.1.1` | Port wrap-around does not crash |
| S5 | Valgrind (optional) | `valgrind --leak-check=full sudo ./ft_traceroute -m 3 -n 127.0.0.1` — no leaks from project code |

## 8. Quick smoke test script

Save as `test_smoke.sh` in the project root:

```bash
#!/bin/bash
set -e

BIN="./ft_traceroute"
PASS=0
FAIL=0

check() {
    local desc="$1"
    local cmd="$2"
    local expect="$3"
    if eval "$cmd" > /tmp/ft_test_out 2> /tmp/ft_test_err; then
        rc=0
    else
        rc=$?
    fi
    if eval "$expect"; then
        echo "[PASS] $desc"
        PASS=$((PASS + 1))
    else
        echo "[FAIL] $desc (exit=$rc)"
        cat /tmp/ft_test_err
        FAIL=$((FAIL + 1))
    fi
}

make re > /dev/null

check "help exits 0" "$BIN --help" "[[ \$rc -eq 0 ]]"
check "missing host" "$BIN" "[[ \$rc -eq 1 ]] && grep -q 'missing host operand' /tmp/ft_test_err"
check "bad option" "$BIN -z host" "[[ \$rc -eq 1 ]] && grep -q 'bad option' /tmp/ft_test_err"
check "invalid first_ttl" "$BIN -f 0 host" "[[ \$rc -eq 1 ]] && grep -q 'invalid value' /tmp/ft_test_err"
check "root required" "$BIN 1.1.1.1" "[[ \$rc -eq 1 ]] && grep -q 'root privileges required' /tmp/ft_test_err"

if [[ $EUID -eq 0 ]]; then
    check "basic trace" "$BIN -n -m 3 -q 1 1.1.1.1" "[[ \$rc -eq 0 ]] && grep -q 'traceroute to' /tmp/ft_test_out"
else
    echo "[SKIP] network tests (run as root to enable)"
fi

echo ""
echo "Results: $PASS passed, $FAIL failed"
[[ $FAIL -eq 0 ]]
```

Run:

```bash
chmod +x test_smoke.sh
./test_smoke.sh
sudo ./test_smoke.sh
```

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
