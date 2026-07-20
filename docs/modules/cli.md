# CLI — Argument Parsing

**Source:** `srcs/args.c`  
**Entry point:** `parse_args(t_traceroute *tr, int argc, char **argv)`  
**Related:** defaults in `init_traceroute()` (`srcs/main.c`), constants in `includes/ft_traceroute.h`

## Purpose

The CLI layer is the first thing `main()` calls after zeroing the program
state. Its job is to turn raw `argv` into a fully configured
`t_traceroute` session **before** any network I/O happens:

1. Accept or reject every option exactly as documented by `--help`.
2. Identify exactly one target host operand.
3. Validate numeric ranges so later modules never see illegal TTL, port,
   or probe counts.
4. Abort early if the process is not running as root, because raw ICMP
   sockets (created next) will fail otherwise.

No sockets are opened and no DNS lookups are performed here. A bad
option never reaches `resolve_host()` or `create_sockets()`.

---

## Defaults before parsing

`init_traceroute()` runs first and fills safe defaults. `parse_args()`
only **overrides** fields the user explicitly sets.

| Field         | Default constant         | Value   |
|---------------|--------------------------|---------|
| `port`        | `DEFAULT_PORT`           | 33434   |
| `max_hops`    | `DEFAULT_MAX_HOPS`       | 30      |
| `nprobes`     | `DEFAULT_NPROBES`        | 3       |
| `packet_size` | `DEFAULT_PACKET_SIZE`    | 60      |
| `wait_time`   | `DEFAULT_WAIT_TIME`      | 5.0 s   |
| `first_ttl`   | (literal in init)        | 1       |
| `no_dns`      | —                        | `false` |
| `seq`         | —                        | 0       |
| `reached`     | —                        | `false` |
| `send_sock` / `recv_sock` | —              | `-1`    |

`packet_size` and `wait_time` are not exposed as CLI flags in this
project; they stay at the defaults above and are used by display and
receive logic respectively.

---

## Usage synopsis

```
ft_traceroute [-n] [-m max_ttl] [-q nqueries] [-p port] [-f first_ttl] host
```

Printed by `print_help()` when the user passes `--help`.

| Option         | Effect                                         | Allowed range     | Default |
|----------------|------------------------------------------------|-------------------|---------|
| `--help`       | Print help and exit with status 0              | —                 | —       |
| `-n`           | Do not resolve hop IPs to hostnames            | flag              | off     |
| `-f first_ttl` | Start probing from this TTL                    | 1–255             | 1       |
| `-m max_ttl`   | Stop after this many hops (max TTL)            | 1–255             | 30      |
| `-q nqueries`  | Number of probes sent per TTL hop              | 1–`MAX_PROBES` (10) | 3     |
| `-p port`      | Base UDP destination port for probes           | 1–65535           | 33434   |
| `host`         | Target hostname or IPv4 address (required)     | non-empty string  | —       |

### Why these ranges

- **TTL 1–255** matches the 8-bit IP TTL field.
- **Probes 1–10** matches `MAX_PROBES` and the size of the stack array
  `t_probe probes[MAX_PROBES]` in `main()`.
- **Port 1–65535** is the full UDP port space (0 is rejected because
  the code treats it as invalid; classic traceroute also starts from a
  non-zero base such as 33434).
- **Base port 33434** is the historical traceroute default: a range of
  high, rarely-used ports so destination hosts tend to answer with
  Port Unreachable instead of delivering the datagram to a real service.

---

## Parsing algorithm

```
parse_args(tr, argc, argv):
    if argc < 2 → error "missing host operand"

    i = 1
    while i < argc:
        if argv[i] starts with '-':
            parse_flag(tr, argv, &i)   # may advance i for option values
        else if tr->host already set:
            error "too many arguments"
        else:
            tr->host = argv[i]
        i++

    if tr->host is NULL → error "missing host operand"
    if getuid() != 0   → error "root privileges required"
```

### Option vs operand

Any token that begins with `-` is treated as a flag. There is **no**
`--` terminator and no GNU-style combining of short options (`-nm` is
not supported — it would be reported as a bad option).

Examples of valid layouts:

```bash
sudo ./ft_traceroute google.com
sudo ./ft_traceroute -n 8.8.8.8
sudo ./ft_traceroute -n -m 15 -q 1 -f 2 -p 40000 example.com
sudo ./ft_traceroute -m 10 example.com -n    # host may appear before later flags
```

The last example works because the loop does not require “all options
first”. The only constraint is that exactly one non-option token becomes
`tr->host`.

### Root check position

`getuid() == 0` is verified **after** the full argv scan. That means:

- `./ft_traceroute --help` works without root (exits inside `parse_flag`
  before the root check).
- `./ft_traceroute bad-option` also exits before the root check.
- `./ft_traceroute google.com` without sudo fails with
  `root privileges required` even though DNS would succeed later.

---

## Flag dispatch — `parse_flag()`

| Token     | Action |
|-----------|--------|
| `--help`  | `print_help()` then `exit(EXIT_SUCCESS)` |
| `-n`      | `tr->no_dns = true` |
| `-f`      | `tr->first_ttl = parse_optval(..., "first_ttl", 1, 255)` |
| `-m`      | `tr->max_hops = parse_optval(..., "max_ttl", 1, 255)` |
| `-q`      | `tr->nprobes = parse_optval(..., "nqueries", 1, MAX_PROBES)` |
| `-p`      | `tr->port = parse_optval(..., "port", 1, 65535)` |
| otherwise | print `bad option`, suggest `--help`, exit failure |

Unknown long options (`--foo`) and unknown short options (`-z`) take
the same “bad option” path. Short options must appear as separate
tokens (`-n`, not glued to their value: `-m15` is invalid).

---

## Numeric values — `parse_optval()`

Used for every option that consumes a following argument.

1. Increment `*i` so it points at the value token.
2. If `argv[*i]` is `NULL` (option was last on the command line), print
   `option requires an argument -- '<name>'` and exit.
3. Convert with `ft_atoi_safe()`.
4. If the integer is outside `[min, max]`, print
   `invalid value for \`<name>\': \`<raw>\` (must be min-max)` and exit.
5. Return the validated `int`.

Because `parse_optval` advances `i`, the outer loop’s trailing `i++`
moves to the token **after** the value. Example scan of
`-m 15 host`:

| Step | `i` points to | Action              | Next |
|------|---------------|---------------------|------|
| 1    | `-m`          | enter `parse_flag`  | —    |
| 2    | `15`          | `parse_optval` sets `max_hops` | returns |
| 3    | (outer `i++`) | now on `host`       | —    |
| 4    | `host`        | assign `tr->host`   | done |

---

## Safe integer conversion — `ft_atoi_safe()`

A minimal digit-only parser used instead of libc `atoi` / `strtol`:

- Rejects empty strings.
- Rejects any non-digit character (signs, spaces, hex, trailing junk).
- Accumulates into a `long` and rejects values `> 2147483647` (prevents
  silent wrap when casting to `int`).
- Returns `-1` on any failure.

Because valid option ranges start at `1` (or `1` for TTL/port/probes),
a failed conversion (`-1`) always falls outside the allowed window and
produces the same `invalid value` error path as an out-of-range number.

Rejected examples: `""`, `abc`, `-5`, `+3`, `12a`, ` 5`, `9999999999`.

---

## Help text — `print_help()`

Writes the usage block to **stdout** (not stderr) and is only invoked
for `--help`. It documents every supported option and mirrors the
table above. Exit status after help is `0`.

---

## Error messages

All failure paths write to **stderr** and call `exit(EXIT_FAILURE)`.

| Condition | Message (abbreviated) |
|-----------|------------------------|
| `argc < 2` or only flags / no host | `missing host operand` |
| Two or more non-option tokens | `too many arguments` |
| Unknown flag | `bad option \`...\`` + try `--help` |
| Flag without following value | `option requires an argument -- '...'` |
| Number out of range / not digits | `invalid value for \`...\`` with allowed range |
| Not running as root | `root privileges required` |

After `missing host` when `argc < 2`, the program also suggests
`--help`. Some other paths exit with only the primary message.

---

## State written into `t_traceroute`

| Field       | Set when                         | Consumed by |
|-------------|----------------------------------|-------------|
| `host`      | Non-option argument              | `resolve_host()`, `print_header()` |
| `no_dns`    | `-n`                             | `print_addr()` in display |
| `first_ttl` | `-f`                             | main TTL loop start |
| `max_hops`  | `-m`                             | main loop bound, header |
| `nprobes`   | `-q`                             | probe inner loop, `check_reached` |
| `port`      | `-p`                             | `send_probe()` port sequence |

Fields **not** touched by the CLI layer remain at `init_traceroute()`
defaults (`wait_time`, `packet_size`, sockets, `seq`, etc.).

---

## Interaction with the rest of the program

```
main
 └─ init_traceroute()     ← defaults
 └─ parse_args()          ← THIS MODULE
 └─ resolve_host()        ← needs tr->host
 └─ create_sockets()      ← needs root (already enforced)
 └─ ... traceroute loop uses first_ttl, max_hops, nprobes, port, no_dns
```

There is currently no cross-check that `first_ttl <= max_hops`. If the
user sets `-f 20 -m 10`, the main loop condition
`ttl <= max_hops` never runs a hop and the program exits after printing
only the header. Prefer documenting that callers should keep
`first_ttl ≤ max_hops`.

---

## Examples

```bash
# Help (no root)
./ft_traceroute --help

# Typical run
sudo ./ft_traceroute google.com

# Numeric hops only, shorter path, one probe each
sudo ./ft_traceroute -n -m 15 -q 1 1.1.1.1

# Skip the first four hops, custom base port
sudo ./ft_traceroute -f 5 -p 40000 example.com

# Validation failures (no network used)
./ft_traceroute
./ft_traceroute -q 99 example.com
./ft_traceroute example.com other.com
```
