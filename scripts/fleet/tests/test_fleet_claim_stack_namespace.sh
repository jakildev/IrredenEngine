#!/usr/bin/env bash
# Tests that `fleet-claim`'s stack/molecule record CARRIES its --repo namespace
# (#2857), so a cross-repo stack can be released without the caller re-supplying
# the flag from memory on a later invocation.
#
# The incident this locks: $CLAIMS_DIR/_stack_<agent>/tasks and the molecule both
# store RAW issue ids, while only the claim-dir slug is namespaced (game-45).
# `release-stack` re-slugifies via REPO_NS, so a game stack released without
# `--repo game` released NOTHING, printed "released stack (2 tasks)", exited 0,
# and deleted both the stack dir and the molecule in the same call — orphaning
# every claim with no record left to reconstruct them from. Every game id is also
# a live engine id, so the raw id could never disambiguate on its own.
#
# The state is seeded by hand rather than via `fleet-claim stack`, which does
# live blocker checks. Releases DO reach _release_inactive_issue_labels, so gh is
# stubbed to a failing no-op: hermetic per scripts/fleet/CLAUDE.md — a stub miss
# cannot fall through to the real gh, because the stub IS the whole PATH entry.
#
# Covers:
#   - stack stamps the namespace into _stack_<agent>/ns and the molecule
#   - release-stack with no --repo adopts the recorded namespace (the regression)
#   - release-stack with the matching --repo still works (positive control)
#   - engine-lane stack with no --repo still works (negative control)
#   - a contradicting --repo refuses AND preserves the stack dir + molecule
#   - a pre-#2857 record (no ns file, no repo: meta) keeps legacy behavior
#   - molecule resume keeps stdout a bare id and reports the namespace on stderr
#   - the repo: meta survives a molecule rewrite (advance/resume re-emit)

set -euo pipefail

export FLEET_SKIP_CLONE_FRESHNESS=1

SCRIPT_DIR=$(cd "$(dirname "$0")/.." && pwd)
FLEET_CLAIM="$SCRIPT_DIR/fleet-claim"

if [[ ! -x "$FLEET_CLAIM" ]]; then
    echo "test setup: fleet-claim not found at $FLEET_CLAIM" >&2
    exit 1
fi

source "$(dirname "$0")/lib_assert.sh"

TMPROOT=""
cleanup() { [[ -n "$TMPROOT" && -d "$TMPROOT" ]] && rm -rf "$TMPROOT"; }
trap cleanup EXIT
TMPROOT=$(mktemp -d)

# gh stub: logs and fails, so no assertion can be satisfied by live GitHub.
mkdir -p "$TMPROOT/bin"
cat > "$TMPROOT/bin/gh" <<'STUB'
#!/bin/sh
echo "gh $*" >> "$GH_LOG"
exit 1
STUB
chmod +x "$TMPROOT/bin/gh"
export GH_LOG="$TMPROOT/gh.log"
: > "$GH_LOG"

AGENT=pool-Z

assert_exit() {
    local actual="$1" expected="$2" msg="$3"
    if [[ "$actual" -eq "$expected" ]]; then
        ok "$msg"
    else
        bad "$msg"
        echo "        expected exit: $expected"
        echo "        actual exit:   $actual"
    fi
}

# seed <case> <ns-token|--legacy> <slug>...
#   Build the on-disk state `fleet-claim stack` produces: one claim dir per
#   namespaced slug, a stack dir whose `tasks` holds RAW ids, and a molecule.
#   `--legacy` omits both namespace fields (the pre-#2857 record shape).
seed() {
    local case_dir="$TMPROOT/$1" ns="$2"; shift 2
    rm -rf "$case_dir"
    mkdir -p "$case_dir/claims" "$case_dir/mol" "$case_dir/res"
    local slug raw
    : > "$case_dir/raws"
    for slug in "$@"; do
        raw="${slug#game-}"
        mkdir -p "$case_dir/claims/$slug"
        echo "$AGENT" > "$case_dir/claims/$slug/owner"
        echo "$raw"   > "$case_dir/claims/$slug/title"
        date +%s      > "$case_dir/claims/$slug/created"
        echo stack    > "$case_dir/claims/$slug/stack"
        echo "$raw"  >> "$case_dir/raws"
    done
    mkdir -p "$case_dir/claims/_stack_$AGENT"
    cp "$case_dir/raws" "$case_dir/claims/_stack_$AGENT/tasks"
    date +%s > "$case_dir/claims/_stack_$AGENT/created"
    {
        echo "name: $AGENT-probe"
        echo "agent: $AGENT"
        echo "created: 2026-08-04T00:00:00Z"
        echo "branch: claude/probe"
        [[ "$ns" == "--legacy" ]] || echo "repo: $ns"
        echo "tasks:"
        for slug in "$@"; do
            echo "  - id: ${slug#game-}"
            echo "    state: done"
        done
    } > "$case_dir/mol/$AGENT.yml"
    [[ "$ns" == "--legacy" ]] || echo "$ns" > "$case_dir/claims/_stack_$AGENT/ns"
}

# fc <case> <fleet-claim args...>
fc() {
    local case_dir="$TMPROOT/$1"; shift
    PATH="$TMPROOT/bin:$PATH" \
    FLEET_CLAIMS_DIR="$case_dir/claims" \
    FLEET_MOLECULES_DIR="$case_dir/mol" \
    FLEET_RESERVATIONS_DIR="$case_dir/res" \
        bash "$FLEET_CLAIM" "$@" 2>&1
}

claim_dirs() {
    ls "$TMPROOT/$1/claims" 2>/dev/null | grep -v '^_stack_' | sort | tr '\n' ' '
}

# --- T1: stack stamps the namespace into its own record ----------------------
# Driven through molecule_write_initial + the stack-dir writer rather than
# `fleet-claim stack` (which does live blocker checks) by sourcing the lib seam.
echo "T1: a game-namespace stack records its namespace in both stores"
T1="$TMPROOT/t1"; mkdir -p "$T1/claims" "$T1/mol"
(
    export FLEET_CLAIMS_DIR="$T1/claims" FLEET_MOLECULES_DIR="$T1/mol"
    export FLEET_CLAIM_LIB=1
    # shellcheck disable=SC1090
    source "$FLEET_CLAIM"
    REPO_NS=game
    mkdir -p "$FLEET_CLAIMS_DIR/_stack_$AGENT"
    _ns_token > "$FLEET_CLAIMS_DIR/_stack_$AGENT/ns"
    molecule_write_initial "$AGENT" 9001 9002
) >/dev/null 2>&1 || true
assert_eq "$(cat "$T1/claims/_stack_$AGENT/ns" 2>/dev/null)" "game" \
    "stack dir records ns=game"
assert_contains "$(cat "$T1/mol/$AGENT.yml")" "repo: game" \
    "molecule records repo: game"

echo "T2: the engine (default) namespace is recorded explicitly, not as empty"
T2="$TMPROOT/t2"; mkdir -p "$T2/claims" "$T2/mol"
(
    export FLEET_CLAIMS_DIR="$T2/claims" FLEET_MOLECULES_DIR="$T2/mol"
    export FLEET_CLAIM_LIB=1
    # shellcheck disable=SC1090
    source "$FLEET_CLAIM"
    REPO_NS=""
    molecule_write_initial "$AGENT" 9001
) >/dev/null 2>&1 || true
assert_contains "$(cat "$T2/mol/$AGENT.yml")" "repo: engine" \
    "default namespace records as 'engine' (distinguishable from unrecorded)"

# --- T3: THE REGRESSION -----------------------------------------------------
echo "T3: game stack, release-stack WITHOUT --repo — adopts the record"
seed rel-adopt game game-9001 game-9002
out=$(fc rel-adopt release-stack "$AGENT")
assert_contains "$out" "adopting it (no --repo passed)" \
    "release-stack reports adopting the recorded namespace"
assert_contains "$out" "slug: game-9001" \
    "release-stack released the game-namespaced slug"
assert_eq "$(claim_dirs rel-adopt)" "" \
    "no orphaned claim dirs remain (pre-fix: game-9001 game-9002 survived)"
assert_absent "$out" "not claimed: #9001" \
    "no 'not claimed' miss (pre-fix: both ids missed under the engine slug)"

echo "T4: positive control — same state WITH the matching --repo game"
seed rel-flag game game-9001 game-9002
out=$(fc rel-flag --repo game release-stack "$AGENT")
assert_contains "$out" "released stack (2 tasks)" "release-stack reports success"
assert_eq "$(claim_dirs rel-flag)" "" "both game claims released"
assert_absent "$out" "adopting it" "no adopt notice when the flag already matches"

echo "T5: negative control — engine-lane stack, no flag, unchanged behavior"
seed rel-engine engine 9001 9002
out=$(fc rel-engine release-stack "$AGENT")
assert_contains "$out" "slug: 9001" "engine slug released"
assert_eq "$(claim_dirs rel-engine)" "" "both engine claims released"
assert_absent "$out" "adopting it" "no adopt notice on the engine lane"

# --- T6: a contradiction must refuse AND preserve ---------------------------
echo "T6: --repo contradicts the record — refuse, release nothing, preserve"
seed rel-conflict game game-9001 game-9002
actual=0; out=$(fc rel-conflict --repo other release-stack "$AGENT") || actual=$?
assert_exit "$actual" 2 "contradicting --repo → exit 2"
assert_contains "$out" "refusing" "refusal is explicit"
assert_contains "$out" "Re-run with --repo game" "names the corrective invocation"
assert_eq "$(claim_dirs rel-conflict)" "game-9001 game-9002 " \
    "claims preserved on refusal"
[[ -f "$TMPROOT/rel-conflict/claims/_stack_$AGENT/tasks" ]] \
    && ok "stack record preserved on refusal" \
    || bad "stack record preserved on refusal"
[[ -f "$TMPROOT/rel-conflict/mol/$AGENT.yml" ]] \
    && ok "molecule preserved on refusal (not archived)" \
    || bad "molecule preserved on refusal (not archived)"

echo "T7: engine record + --repo game also refuses (both directions)"
seed rel-conflict2 engine 9001
actual=0; out=$(fc rel-conflict2 --repo game release-stack "$AGENT") || actual=$?
assert_exit "$actual" 2 "engine record vs --repo game → exit 2"
assert_contains "$out" "Re-run with no --repo flag" \
    "corrective for the engine lane is 'no --repo flag'"

# --- T8: legacy records keep the old behavior -------------------------------
echo "T8: a pre-#2857 record (no ns, no repo: meta) keeps legacy behavior"
seed rel-legacy --legacy game-9001
out=$(fc rel-legacy release-stack "$AGENT")
assert_contains "$out" "not claimed: #9001" \
    "legacy record still resolves against the caller's namespace"
assert_absent "$out" "adopting it" "no adopt notice without a recorded namespace"
assert_absent "$out" "refusing" "a legacy record is never treated as a conflict"

# --- T9: resume keeps stdout parseable, reports the namespace on stderr ------
echo "T9: molecule resume — bare id on stdout, namespace on stderr"
seed res game game-9001 game-9002
python3 - "$TMPROOT/res/mol/$AGENT.yml" <<'PY'
import sys
p = sys.argv[1]
# Read fully before opening for write — the one-liner form truncates first.
body = open(p).read().replace("state: done", "state: pending")
open(p, "w").write(body)
PY
res_out=$(PATH="$TMPROOT/bin:$PATH" FLEET_CLAIMS_DIR="$TMPROOT/res/claims" \
    FLEET_MOLECULES_DIR="$TMPROOT/res/mol" FLEET_RESERVATIONS_DIR="$TMPROOT/res/res" \
    bash "$FLEET_CLAIM" molecule resume "$AGENT" 2>"$TMPROOT/res.err")
assert_eq "$res_out" "9001" "stdout is exactly the bare issue id"
assert_contains "$(cat "$TMPROOT/res.err")" "molecule namespace: game" \
    "stderr names the namespace a fresh context would otherwise have to guess"

echo "T10: the repo: meta survives the rewrite resume performs"
assert_contains "$(cat "$TMPROOT/res/mol/$AGENT.yml")" "repo: game" \
    "repo: meta re-emitted after resume rewrote the molecule"

echo "T11: molecule list annotates the namespace beside the agent"
assert_contains "$(fc res molecule list)" "[game]" \
    "molecule list shows the namespace (its task ids are raw)"

# --- T12: hermeticity -------------------------------------------------------
echo "T12: no live GitHub reached"
if grep -qE '^gh (issue|pr) (edit|create|close)' "$GH_LOG"; then
    bad "gh mutation attempted through the stub (would have been live without it)"
else
    ok "no gh mutation escaped the stub"
fi

summarize
