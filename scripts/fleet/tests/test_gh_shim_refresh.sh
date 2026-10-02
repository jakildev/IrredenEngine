#!/usr/bin/env bash
# Tests for scripts/fleet/gh — the fleet shim that re-mints a stale `ghs_`
# installation token per call and exec's the real gh.
#
# fleet-gh-token and the "real" gh are PATH stubs: the real-gh stub prints the
# GH_TOKEN it sees (or "<unset>"), its argv, and the PATH it was handed, so each
# case asserts what the real binary would have authenticated with.
#
# Covers: stale ghs_ refreshed; operator PAT untouched; empty/unset GH_TOKEN left
# alone even when a token is mintable (keychain-auth callers stay on the user
# pool); empty or failing mint keeps the incoming token; FLEET_GH_SHIM=0 bypass;
# no recursion when the shim dir is first on PATH (direct, symlink-installed,
# and trailing-slash spellings); argv/PATH/exit code pass-through.

set -euo pipefail

SCRIPT_DIR=$(cd "$(dirname "$0")/.." && pwd)
source "$(dirname "$0")/lib_preflight.sh"
SHIM="$SCRIPT_DIR/gh"
[[ -x "$SHIM" ]] || { echo "SKIP: gh shim not found at $SHIM" >&2; exit 3; }

source "$(dirname "$0")/lib_assert.sh"

TMPROOT=$(mktemp -d)
cleanup() { [[ -n "$TMPROOT" && -d "$TMPROOT" ]] && rm -rf "$TMPROOT"; }
trap cleanup EXIT

STUBS="$TMPROOT/stubs"      # fleet-gh-token stub
REAL="$TMPROOT/real"        # the "real" gh
INSTALLED="$TMPROOT/home/bin"  # where install.sh would symlink the shim
mkdir -p "$STUBS" "$REAL" "$INSTALLED"

# Mint stub: prints $GHT_STUB_OUT, exits $GHT_STUB_RC, counts calls.
cat > "$STUBS/fleet-gh-token" <<'SH'
#!/usr/bin/env bash
echo x >> "$GHT_STUB_CALLS"
[[ -n "${GHT_STUB_OUT:-}" ]] && printf '%s\n' "$GHT_STUB_OUT"
exit "${GHT_STUB_RC:-0}"
SH
cat > "$REAL/gh" <<'SH'
#!/usr/bin/env bash
echo x >> "$GH_REAL_CALLS"
echo "token=${GH_TOKEN-<unset>}"
echo "argv=$*"
echo "path=$PATH"
exit "${GH_REAL_RC:-0}"
SH
chmod +x "$STUBS/fleet-gh-token" "$REAL/gh"
ln -s "$SHIM" "$INSTALLED/gh"

export GHT_STUB_CALLS="$TMPROOT/ght.calls"
export GH_REAL_CALLS="$TMPROOT/real.calls"
: > "$GHT_STUB_CALLS"; : > "$GH_REAL_CALLS"
base_path="/usr/bin:/bin"

# run_shim <shim-dir-spelling> [VAR=val ...] -- <gh args> : runs the shim as
# `gh` resolved through PATH = <shim dir>:$STUBS:$REAL:base, with exactly the
# given env additions on top of a scrubbed GH_TOKEN/FLEET_GH_SHIM.
run_shim() {
    local shimdir="$1"; shift
    local -a envs=()
    while [[ "$1" != "--" ]]; do envs+=("$1"); shift; done
    shift
    env -u GH_TOKEN -u FLEET_GH_SHIM "${envs[@]}" \
        PATH="$shimdir:$STUBS:$REAL:$base_path" gh "$@"
}

token_seen() { sed -n 's/^token=//p' <<<"$1"; }

echo "T1: stale ghs_ token is replaced by the freshly minted one"
out=$(run_shim "$INSTALLED" GH_TOKEN=ghs_old GHT_STUB_OUT=ghs_new -- api user)
assert_eq "$(token_seen "$out")" "ghs_new" "real gh sees ghs_new, not ghs_old"
assert_contains "$out" "argv=api user" "argv passed through verbatim"

echo "T2: an operator PAT is never overridden"
for pat in ghp_operator github_pat_abc; do
    out=$(run_shim "$INSTALLED" GH_TOKEN="$pat" GHT_STUB_OUT=ghs_new -- api user)
    assert_eq "$(token_seen "$out")" "$pat" "$pat passes through untouched"
done

echo "T3: empty/unset GH_TOKEN stays keychain auth even with a mintable token"
out=$(run_shim "$INSTALLED" GHT_STUB_OUT=ghs_new -- api user)
assert_eq "$(token_seen "$out")" "<unset>" "unset GH_TOKEN stays unset"
out=$(run_shim "$INSTALLED" GH_TOKEN= GHT_STUB_OUT=ghs_new -- api user)
assert_eq "$(token_seen "$out")" "" "empty GH_TOKEN stays empty (not replaced)"

echo "T4: an empty or failing mint keeps the incoming token (never writes empty)"
out=$(run_shim "$INSTALLED" GH_TOKEN=ghs_old -- api user)
assert_eq "$(token_seen "$out")" "ghs_old" "mint prints nothing -> ghs_old kept"
out=$(run_shim "$INSTALLED" GH_TOKEN=ghs_old GHT_STUB_OUT=ghs_new GHT_STUB_RC=1 -- api user)
assert_eq "$(token_seen "$out")" "ghs_new" "mint prints then exits 1 -> printed value used"
out=$(run_shim "$INSTALLED" GH_TOKEN=ghs_old GHT_STUB_RC=1 -- api user)
assert_eq "$(token_seen "$out")" "ghs_old" "mint fails silently -> ghs_old kept"

echo "T5: FLEET_GH_SHIM=0 bypasses the refresh"
: > "$GHT_STUB_CALLS"
out=$(run_shim "$INSTALLED" GH_TOKEN=ghs_old GHT_STUB_OUT=ghs_new FLEET_GH_SHIM=0 -- api user)
assert_eq "$(token_seen "$out")" "ghs_old" "bypass leaves ghs_old"
assert_eq "$(wc -l < "$GHT_STUB_CALLS" | tr -d ' ')" "0" "bypass never calls fleet-gh-token"

echo "T6: no recursion — real gh runs exactly once, whichever way the shim is reached"
for spelling in "$INSTALLED" "$INSTALLED/"; do
    : > "$GH_REAL_CALLS"
    out=$(run_shim "$spelling" GH_TOKEN=ghs_old GHT_STUB_OUT=ghs_new -- version)
    assert_eq "$(wc -l < "$GH_REAL_CALLS" | tr -d ' ')" "1" "real gh ran once via PATH entry '$spelling'"
    assert_eq "$(token_seen "$out")" "ghs_new" "refresh applied via PATH entry '$spelling'"
done

# Run straight from scripts/fleet/ (no ~/bin symlink): the real gh is found, the
# shim skips itself. The mint stub leads PATH — scripts/fleet/ holds the real
# fleet-gh-token and must never be ahead of it.
: > "$GH_REAL_CALLS"
out=$(env -u GH_TOKEN GH_TOKEN=ghs_old GHT_STUB_OUT=ghs_new PATH="$STUBS:$REAL:$base_path" "$SHIM" version)
assert_eq "$(wc -l < "$GH_REAL_CALLS" | tr -d ' ')" "1" "real gh ran once when the shim is run by repo path"
assert_eq "$(token_seen "$out")" "ghs_new" "refresh applied when the shim is run by repo path"

echo "T7: PATH is passed through unchanged; exit code preserved"
out=$(run_shim "$INSTALLED" GH_TOKEN=ghs_old GHT_STUB_OUT=ghs_new -- version)
assert_contains "$out" "path=$INSTALLED:$STUBS:$REAL:$base_path" "real gh sees the caller's full PATH"
rc=0
run_shim "$INSTALLED" GH_TOKEN=ghp_x GH_REAL_RC=7 -- version >/dev/null || rc=$?
assert_eq "$rc" "7" "real gh's exit code is preserved"

echo "T8: no real gh on PATH -> exit 127 with a diagnostic, no loop"
rc=0
err=$(env -u GH_TOKEN PATH="$INSTALLED:$base_path" gh version 2>&1 >/dev/null) || rc=$?
assert_eq "$rc" "127" "missing real gh exits 127"
assert_contains "$err" "no real gh found" "diagnostic names the problem"

summarize "gh shim tests"
