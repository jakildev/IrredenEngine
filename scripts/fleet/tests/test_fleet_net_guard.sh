#!/usr/bin/env bash
# Tests for scripts/fleet/fleet-net.sh — the network-call timeout guard (#2362).
#
# Hermetic: no live git/gh, no network. A temp bindir holds fake `timeout`,
# `git`, and `gh` on PATH front. The shadow functions call `command timeout …`
# and `command git`/`command gh`, which do a normal PATH lookup (command only
# skips shell functions/aliases), so the fakes stand in for the real binaries.
# The fake `timeout` prints a marker so a test can prove the guard fired.

set -euo pipefail

SCRIPT_DIR=$(cd "$(dirname "$0")/.." && pwd)
LIB="$SCRIPT_DIR/fleet-net.sh"
SHIM="$SCRIPT_DIR/timeout-shim.py"

if [[ ! -f "$LIB" ]]; then
    echo "SKIP: lib not found at $LIB" >&2
    exit 0
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

# --- T1: network subcommand is guarded ---------------------------------------
echo "T1: git fetch is wrapped in the timeout guard"
out=$(git -C /tmp fetch origin master 2>&1 || true)
echo "$out" | grep -q "TIMEOUT-INVOKED budget=7" && ok "fetch invoked timeout (budget passed)" || fail "fetch not guarded: $out"
echo "$out" | grep -q "GIT-RAN args=-C /tmp fetch origin master" && ok "fetch reached git with intact args" || fail "fetch args wrong: $out"

# --- T2: local subcommand passes through unguarded ---------------------------
echo "T2: git rev-parse (local) is NOT wrapped"
out=$(git -C /tmp rev-parse HEAD 2>&1 || true)
echo "$out" | grep -q "TIMEOUT-INVOKED" && fail "local op was wrongly guarded: $out" || ok "rev-parse not guarded (no timeout)"
echo "$out" | grep -q "GIT-RAN args=-C /tmp rev-parse HEAD" && ok "rev-parse reached git" || fail "rev-parse args wrong: $out"

# --- T3: -c global flag is skipped when finding the subcommand ----------------
echo "T3: git -c k=v push finds 'push' past the -c flag"
out=$(git -c protocol.version=2 push origin HEAD 2>&1 || true)
echo "$out" | grep -q "TIMEOUT-INVOKED" && ok "push guarded past -c flag" || fail "push not guarded: $out"

# --- T3b: a local op behind -C is still not guarded --------------------------
echo "T3b: git -C <path> checkout (local) stays unguarded"
out=$(git -C /tmp checkout -b x 2>&1 || true)
echo "$out" | grep -q "TIMEOUT-INVOKED" && fail "checkout wrongly guarded: $out" || ok "checkout not guarded"

# --- T4: every gh call is guarded --------------------------------------------
echo "T4: gh is always wrapped"
out=$(gh pr view 5 --json state 2>&1 || true)
echo "$out" | grep -q "TIMEOUT-INVOKED" && ok "gh guarded" || fail "gh not guarded: $out"
echo "$out" | grep -q "GH-RAN args=pr view 5 --json state" && ok "gh reached binary with intact args" || fail "gh args wrong: $out"

# --- T5: exit 124 (timeout kill) propagates through the shadow ----------------
echo "T5: a timed-out network op propagates exit 124"
export FT_RC=124
git -C /tmp fetch origin >/dev/null 2>&1 && rc=0 || rc=$?
unset FT_RC
[[ "$rc" == "124" ]] && ok "fetch returned 124 on timeout" || fail "expected 124, got $rc"
export FT_RC=124
gh pr list >/dev/null 2>&1 && rc=0 || rc=$?
unset FT_RC
[[ "$rc" == "124" ]] && ok "gh returned 124 on timeout" || fail "expected 124, got $rc"

# --- T6: empty FLEET_TIMEOUT_CMD is a pure passthrough -----------------------
echo "T6: empty FLEET_TIMEOUT_CMD passes through unguarded"
out=$(FLEET_TIMEOUT_CMD="" git -C /tmp fetch origin 2>&1 || true)
echo "$out" | grep -q "TIMEOUT-INVOKED" && fail "guarded despite empty cmd: $out" || ok "no guard when FLEET_TIMEOUT_CMD empty"
echo "$out" | grep -q "GIT-RAN" && ok "git still ran (passthrough)" || fail "git did not run in passthrough: $out"

# --- T7: the coreutils probe rejects a non-coreutils timeout -----------------
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

# --- T8: the fallback shim advertises coreutils (so the probe accepts it) -----
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

echo ""
echo "PASS: $PASS  FAIL: $FAIL"
[[ $FAIL -eq 0 ]]
