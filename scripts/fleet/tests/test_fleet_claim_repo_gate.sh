#!/usr/bin/env bash
# Tests for fleet-claim's check_repo_tag gate.
#
# The gate refuses a claim when the calling role's repo (FLEET_ROLE_REPO,
# set by fleet-dispatch-wrap from the pane's worktree) doesn't match the
# task's `--repo` namespace (empty/"engine" => engine, "game" => game). It
# keeps an engine-worktree worker from claiming a game task (and vice versa).
# A `gh` stub lets accepted claims complete without a live GitHub round-trip.
#
# Covers:
#   - engine role accepts an engine task, rejects a game task
#   - game role accepts a game task, rejects an engine task
#   - FLEET_ROLE_REPO unset/empty passes any repo (opt-in no-op)

set -euo pipefail

SCRIPT_DIR=$(cd "$(dirname "$0")/.." && pwd)
FLEET_CLAIM="$SCRIPT_DIR/fleet-claim"

if [[ ! -x "$FLEET_CLAIM" ]]; then
    echo "test setup: fleet-claim not found at $FLEET_CLAIM" >&2
    exit 1
fi

PASS=0
FAIL=0
TMPROOT=""

cleanup() {
    [[ -n "$TMPROOT" && -d "$TMPROOT" ]] && rm -rf "$TMPROOT"
}
trap cleanup EXIT

assert_exit() {
    local actual_exit="$1" expected_exit="$2" msg="$3"
    if [[ "$actual_exit" -eq "$expected_exit" ]]; then
        PASS=$((PASS + 1))
        echo "  ok: $msg"
    else
        FAIL=$((FAIL + 1))
        echo "  FAIL: $msg"
        echo "        expected exit: $expected_exit"
        echo "        actual exit:   $actual_exit"
    fi
}

TMPROOT=$(mktemp -d)
export FLEET_CLAIMS_DIR="$TMPROOT/claims"
export FLEET_RESERVATIONS_DIR="$TMPROOT/reservations"
mkdir -p "$FLEET_CLAIMS_DIR" "$FLEET_RESERVATIONS_DIR"

# Stub `gh` so accepted claims complete without GitHub. Issue 2001 is OPEN,
# fleet:queued, no blockers, no model label (so the model gate is a no-op).
# The label-acquire POST echoes the requested label back as the lex-min
# winner; pr list / issue edit are no-ops.
STUB_DIR="$TMPROOT/bin"
mkdir -p "$STUB_DIR"
cat >"$STUB_DIR/gh" <<'GHSTUB'
#!/usr/bin/env bash
case "$1 $2" in
    "issue view")
        echo '{"state":"OPEN","labels":[{"name":"fleet:queued"}],"body":""}'
        exit 0
        ;;
    "api "*)
        label=""
        while [[ $# -gt 0 ]]; do
            case "$1" in
                -f) shift; case "$1" in labels\[\]=*) label="${1#labels[]=}" ;; esac ;;
            esac
            shift || true
        done
        if [[ -n "$label" ]]; then printf '[{"name":"%s"}]\n' "$label"; else echo '[]'; fi
        exit 0
        ;;
esac
# Everything else (incl. `gh pr list --json … --jq …` open-PR check) returns
# empty stdout — matching real gh+jq output for "no matching PRs".
exit 0
GHSTUB
chmod +x "$STUB_DIR/gh"
export PATH="$STUB_DIR:$PATH"

rel_engine() { "$FLEET_CLAIM" release "$1" >/dev/null 2>&1 || true; }
rel_game()   { "$FLEET_CLAIM" --repo game release "$1" >/dev/null 2>&1 || true; }

# --- T1: engine role accepts an engine task --------------------------------
echo "T1: engine role accepts engine task (no --repo)"
actual=0; FLEET_ROLE_REPO=engine "$FLEET_CLAIM" claim 2001 test-agent 2>/dev/null || actual=$?
assert_exit "$actual" 0 "engine role + engine task → exit 0"
rel_engine 2001

# --- T2: engine role rejects a game task -----------------------------------
echo "T2: engine role rejects game task (--repo game)"
actual=0; FLEET_ROLE_REPO=engine "$FLEET_CLAIM" --repo game claim 2001 test-agent 2>/dev/null || actual=$?
assert_exit "$actual" 1 "engine role + game task → exit 1"
rel_game 2001

# --- T3: game role accepts a game task -------------------------------------
echo "T3: game role accepts game task (--repo game)"
actual=0; FLEET_ROLE_REPO=game "$FLEET_CLAIM" --repo game claim 2001 test-agent 2>/dev/null || actual=$?
assert_exit "$actual" 0 "game role + game task → exit 0"
rel_game 2001

# --- T4: game role rejects an engine task ----------------------------------
echo "T4: game role rejects engine task (no --repo)"
actual=0; FLEET_ROLE_REPO=game "$FLEET_CLAIM" claim 2001 test-agent 2>/dev/null || actual=$?
assert_exit "$actual" 1 "game role + engine task → exit 1"
rel_engine 2001

# --- T5: FLEET_ROLE_REPO unset passes a game task (opt-in no-op) ------------
echo "T5: unset FLEET_ROLE_REPO passes game task"
actual=0; "$FLEET_CLAIM" --repo game claim 2001 test-agent 2>/dev/null || actual=$?
assert_exit "$actual" 0 "unset role + game task → exit 0"
rel_game 2001

# --- T6: FLEET_ROLE_REPO unset passes an engine task -----------------------
echo "T6: unset FLEET_ROLE_REPO passes engine task"
actual=0; "$FLEET_CLAIM" claim 2001 test-agent 2>/dev/null || actual=$?
assert_exit "$actual" 0 "unset role + engine task → exit 0"
rel_engine 2001

echo ""
echo "PASS: $PASS  FAIL: $FAIL"
[[ "$FAIL" -eq 0 ]]
