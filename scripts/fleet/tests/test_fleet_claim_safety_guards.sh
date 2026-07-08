#!/usr/bin/env bash
# Tests for fleet-claim's two #2201 safety guards:
#
#   Guard 1 — cmd_claim / cmd_planning_claim refuse a CLOSED issue.
#     A stale scout cache can list a closed issue in tasks.open[] /
#     needs_plan[]; granting a lock strands a fleet:claim-* label on it
#     (release also retains it). _refuse_if_closed refuses an explicit CLOSED
#     and soft-degrades on any gh lookup miss.
#
#   Guard 2 — a trailing `--repo` token (after the subcommand) is a hard
#     error. `--repo` is a GLOBAL flag parsed before the subcommand; placed
#     after it, it was silently ignored so the wrong repo's same-numbered
#     issue was claimed/released. cleanup/reconcile are the sole exemption
#     (they take their own `--repo <owner/repo>` scoped after their name).
#
# Hermetic per scripts/fleet/CLAUDE.md: no live GitHub, no live ~/.fleet.
# `gh` is stubbed at a fail-closed seam; claims/reservations/state land in a
# tempdir via the FLEET_*_DIR overrides.

set -euo pipefail

# cmd_claim runs against the real (possibly-stale) main clone but these guards
# don't care about clone freshness — disable the #1810 freshness gate so a
# stale local clone can't mask a guard's exit code.
export FLEET_SKIP_CLONE_FRESHNESS=1

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

assert_no_dir() {
    local dir="$1" msg="$2"
    if [[ ! -d "$dir" ]]; then
        PASS=$((PASS + 1))
        echo "  ok: $msg"
    else
        FAIL=$((FAIL + 1))
        echo "  FAIL: $msg (dir exists: $dir)"
    fi
}

assert_dir() {
    local dir="$1" msg="$2"
    if [[ -d "$dir" ]]; then
        PASS=$((PASS + 1))
        echo "  ok: $msg"
    else
        FAIL=$((FAIL + 1))
        echo "  FAIL: $msg (dir missing: $dir)"
    fi
}

assert_contains() {
    local haystack="$1" needle="$2" msg="$3"
    if [[ "$haystack" == *"$needle"* ]]; then
        PASS=$((PASS + 1))
        echo "  ok: $msg"
    else
        FAIL=$((FAIL + 1))
        echo "  FAIL: $msg"
        echo "        expected to contain: $needle"
        echo "        actual:              $haystack"
    fi
}

assert_not_contains() {
    local haystack="$1" needle="$2" msg="$3"
    if [[ "$haystack" != *"$needle"* ]]; then
        PASS=$((PASS + 1))
        echo "  ok: $msg"
    else
        FAIL=$((FAIL + 1))
        echo "  FAIL: $msg"
        echo "        expected NOT to contain: $needle"
        echo "        actual:                  $haystack"
    fi
}

TMPROOT=$(mktemp -d)
export FLEET_CLAIMS_DIR="$TMPROOT/claims"
export FLEET_RESERVATIONS_DIR="$TMPROOT/reservations"
export FLEET_STATE_DIR="$TMPROOT/state"
mkdir -p "$FLEET_CLAIMS_DIR" "$FLEET_RESERVATIONS_DIR" "$FLEET_STATE_DIR"

# Stub `gh` so the guards + full claim path resolve against canned fixtures
# instead of GitHub. Fixtures: #5001 CLOSED, everything else OPEN.
#   gh issue view <N> ... --jq .state  → bare state string (the _refuse_if_closed
#                                        seam is the only --jq caller in scope)
#   gh issue view <N> ... --json ...   → full JSON (the model/host/blocker gates
#                                        parse this themselves, no --jq)
#   gh api .../labels ... -f labels[]=X → echo X back so _acquire_label_on wins
#   gh issue edit / pr list / issue list → benign no-op
#   gh pr view                          → fail (exercised only by the
#                                          --stackable-on no-false-positive case,
#                                          which cmd_claim handles gracefully)
STUB_DIR="$TMPROOT/bin"
mkdir -p "$STUB_DIR"

cat >"$STUB_DIR/gh" <<'GHSTUB'
#!/usr/bin/env bash
state_for() {
    case "$1" in
        5001) echo "CLOSED" ;;
        *)    echo "OPEN" ;;
    esac
}
case "$1 $2" in
    "issue view")
        issue_num="$3"
        has_jq=0
        for a in "$@"; do
            [[ "$a" == "--jq" ]] && has_jq=1
        done
        st=$(state_for "$issue_num")
        if [[ "$has_jq" -eq 1 ]]; then
            # _refuse_if_closed's `--json state --jq .state` → bare state.
            echo "$st"
        else
            printf '%s' "{\"number\":$issue_num,\"state\":\"$st\",\"labels\":[],\"body\":\"\",\"title\":\"stub\"}"
        fi
        exit 0
        ;;
    "api "*)
        label=""
        while [[ $# -gt 0 ]]; do
            case "$1" in
                -f) shift
                    case "$1" in
                        labels\[\]=*) label="${1#labels[]=}" ;;
                    esac
                    ;;
            esac
            shift || true
        done
        if [[ -n "$label" ]]; then
            printf '[{"name":"%s"}]\n' "$label"
        else
            echo '[]'
        fi
        exit 0
        ;;
    "pr view")
        # Only the --stackable-on no-false-positive test reaches this; make it
        # fail so cmd_claim bails cleanly (we assert on the guard, not success).
        exit 1
        ;;
    "pr list"|"issue list")
        echo '[]'
        exit 0
        ;;
    "issue edit"|"label "*|"repo view")
        exit 0
        ;;
esac
exit 0
GHSTUB
chmod +x "$STUB_DIR/gh"

export PATH="$STUB_DIR:$PATH"

release_quiet() {
    "$FLEET_CLAIM" release "$1" >/dev/null 2>&1 || true
    "$FLEET_CLAIM" --repo game release "$1" >/dev/null 2>&1 || true
}

# ============================================================================
# Guard 1 — CLOSED-issue refusal
# ============================================================================

# --- T1: claim on a CLOSED issue → exit 1, no claim dir ---------------------
echo "T1: claim on a CLOSED issue is refused"
actual=0; "$FLEET_CLAIM" claim 5001 test-agent 2>/dev/null || actual=$?
assert_exit "$actual" 1 "claim CLOSED #5001 → exit 1"
assert_no_dir "$FLEET_CLAIMS_DIR/5001" "no claim dir created for CLOSED #5001"

# --- T2: planning-claim on a CLOSED issue → exit non-zero, no lock ----------
echo "T2: planning-claim on a CLOSED issue is refused"
actual=0; "$FLEET_CLAIM" planning-claim 5001 test-agent 2>/dev/null || actual=$?
assert_exit "$actual" 1 "planning-claim CLOSED #5001 → exit 1"

# --- T3: claim on an OPEN issue still succeeds (Guard 1 no-regression) -------
echo "T3: claim on an OPEN issue still succeeds"
actual=0; "$FLEET_CLAIM" claim 5002 test-agent 2>/dev/null || actual=$?
assert_exit "$actual" 0 "claim OPEN #5002 → exit 0"
assert_dir "$FLEET_CLAIMS_DIR/5002" "claim dir created for OPEN #5002"
release_quiet 5002

# ============================================================================
# Guard 2 — trailing --repo hard error
# ============================================================================

# --- T4: claim with a TRAILING --repo → exit 2, no claim dir ----------------
echo "T4: claim with a trailing --repo is rejected"
err=$("$FLEET_CLAIM" claim 5002 test-agent --repo game 2>&1 1>/dev/null) && actual=0 || actual=$?
assert_exit "$actual" 2 "claim 5002 test-agent --repo game → exit 2"
assert_contains "$err" "must precede the subcommand" "trailing --repo prints the precede-hint"
assert_no_dir "$FLEET_CLAIMS_DIR/5002" "no engine claim dir from trailing --repo claim"
assert_no_dir "$FLEET_CLAIMS_DIR/game-5002" "no game claim dir from trailing --repo claim"

# --- T5: release with a TRAILING --repo → exit 2 (was silent exit 0) --------
echo "T5: release with a trailing --repo is rejected"
err=$("$FLEET_CLAIM" release 5002 --repo game 2>&1 1>/dev/null) && actual=0 || actual=$?
assert_exit "$actual" 2 "release 5002 --repo game → exit 2 (was silent 0)"
assert_contains "$err" "must precede the subcommand" "trailing --repo on release prints the precede-hint"

# --- T6: trailing --repo=<ns> equals form is also rejected ------------------
echo "T6: trailing --repo=<ns> equals form is rejected"
actual=0; "$FLEET_CLAIM" release 5002 --repo=game >/dev/null 2>&1 || actual=$?
assert_exit "$actual" 2 "release 5002 --repo=game → exit 2"

# --- T7: well-formed LEADING global --repo still works (no over-fire) --------
echo "T7: leading global --repo game claim still succeeds"
actual=0; "$FLEET_CLAIM" --repo game claim 5002 test-agent >/dev/null 2>&1 || actual=$?
assert_exit "$actual" 0 "--repo game claim 5002 → exit 0"
assert_dir "$FLEET_CLAIMS_DIR/game-5002" "game claim dir created for leading --repo"
release_quiet 5002

# --- T8: --stackable-on <url> is NOT misflagged by Guard 2 ------------------
echo "T8: --stackable-on <url> is not misflagged as a trailing --repo"
err=$("$FLEET_CLAIM" claim 5002 test-agent \
        --stackable-on https://github.com/jakildev/IrredenEngine/pull/123 2>&1 1>/dev/null) || true
assert_not_contains "$err" "must precede the subcommand" "stackable-on URL doesn't trip the --repo scan"

# --- T9: cleanup keeps its own trailing --repo (exemption) ------------------
echo "T9: cleanup exemption — trailing --repo is not hard-errored"
err=$("$FLEET_CLAIM" cleanup --repo jakildev/IrredenEngine 2>&1 1>/dev/null) && actual=0 || actual=$?
assert_exit "$actual" 0 "cleanup --repo <owner/repo> (empty claims) → exit 0"
assert_not_contains "$err" "must precede the subcommand" "cleanup's trailing --repo is exempt"

# --- T10: reconcile keeps its own trailing --repo (exemption) ---------------
# reconcile parses its args before any gh/state access, so a trailing --repo
# followed by a bogus flag reaches reconcile's OWN parser (its "unknown arg"),
# proving Guard 2 did not intercept the --repo.
echo "T10: reconcile exemption — trailing --repo reaches reconcile's own parser"
err=$("$FLEET_CLAIM" reconcile --repo jakildev/IrredenEngine --bogus-flag 2>&1 1>/dev/null) && actual=0 || actual=$?
assert_exit "$actual" 2 "reconcile --repo <owner/repo> --bogus-flag → exit 2 (its own error)"
assert_contains "$err" "reconcile: unknown arg" "reconcile's own parser saw the trailing --repo"
assert_not_contains "$err" "must precede the subcommand" "reconcile's trailing --repo is exempt from Guard 2"

echo ""
echo "PASS: $PASS  FAIL: $FAIL"
[[ "$FAIL" -eq 0 ]]
