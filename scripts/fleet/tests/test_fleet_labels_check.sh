#!/usr/bin/env bash
# Tests for `fleet-labels --check` — the drift guard that keeps the label
# catalog in scripts/fleet/fleet-labels 1:1 with the node set in
# docs/agents/fleet-state-machine.json.
#
# Why this suite exists (#3205): the guard was green-by-inspection only. No
# suite ran it, so the catalog drifted four labels ahead of the JSON
# (fleet:author-* / fleet:runtime-*) and `--check` sat red on master until a
# reviewer happened to run it by hand. A guard nothing executes is not a gate.
#
# The first arm reads this checkout's own catalog and state machine rather
# than a fixture, because that is the regression the issue asks for: the next
# catalog addition that forgets the JSON has to fail a suite, not a manual run.
# That is still within scripts/fleet/CLAUDE.md's hermeticity bar — no live
# GitHub, no live ~/.fleet, no network — it just declines to stub the two
# tracked files whose agreement IS the subject under test. The remaining arms
# are the controls that keep it from being vacuous: each drives `--check` at a
# mutated copy of one input and asserts it goes red for the right reason.
#
# Covers:
#   - the live tree passes, and passes against THIS worktree's state machine
#   - the four #3205 labels are nodes and are documented
#   - control: a label in the catalog with no node → exit 1, names the label
#   - control: a node with no catalog entry → exit 1, names the node
#   - control: a catalog description over 100 chars → exit 1, names the entry

set -euo pipefail

SCRIPT_DIR=$(cd "$(dirname "$0")/.." && pwd)
source "$(dirname "$0")/lib_assert.sh"

FLEET_LABELS="$SCRIPT_DIR/fleet-labels"
REPO_ROOT=$(cd "$SCRIPT_DIR/../.." && pwd)
STATE_MACHINE="$REPO_ROOT/docs/agents/fleet-state-machine.json"
LABELS_REFERENCE="$REPO_ROOT/docs/agents/fleet-labels-reference.md"

if [[ ! -f "$FLEET_LABELS" ]]; then
    echo "test setup: fleet-labels not found at $FLEET_LABELS" >&2
    exit 1
fi

# `--check` is a jq consumer and exits 2 without it; and a scripts/fleet staged
# outside its repo has no docs/ beside it. Neither is a drift failure, so skip
# rather than score them — an unrunnable subject must not read as a red guard
# (nor, per run_all.sh, as a green one).
if ! command -v jq >/dev/null 2>&1; then
    echo "SKIP: jq not found — fleet-labels --check cannot run" >&2
    exit 3
fi
if [[ ! -f "$STATE_MACHINE" ]]; then
    echo "SKIP: no state machine at $STATE_MACHINE — scripts/fleet is staged outside its repo" >&2
    exit 3
fi

TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT

# Run --check and report "<rc>" plus merged output. --check writes its OK line
# to stdout and every diagnostic to stderr, so both halves have to be captured
# or the failure arms assert against nothing.
run_check() {  # run_check <script> [env assignments via caller]
    local out rc
    set +e
    out=$("$@" 2>&1)
    rc=$?
    set -e
    CHECK_RC="$rc"
    CHECK_OUT="$out"
}

# --- T1: the live tree is green -------------------------------------------
# This is the regression arm #3205 asks for. It runs the branch's own script,
# which resolves the state machine from its own location — so it measures this
# worktree even when a ~/bin/fleet-labels symlink into the main clone is on
# PATH (invoking the wrapper by name reports on master, not on the branch,
# which is how the drift stayed invisible through a review).
echo "T1: fleet-labels --check is green over this checkout"
run_check bash "$FLEET_LABELS" --check
assert_eq "$CHECK_RC" "0" "--check exits 0 over the real catalog + state machine"
assert_contains "$CHECK_OUT" "--check: OK" "the OK line is printed"
assert_contains "$CHECK_OUT" "$STATE_MACHINE" \
    "the check read THIS worktree's state machine, not another clone's"

# --- T2: the #3205 labels are nodes and are documented ---------------------
# T1 subsumes the node half, but only as part of a 60-label aggregate; naming
# the four keeps the failure legible if they are ever dropped again.
echo "T2: the four author-*/runtime-* labels are nodes and documented"
NODE_NAMES=$(jq -r '.labels[].name' "$STATE_MACHINE")
for label in fleet:author-claude fleet:author-codex fleet:runtime-claude fleet:runtime-codex; do
    assert_contains "$NODE_NAMES" "$label" "$label is a state-machine node"
    assert_contains "$(cat "$LABELS_REFERENCE")" "$label" \
        "$label is documented in fleet-labels-reference.md"
done

# --- T3: control — a catalog label with no node ----------------------------
# The exact shape of the #3205 defect: the catalog gains a label, the JSON
# does not. Drop a node instead of adding a catalog entry so the control drives
# the real script rather than a copy of it.
echo "T3: control — catalog label with no node is caught"
jq 'del(.labels[] | select(.name == "fleet:author-claude"))' \
    "$STATE_MACHINE" > "$TMP/missing-node.json"
run_check env FLEET_STATE_MACHINE="$TMP/missing-node.json" bash "$FLEET_LABELS" --check
assert_eq "$CHECK_RC" "1" "a missing node exits 1"
assert_contains "$CHECK_OUT" "In fleet-labels but NOT in fleet-state-machine.json" \
    "the drift is reported in the catalog-side direction"
assert_contains "$CHECK_OUT" "fleet:author-claude" "the offending label is named"

# --- T4: control — a node with no catalog entry ----------------------------
echo "T4: control — node with no catalog entry is caught"
jq '.labels += [{"name": "fleet:control-only-not-in-catalog", "scope": "pr",
                 "color": "ededed", "description": "control fixture"}]' \
    "$STATE_MACHINE" > "$TMP/extra-node.json"
run_check env FLEET_STATE_MACHINE="$TMP/extra-node.json" bash "$FLEET_LABELS" --check
assert_eq "$CHECK_RC" "1" "an extra node exits 1"
assert_contains "$CHECK_OUT" "In fleet-state-machine.json but NOT in fleet-labels" \
    "the drift is reported in the node-side direction"
assert_contains "$CHECK_OUT" "fleet:control-only-not-in-catalog" "the offending node is named"

# --- T5: control — an over-long catalog description ------------------------
# The other half of what --check gates: GitHub 422s a description over 100
# chars, so the sync loop truncates it and the catalog text silently diverges
# from what is on the repo. Mutating a description leaves every label NAME
# alone, so this arm isolates the length guard from the drift guard.
echo "T5: control — catalog description over 100 chars is caught"
PAD=$(printf 'x%.0s' $(seq 1 120))
awk -v pad="$PAD" '
    !patched && $0 ~ /^[[:space:]]*"fleet:/ && $0 ~ /"$/ {
        if (split($0, parts, "|") >= 3) { sub(/"$/, pad "\"", $0); patched = 1 }
    }
    { print }
' "$FLEET_LABELS" > "$TMP/fleet-labels-overlong"
run_check env FLEET_STATE_MACHINE="$STATE_MACHINE" bash "$TMP/fleet-labels-overlong" --check
assert_eq "$CHECK_RC" "1" "an over-long description exits 1"
assert_contains "$CHECK_OUT" "over GitHub's 100-char limit" "the length guard is what fired"
assert_absent "$CHECK_OUT" "DRIFT between the catalog" \
    "no name drift is reported — the arm isolates the length guard"

summarize "fleet-labels --check tests"
