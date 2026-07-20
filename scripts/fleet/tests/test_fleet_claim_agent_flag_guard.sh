#!/usr/bin/env bash
# Tests for require_agent_token (#2441): the #2201 flag-token guard
# (`[[ "${1:-}" != --* ]]` in cmd_claim / cmd_planning_claim) only covered
# those two subcommands. The other eight agent-positional subcommands
# (review-/resolving-/amending-/steward- claim and release) took the dispatch
# case block's raw "$3" verbatim as the agent, so a flag passed where <agent>
# belongs (a caller mistake — most commonly a `--role`/`--agent`/`--repo`
# meant elsewhere) got silently stamped into a claim label, e.g.
# `fleet:reviewing-mac---role`. require_agent_token centralizes the guard and
# is now called at all eight of those dispatch sites, before any `gh` call —
# a rejected token acquires NO label.
#
# Hermetic per scripts/fleet/CLAUDE.md: no live GitHub. `gh` is stubbed to log
# every invocation so a rejected call can be asserted to have made none.

set -euo pipefail

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

assert_exit() {
    local actual_exit="$1" expected_exit="$2" msg="$3"
    if [[ "$actual_exit" -eq "$expected_exit" ]]; then
        ok "$msg"
    else
        bad "$msg"
        echo "        expected exit: $expected_exit"
        echo "        actual exit:   $actual_exit"
    fi
}

TMPROOT=$(mktemp -d)
export FLEET_CLAIMS_DIR="$TMPROOT/claims"
export FLEET_RESERVATIONS_DIR="$TMPROOT/reservations"
export FLEET_STATE_DIR="$TMPROOT/state"
export FLEET_TEST_HOST="mac"
mkdir -p "$FLEET_CLAIMS_DIR" "$FLEET_RESERVATIONS_DIR" "$FLEET_STATE_DIR"

GH_LOG="$TMPROOT/gh.log"
: > "$GH_LOG"

# Stub `gh` — logs every call, then handles just enough to let a WELL-FORMED
# claim/release round-trip succeed (positive control). `api ... labels`
# echoes the posted label back so _acquire_label_on wins; `issue edit` is a
# benign no-op (the release path).
STUB_DIR="$TMPROOT/bin"
mkdir -p "$STUB_DIR"
cat >"$STUB_DIR/gh" <<GHSTUB
#!/usr/bin/env bash
echo "\$*" >> "$GH_LOG"
case "\$1 \$2" in
    "api "*)
        label=""
        while [[ \$# -gt 0 ]]; do
            case "\$1" in
                -f) shift
                    case "\$1" in
                        labels\\[\\]=*) label="\${1#labels[]=}" ;;
                    esac
                    ;;
            esac
            shift || true
        done
        if [[ -n "\$label" ]]; then
            printf '[{"name":"%s"}]\n' "\$label"
        else
            echo '[]'
        fi
        exit 0
        ;;
    "issue edit"|"label "*|"repo view"|"pr list"|"issue list")
        exit 0
        ;;
esac
exit 0
GHSTUB
chmod +x "$STUB_DIR/gh"
export PATH="$STUB_DIR:$PATH"

gh_call_count() { wc -l < "$GH_LOG" | tr -d ' '; }

# ============================================================================
# A flag-shaped token in the <agent> position is refused, no label acquired
# ============================================================================

echo "== flag-shaped <agent> token: exit 2, zero gh calls =="

declare -a CLAIM_CMDS=(review-claim resolving-claim amending-claim steward-claim)
declare -a RELEASE_CMDS=(review-release resolving-release amending-release steward-release)

for cmd in "${CLAIM_CMDS[@]}" "${RELEASE_CMDS[@]}"; do
    # --role / --agent: the genuinely new coverage — require_agent_token is
    # the only guard that catches these. --repo: already hard-errored by the
    # pre-existing global Guard 2 (#2201) scan, which fires before dispatch
    # even reaches require_agent_token — checked here as a no-regression
    # pin, with its own (different) message.
    for badtok in "--role" "--agent"; do
        : > "$GH_LOG"
        actual=0
        err=$("$FLEET_CLAIM" "$cmd" 999 "$badtok" opus-reviewer 2>&1 1>/dev/null) || actual=$?
        assert_exit "$actual" 2 "$cmd 999 $badtok → exit 2"
        assert_eq "$(gh_call_count)" "0" "$cmd 999 $badtok → no gh calls (no label stamped)"
        assert_contains "$err" "must not look like a flag" "$cmd 999 $badtok → guard message present"
    done

    : > "$GH_LOG"
    actual=0
    err=$("$FLEET_CLAIM" "$cmd" 999 --repo opus-reviewer 2>&1 1>/dev/null) || actual=$?
    assert_exit "$actual" 2 "$cmd 999 --repo → exit 2 (pre-existing Guard 2)"
    assert_eq "$(gh_call_count)" "0" "$cmd 999 --repo → no gh calls"
    assert_contains "$err" "must precede the subcommand" "$cmd 999 --repo → Guard 2 message present"
done

echo "== empty <agent> token: exit 2, zero gh calls (no regression on the old -z check) =="

for cmd in "${CLAIM_CMDS[@]}" "${RELEASE_CMDS[@]}"; do
    : > "$GH_LOG"
    actual=0
    "$FLEET_CLAIM" "$cmd" 999 2>/dev/null || actual=$?
    assert_exit "$actual" 2 "$cmd 999 (missing agent) → exit 2"
    assert_eq "$(gh_call_count)" "0" "$cmd 999 (missing agent) → no gh calls"
done

# ============================================================================
# Positive control — a well-formed agent name still claims/releases cleanly
# ============================================================================

echo "== well-formed <agent> token still round-trips (positive control) =="

: > "$GH_LOG"
actual=0
out=$("$FLEET_CLAIM" review-claim 4242 opus-reviewer 2>&1) || actual=$?
assert_exit "$actual" 0 "review-claim 4242 opus-reviewer → exit 0"
assert_contains "$out" "acquired" "review-claim 4242 opus-reviewer → acquired"
assert_contains "$(cat "$GH_LOG")" "labels[]=fleet:reviewing-mac-opus-reviewer" \
    "review-claim 4242 opus-reviewer → posted the expected label, not a flag-mangled one"

: > "$GH_LOG"
actual=0
"$FLEET_CLAIM" review-release 4242 opus-reviewer >/dev/null 2>&1 || actual=$?
assert_exit "$actual" 0 "review-release 4242 opus-reviewer → exit 0"

summarize
