#!/usr/bin/env bash
# Tests for scripts/fleet/fleet-net.sh — the network-call timeout guard.
#
# Hermetic: no live git/gh, no network. A temp bindir holds fake `timeout`,
# `git`, and `gh` on PATH front. The shadow functions call `command timeout …`
# and `command git`/`command gh`, which do a normal PATH lookup (command only
# skips shell functions/aliases), so the fakes stand in for the real binaries.
# The fake `timeout` prints a marker so a test can prove the guard fired.
# The REST fallback itself is covered by test_fleet_gh_fallback.sh; this suite
# proves which calls it leaves alone.

set -euo pipefail

SCRIPT_DIR=$(cd "$(dirname "$0")/.." && pwd)
source "$(dirname "$0")/lib_preflight.sh"
LIB="$SCRIPT_DIR/fleet-net.sh"
SHIM="$SCRIPT_DIR/timeout-shim.py"

if [[ ! -f "$LIB" ]]; then
    echo "SKIP: lib not found at $LIB" >&2
    exit 3  # skip status — run_all.sh must not count this as a pass
fi

PASS=0
FAIL=0
TMPROOT=""
cleanup() { [[ -n "$TMPROOT" && -d "$TMPROOT" ]] && rm -rf "$TMPROOT"; }
trap cleanup EXIT
TMPROOT=$(mktemp -d)

ok()   { echo "  ok: $1";   PASS=$((PASS + 1)); }
fail() { echo "  FAIL: $1"; FAIL=$((FAIL + 1)); }

# --- fake binaries on PATH ---------------------------------------------------
BIN="$TMPROOT/bin"
mkdir -p "$BIN"

# Fake timeout: marker on stderr, then either force an exit code (FT_RC, to
# simulate a kill) or exec the wrapped command.
cat > "$BIN/timeout" <<'EOF'
#!/usr/bin/env bash
echo "TIMEOUT-INVOKED budget=$1" >&2
shift
if [[ -n "${FT_RC:-}" ]]; then
    exit "$FT_RC"
fi
exec "$@"
EOF

# Fake git/gh: echo which one ran + args, so a test can confirm the subcommand
# reached the binary and (via the marker's absence) that a local op wasn't
# guarded.
cat > "$BIN/git" <<'EOF'
#!/usr/bin/env bash
echo "GIT-RAN args=$*"
EOF
cat > "$BIN/gh" <<'EOF'
#!/usr/bin/env bash
echo "GH-RAN args=$*"
EOF
chmod +x "$BIN/timeout" "$BIN/git" "$BIN/gh"
export PATH="$BIN:$PATH"

# Source the lib with FLEET_TIMEOUT_CMD pinned to our fake, so we don't depend
# on the host actually having coreutils timeout.
export FLEET_TIMEOUT_CMD="timeout"
export FLEET_NET_TIMEOUT="7"
# shellcheck source=/dev/null
source "$LIB"

echo "T1: git fetch is wrapped in the timeout guard"
out=$(git -C /tmp fetch origin master 2>&1 || true)
echo "$out" | grep -q "TIMEOUT-INVOKED budget=7" && ok "fetch invoked timeout (budget passed)" || fail "fetch not guarded: $out"
echo "$out" | grep -q "GIT-RAN args=-C /tmp fetch origin master" && ok "fetch reached git with intact args" || fail "fetch args wrong: $out"

echo "T2: git rev-parse (local) is NOT wrapped"
out=$(git -C /tmp rev-parse HEAD 2>&1 || true)
echo "$out" | grep -q "TIMEOUT-INVOKED" && fail "local op was wrongly guarded: $out" || ok "rev-parse not guarded (no timeout)"
echo "$out" | grep -q "GIT-RAN args=-C /tmp rev-parse HEAD" && ok "rev-parse reached git" || fail "rev-parse args wrong: $out"

echo "T3: git -c k=v push finds 'push' past the -c flag"
out=$(git -c protocol.version=2 push origin HEAD 2>&1 || true)
echo "$out" | grep -q "TIMEOUT-INVOKED" && ok "push guarded past -c flag" || fail "push not guarded: $out"

echo "T3b: git -C <path> checkout (local) stays unguarded"
out=$(git -C /tmp checkout -b x 2>&1 || true)
echo "$out" | grep -q "TIMEOUT-INVOKED" && fail "checkout wrongly guarded: $out" || ok "checkout not guarded"

echo "T4: gh is always wrapped"
out=$(gh pr view 5 --json state 2>&1 || true)
echo "$out" | grep -q "TIMEOUT-INVOKED" && ok "gh guarded" || fail "gh not guarded: $out"
echo "$out" | grep -q "GH-RAN args=pr view 5 --json state" && ok "gh reached binary with intact args" || fail "gh args wrong: $out"

echo "T5: a timed-out network op propagates exit 124"
export FT_RC=124
git -C /tmp fetch origin >/dev/null 2>&1 && rc=0 || rc=$?
unset FT_RC
[[ "$rc" == "124" ]] && ok "fetch returned 124 on timeout" || fail "expected 124, got $rc"
export FT_RC=124
gh pr list >/dev/null 2>&1 && rc=0 || rc=$?
unset FT_RC
[[ "$rc" == "124" ]] && ok "gh returned 124 on timeout" || fail "expected 124, got $rc"

echo "T6: empty FLEET_TIMEOUT_CMD passes through unguarded"
out=$(FLEET_TIMEOUT_CMD="" git -C /tmp fetch origin 2>&1 || true)
echo "$out" | grep -q "TIMEOUT-INVOKED" && fail "guarded despite empty cmd: $out" || ok "no guard when FLEET_TIMEOUT_CMD empty"
echo "$out" | grep -q "GIT-RAN" && ok "git still ran (passthrough)" || fail "git did not run in passthrough: $out"

echo "T7: coreutils probe accepts/rejects by --version"
cat > "$BIN/faketrue" <<'EOF'
#!/usr/bin/env bash
[[ "$1" == "--version" ]] && echo "faketrue (GNU coreutils) 9.0" && exit 0
exit 0
EOF
cat > "$BIN/fakebusybox" <<'EOF'
#!/usr/bin/env bash
[[ "$1" == "--version" ]] && echo "BusyBox v1.36 multi-call binary" && exit 0
exit 0
EOF
chmod +x "$BIN/faketrue" "$BIN/fakebusybox"
if _fleet_net_is_coreutils_timeout faketrue; then ok "probe accepts a coreutils --version"; else fail "probe rejected a coreutils runner"; fi
if _fleet_net_is_coreutils_timeout fakebusybox; then fail "probe accepted a non-coreutils runner"; else ok "probe rejects a non-coreutils runner"; fi
if _fleet_net_is_coreutils_timeout definitely-not-on-path-xyz; then fail "probe accepted a missing binary"; else ok "probe rejects a missing binary"; fi

echo "T8: timeout-shim.py --version passes the probe"
if [[ -f "$SHIM" ]] && command -v python3 >/dev/null 2>&1; then
    python3 "$SHIM" --version 2>/dev/null | grep -qi coreutils && ok "shim --version contains 'coreutils'" || fail "shim --version lacks coreutils marker"
    # And it actually enforces a timeout: a 1s budget on a 30s sleep -> 124.
    python3 "$SHIM" 1 sleep 30 >/dev/null 2>&1 && rc=0 || rc=$?
    [[ "$rc" == "124" ]] && ok "shim returns 124 when the command overruns" || fail "shim timeout rc=$rc, expected 124"
    # A command that finishes in time propagates its own exit code.
    python3 "$SHIM" 5 sh -c 'exit 3' >/dev/null 2>&1 && rc=0 || rc=$?
    [[ "$rc" == "3" ]] && ok "shim propagates the child exit code" || fail "shim exit passthrough rc=$rc, expected 3"
else
    echo "  SKIP: shim or python3 unavailable"
fi

echo "T9: non-candidate gh calls stream unbuffered behind the timeout"
# The fake writes stdout, stderr, stdout. Merged into one stream, a streamed
# call keeps that order; the REST-fallback candidates are buffered and replay
# stdout before stderr.
cat > "$BIN/gh" <<'EOF'
#!/usr/bin/env bash
echo "OUT-1"; echo "ERR-2" >&2; echo "OUT-3"
exit "${FAKE_GH_RC:-0}"
EOF
chmod +x "$BIN/gh"
order() { tr '\n' ' ' <<<"$1" | sed 's/ $//'; }
out=$(gh api repos/o/r/pulls 2>&1 || true)
echo "$out" | grep -q "TIMEOUT-INVOKED budget=7" && ok "gh api still takes the timeout prefix" || fail "gh api not guarded: $out"
[[ "$(order "$(grep -v TIMEOUT <<<"$out")")" == "OUT-1 ERR-2 OUT-3" ]] && ok "gh api streams unbuffered" \
    || fail "gh api was buffered: $(order "$out")"
out=$(gh pr create --title t --body b 2>&1 || true)
echo "$out" | grep -q "TIMEOUT-INVOKED budget=7" && ok "gh pr create still takes the timeout prefix" || fail "gh pr create not guarded: $out"
[[ "$(order "$(grep -v TIMEOUT <<<"$out")")" == "OUT-1 ERR-2 OUT-3" ]] && ok "gh pr create streams unbuffered" \
    || fail "gh pr create was buffered: $(order "$out")"
out=$(gh pr view 5 --json state 2>&1 || true)
[[ "$(order "$(grep -v TIMEOUT <<<"$out")")" == "OUT-1 OUT-3 ERR-2" ]] && ok "gh pr view (a fallback candidate) is buffered" \
    || fail "gh pr view order: $(order "$out")"

echo "T10: a candidate failure that is not a GraphQL refusal replays as-is"
export FLEET_STATE_DIR="$TMPROOT/state"
stdout=$(FAKE_GH_RC=5 gh issue view 5 --json state 2>"$TMPROOT/err") && rc=0 || rc=$?
[[ "$rc" == "5" ]] && ok "exit status 5 preserved" || fail "expected 5, got $rc"
[[ "$(order "$stdout")" == "OUT-1 OUT-3" ]] && ok "stdout replayed" || fail "stdout: $stdout"
grep -q "ERR-2" "$TMPROOT/err" && ok "stderr replayed" || fail "stderr: $(cat "$TMPROOT/err")"
[[ ! -e "$FLEET_STATE_DIR/usage/github-graphql.rejected.json" ]] && ok "no refusal latch written" \
    || fail "a non-refusal failure latched the gate"

echo ""
echo "PASS: $PASS  FAIL: $FAIL"
[[ $FAIL -eq 0 ]]
