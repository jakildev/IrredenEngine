#!/usr/bin/env bash
# Tests for cmd_cleanup_gh's fifth pass (#2441): reaping flag-token-consumed
# claim labels. Before require_agent_token existed, a flag passed where
# <agent> belongs on review-/resolving-/amending-/steward- claim got stamped
# verbatim into a label, e.g. `fleet:reviewing-mac---role` from
# `review-claim <pr> --role <agent>`. Nothing swept these — the PR-label sweep
# (first pass) only looks at labels attached to OPEN PRs, and #226's stray
# `fleet:reviewing-mac---repo` sat on an ISSUE (steward-claim targets issues)
# for 8+ days. The fifth pass instead scans the repo's whole label CATALOG for
# the malformed <host>--- shape and `gh label delete`s it unconditionally —
# no TTL, no per-issue lookup — which also strips it off wherever it's
# attached (e.g. #226) in the same call.
#
# Hermetic per scripts/fleet/CLAUDE.md: no live GitHub. This test sources
# fleet-claim as a library (FLEET_CLAIM_LIB=1) and shadows `gh` with an
# in-process function so cmd_cleanup_gh's actual --jq filter expression runs
# against a canned label catalog through the real `jq` binary — a regex bug
# in the filter would be caught here, not just asserted away.

set -euo pipefail

SCRIPT_DIR=$(cd "$(dirname "$0")/.." && pwd)
FLEET_CLAIM="$SCRIPT_DIR/fleet-claim"

if [[ ! -x "$FLEET_CLAIM" ]]; then
    echo "test setup: fleet-claim not found at $FLEET_CLAIM" >&2
    exit 1
fi

if ! command -v jq >/dev/null 2>&1; then
    echo "test setup: jq not on PATH; skipping (this test exercises gh's real --jq filter)" >&2
    exit 0
fi

source "$(dirname "$0")/lib_assert.sh"

TMPROOT=""
cleanup() { [[ -n "$TMPROOT" && -d "$TMPROOT" ]] && rm -rf "$TMPROOT"; }
trap cleanup EXIT

TMPROOT=$(mktemp -d)
export FLEET_CLAIMS_DIR="$TMPROOT/claims"
export FLEET_RESERVATIONS_DIR="$TMPROOT/reservations"
export FLEET_STATE_DIR="$TMPROOT/state"
export FLEET_TEST_HOST="mac"
mkdir -p "$FLEET_CLAIMS_DIR" "$FLEET_RESERVATIONS_DIR" "$FLEET_STATE_DIR"

# Source fleet-claim as a library (defines cmd_cleanup_gh, skips dispatch).
set --
FLEET_CLAIM_LIB=1 source "$FLEET_CLAIM"

DELETE_LOG="$TMPROOT/delete.log"; : > "$DELETE_LOG"

# Canned catalog: three flag-token-consumed orphans (must be reaped) mixed
# with legitimate labels that share prefixes/hosts and superficially similar
# shapes (must survive) — including a real agent name that itself embeds the
# host twice ("mac-mac"), to pin that the filter keys on the '---' shape and
# not on any generic "looks weird" heuristic.
LABELS_JSON='[
  {"name":"fleet:reviewing-mac---role"},
  {"name":"fleet:reviewing-mac---repo"},
  {"name":"fleet:reviewing-mac---agent"},
  {"name":"fleet:reviewing-mac-opus-reviewer"},
  {"name":"fleet:reviewing-mac-mac-opus-reviewer"},
  {"name":"fleet:stewarding-mac-epic-steward"},
  {"name":"fleet:amending-linux-worker-1"},
  {"name":"fleet:resolving-windows-opus-worker-1"},
  {"name":"fleet:nit-of-pr"}
]'

gh() {
    case "${1:-}" in
        pr)
            [[ "${2:-}" == "list" ]] && { echo "[]"; return 0; }
            return 0
            ;;
        issue)
            case "${2:-}" in
                list) echo "[]"; return 0 ;;
                edit) printf '%s\n' "$*" >> "$DELETE_LOG"; return 0 ;;
                *) return 0 ;;
            esac
            ;;
        label)
            case "${2:-}" in
                list)
                    local jqexpr="" a prev=""
                    for a in "$@"; do
                        [[ "$prev" == "--jq" ]] && jqexpr="$a"
                        prev="$a"
                    done
                    echo "$LABELS_JSON" | jq -r "$jqexpr"
                    return 0
                    ;;
                delete)
                    printf '%s\n' "$*" >> "$DELETE_LOG"
                    return 0
                    ;;
                *) return 0 ;;
            esac
            ;;
        api) echo "[]"; return 0 ;;
        *) return 0 ;;
    esac
}

echo "== cleanup --gh fifth pass: reap flag-token-consumed catalog labels =="

out=$(cmd_cleanup_gh "jakildev/IrredenEngine" 2>&1)
log=$(cat "$DELETE_LOG")

for orphan in "fleet:reviewing-mac---role" "fleet:reviewing-mac---repo" "fleet:reviewing-mac---agent"; do
    assert_contains "$log" "label delete $orphan --repo jakildev/IrredenEngine --yes" \
        "$orphan reaped via gh label delete"
    assert_contains "$out" "reaped orphan claim label '$orphan'" \
        "$orphan reap reported in cleanup --gh output"
done

for legit in "fleet:reviewing-mac-opus-reviewer" "fleet:reviewing-mac-mac-opus-reviewer" \
             "fleet:stewarding-mac-epic-steward" "fleet:amending-linux-worker-1" \
             "fleet:resolving-windows-opus-worker-1" "fleet:nit-of-pr"; do
    assert_absent "$log" "label delete $legit " "$legit NOT reaped (legitimate label)"
done

assert_eq "$(printf '%s\n' "$log" | grep -c '^label delete ' || true)" "3" \
    "exactly three labels deleted (no over- or under-reap)"

summarize
