#!/usr/bin/env bash
# Regression test for #2754: fleet-claim's duplicate-open-PR guard
# (cmd_claim's `gh pr list --repo ... --state open --json number,headRefName,body`)
# ran with no `--limit` flag, so `gh pr list`'s own default of 30 silently
# truncated the response to the 30 newest open PRs. Any issue whose
# in-flight PR sat outside that window passed the guard as "no existing
# PR" and the claim proceeded, producing a duplicate claim + duplicate PR
# for work already in flight. The fix adds `--limit 300`, matching every
# sibling `gh pr list` call in this file.
#
# The `gh` stub below faithfully emulates gh's own truncation semantics
# (default 30, `--limit N` returns up to N) rather than special-casing the
# fixed call site — so this suite fails on the pre-fix code (no `--limit`
# arg -> stub truncates to 30 -> the duplicate at position 31 is invisible
# -> the claim wrongly succeeds) and passes once `--limit 300` ships.
#
# Hermetic per scripts/fleet/CLAUDE.md: no live GitHub, no live ~/.fleet.

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

cleanup() {
    [[ -n "$TMPROOT" && -d "$TMPROOT" ]] && rm -rf "$TMPROOT"
}
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

assert_dir() {
    local dir="$1" msg="$2"
    if [[ -d "$dir" ]]; then
        ok "$msg"
    else
        bad "$msg (dir missing: $dir)"
    fi
}

TMPROOT=$(mktemp -d)
export FLEET_CLAIMS_DIR="$TMPROOT/claims"
export FLEET_RESERVATIONS_DIR="$TMPROOT/reservations"
export FLEET_STATE_DIR="$TMPROOT/state"
mkdir -p "$FLEET_CLAIMS_DIR" "$FLEET_RESERVATIONS_DIR" "$FLEET_STATE_DIR"

# Stub `gh`. Fixtures: every issue OPEN. `pr list --state open` serves a
# 31-PR pool (#7001..#7030 filler, #7031 headRefName claude/9101-dup — the
# ONLY PR that matches issue #9101; nothing matches #9102) and truncates it
# exactly like real `gh pr list`: `--limit N` returns the first N, no
# `--limit` (or N<31) returns only the 30 filler PRs, hiding #7031.
STUB_DIR="$TMPROOT/bin"
mkdir -p "$STUB_DIR"

cat >"$STUB_DIR/gh" <<'GHSTUB'
#!/usr/bin/env bash
case "$1 $2" in
    "issue view")
        issue_num="$3"
        has_jq=0
        for a in "$@"; do
            [[ "$a" == "--jq" ]] && has_jq=1
        done
        if [[ "$has_jq" -eq 1 ]]; then
            echo "OPEN"
        else
            printf '%s' "{\"number\":$issue_num,\"state\":\"OPEN\",\"labels\":[],\"body\":\"\",\"title\":\"stub\"}"
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
    "pr list"|"issue list")
        if [[ "$*" == *"--state open"* ]]; then
            limit=30
            prev=""
            for a in "$@"; do
                if [[ "$prev" == "--limit" ]]; then
                    limit="$a"
                fi
                prev="$a"
            done
            filler=""
            for n in $(seq 7001 7030); do
                filler+="{\"number\":$n,\"headRefName\":\"claude/${n}-filler\",\"body\":\"\"},"
            done
            dup='{"number":7031,"headRefName":"claude/9101-dup","body":""}'
            if [[ "$limit" -ge 31 ]]; then
                printf '[%s%s]\n' "$filler" "$dup"
            else
                printf '[%s]\n' "${filler%,}"
            fi
            exit 0
        fi
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
}

# --- T1: issue #9101's own PR (#7031) sits outside the newest-30 window -----
# but the guard's `--limit 300` still surfaces it -> claim refused.
echo "T1: claim refused when the matching PR sits outside the default 30-item window"
err=$("$FLEET_CLAIM" claim 9101 test-agent 2>&1 1>/dev/null) && actual=0 || actual=$?
assert_exit "$actual" 1 "claim 9101 (own PR #7031 outside newest-30) -> exit 1"
assert_contains "$err" "already has open PR #7031" "duplicate-PR guard fires past the 30-item default limit"

# --- T2: guard against a stub that trivially always refuses -----------------
# no PR anywhere in the (same, full) 31-PR set matches #9102 -> claim succeeds.
echo "T2: claim succeeds when no PR anywhere in the set matches (no false positive)"
actual=0; "$FLEET_CLAIM" claim 9102 test-agent >/dev/null 2>&1 || actual=$?
assert_exit "$actual" 0 "claim 9102 (no matching PR in the 31-item set) -> exit 0"
assert_dir "$FLEET_CLAIMS_DIR/9102" "claim dir created for the legitimate claim"
release_quiet 9102

summarize "fleet-claim duplicate-PR --limit regression (#2754)"
