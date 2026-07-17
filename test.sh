#!/usr/bin/env bash

ROOT_DIR="$(cd "$(dirname "$0")" && pwd)"
BIN="$ROOT_DIR/ft_traceroute"
OUT="/tmp/ft_traceroute_test_out.$$"
ERR="/tmp/ft_traceroute_test_err.$$"
PASS=0
FAIL=0
SKIP=0
RUN_NETWORK=0

GREEN='\033[1;32m'
RED='\033[1;31m'
YELLOW='\033[1;33m'
BLUE='\033[1;34m'
CYAN='\033[1;36m'
RESET='\033[0m'

cleanup() {
	rm -f "$OUT" "$ERR"
}

trap cleanup EXIT

usage() {
	echo "Usage: $0 [--network]"
	echo "  (default)   build + CLI tests"
	echo "  --network   also run network tests (requires root)"
}

while [[ $# -gt 0 ]]; do
	case "$1" in
		--network|-n)
			RUN_NETWORK=1
			shift
			;;
		--help|-h)
			usage
			exit 0
			;;
		*)
			usage
			exit 1
			;;
	esac
done

pass() {
	printf "${GREEN}[PASS]${RESET} %s\n" "$1"
	PASS=$((PASS + 1))
}

fail() {
	printf "${RED}[FAIL]${RESET} %s\n" "$1"
	if [[ -s "$ERR" ]]; then
		sed 's/^/       stderr: /' "$ERR"
	fi
	if [[ -s "$OUT" ]]; then
		sed 's/^/       stdout: /' "$OUT" | head -n 5
	fi
	FAIL=$((FAIL + 1))
}

skip() {
	printf "${YELLOW}[SKIP]${RESET} %s\n" "$1"
	SKIP=$((SKIP + 1))
}

run_bin() {
	"$BIN" "$@" >"$OUT" 2>"$ERR"
	RC=$?
}

expect_exit() {
	local desc="$1"
	local want_rc="$2"
	shift 2
	run_bin "$@"
	if [[ "$RC" -eq "$want_rc" ]]; then
		pass "$desc"
	else
		fail "$desc (exit=$RC, expected=$want_rc)"
	fi
}

expect_stderr() {
	local desc="$1"
	local want_rc="$2"
	local pattern="$3"
	shift 3
	run_bin "$@"
	if [[ "$RC" -eq "$want_rc" ]] && grep -qE "$pattern" "$ERR"; then
		pass "$desc"
	else
		fail "$desc (exit=$RC, pattern='$pattern')"
	fi
}

expect_stdout() {
	local desc="$1"
	local want_rc="$2"
	local pattern="$3"
	shift 3
	run_bin "$@"
	if [[ "$RC" -eq "$want_rc" ]] && grep -qE "$pattern" "$OUT"; then
		pass "$desc"
	else
		fail "$desc (exit=$RC, pattern='$pattern')"
	fi
}

printf "${BLUE}=== Build ===${RESET}\n"
cd "$ROOT_DIR"
if make re >"$OUT" 2>"$ERR" && [[ -x "$BIN" ]]; then
	pass "make re builds executable"
else
	fail "make re"
	echo ""
	printf "Results: ${GREEN}%s passed${RESET}, ${RED}%s failed${RESET}, ${YELLOW}%s skipped${RESET}\n" "$PASS" "$FAIL" "$SKIP"
	exit 1
fi

echo ""
printf "${BLUE}=== CLI ===${RESET}\n"
expect_exit "C1 --help exits 0" 0 --help
expect_stderr "C2 missing host" 1 "missing host operand"
expect_stderr "C3 too many arguments" 1 "too many arguments" example.com google.com
expect_stderr "C4 bad option -z" 1 "bad option" -z example.com
expect_stderr "C5 bad option --unknown" 1 "bad option" --unknown example.com
expect_stderr "C6 -f missing value" 1 "option requires an argument -- 'first_ttl'" -f
expect_stderr "C7 -m missing value" 1 "option requires an argument -- 'max_ttl'" -m
expect_stderr "C8 -q missing value" 1 "option requires an argument -- 'nqueries'" -q
expect_stderr "C9 -p missing value" 1 "option requires an argument -- 'port'" -p
expect_stderr "C10 -f 0 invalid" 1 "invalid value for .first_ttl" -f 0 example.com
expect_stderr "C11 -f 256 invalid" 1 "invalid value for .first_ttl" -f 256 example.com
expect_stderr "C12 -m 0 invalid" 1 "invalid value for .max_ttl" -m 0 example.com
expect_stderr "C13 -q 0 invalid" 1 "invalid value for .nqueries" -q 0 example.com
expect_stderr "C14 -q 11 invalid" 1 "invalid value for .nqueries" -q 11 example.com
expect_stderr "C15 -p 0 invalid" 1 "invalid value for .port" -p 0 example.com
expect_stderr "C16 -f abc invalid" 1 "invalid value for .first_ttl" -f abc example.com

if [[ "$EUID" -ne 0 ]]; then
	expect_stderr "C17 root required" 1 "root privileges required" example.com
else
	skip "C17 root required (already root)"
fi

if [[ "$RUN_NETWORK" -eq 1 ]]; then
	echo ""
	printf "${BLUE}=== Network ===${RESET}\n"
	if [[ "$EUID" -ne 0 ]]; then
		printf "${CYAN}Network tests require root. Re-run as:${RESET}\n"
		echo "  sudo $0 --network"
		skip "all network tests (not root)"
	else
		expect_stdout "N1 traceroute 1.1.1.1" 0 "traceroute to 1.1.1.1" -n -m 5 -q 1 1.1.1.1
		expect_stdout "N2 resolve google.com" 0 "traceroute to google.com \(" -m 3 -q 1 google.com
		expect_stdout "N3 -n shows no hostname lookup format check" 0 "traceroute to 8.8.8.8" -n -m 3 -q 1 8.8.8.8
		run_bin -n -m 5 -q 1 1.1.1.1
		hop_count="$(grep -cE '^[ ]*[0-9]+' "$OUT" || true)"
		if [[ "$RC" -eq 0 && "$hop_count" -ge 1 && "$hop_count" -le 5 ]]; then
			pass "N4 -m 5 limits hops (got $hop_count)"
		else
			fail "N4 -m 5 limits hops (exit=$RC hops=$hop_count)"
		fi
		run_bin -n -f 3 -m 5 -q 1 1.1.1.1
		if [[ "$RC" -eq 0 ]] && grep -qE '^ *3' "$OUT"; then
			pass "N5 -f 3 starts at hop 3"
		else
			fail "N5 -f 3 starts at hop 3"
		fi
		run_bin -n -m 3 -q 1 1.1.1.1
		if [[ "$RC" -eq 0 ]]; then
			pass "N6 -q 1 runs"
		else
			fail "N6 -q 1 runs"
		fi
		expect_stdout "N7 -p 40000" 0 "traceroute to 1.1.1.1" -n -m 3 -q 1 -p 40000 1.1.1.1
		expect_stderr "N8 invalid hostname" 1 "invalid\.host\.42test" invalid.host.42test
		expect_stderr "N9 invalid ipv4" 1 "999\.999\.999\.999" 999.999.999.999
		expect_stdout "N10 reachable host" 0 "traceroute to 1.1.1.1" -n -m 15 -q 1 1.1.1.1
		expect_stdout "N11 limited unreachable-ish target" 0 "traceroute to" -n -m 3 -q 1 10.255.255.1
	fi
else
	echo ""
	printf "${BLUE}=== Network ===${RESET}\n"
	skip "network tests (pass --network as root to enable)"
fi

echo ""
printf "Results: ${GREEN}%s passed${RESET}, ${RED}%s failed${RESET}, ${YELLOW}%s skipped${RESET}\n" "$PASS" "$FAIL" "$SKIP"
if [[ "$FAIL" -eq 0 ]]; then
	exit 0
fi
exit 1
