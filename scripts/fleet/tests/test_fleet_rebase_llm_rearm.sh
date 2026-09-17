#!/usr/bin/env bash
# Tests for fleet-rebase's LLM target predicate.
#
# The tier-0 pass re-arms the LLM merger after each run by writing one
# `merge:<repo>:<N>` line per PR that needs judgment into the trigger; the
# dispatcher launches the merger bound to one line at a time. It used to
# write a bare "llm" whenever ANY PR was left over — including every
# approved + MERGEABLE PR waiting on the human's merge click, and every
# cached `mergeable: UNKNOWN` row — so every scout tick that touched a PR
# label spawned tier-0, which re-armed the LLM, which walked both repos to
# report "0 candidates" (measured 2026-09-17: 39 LLM merger iterations in a
# day, none productive). The target predicate is role-merger.md step 3's own
# candidate rule: CONFLICTING, no skip label, and any fleet:merger-cooldown
# older than the cooldown; an UNKNOWN row is re-read over REST (at most two
# per run) and targeted only when it comes back CONFLICTING.
#
#   T1: approved + MERGEABLE + fleet:needs-human → human_remaining, no
#       re-arm, no trigger written.
#   T2: approved + MERGEABLE with only benign labels → human_remaining, no
#       re-arm, and no gh call for it (nothing mechanical is left to do).
#   T3: unapproved CONFLICTING, no skip labels → llm_remaining=1 and the
#       trigger reads `merge:engine:500`.
#   T4: CONFLICTING + fleet:merger-cooldown updated just now → cooling, no
#       re-arm.
#   T5: CONFLICTING + fleet:merger-cooldown updated 20 min ago → re-arms.
#   T6: UNKNOWN never counts on the cached value: unrefreshable → skipped,
#       no trigger; refreshed to CONFLICTING → a target; refreshed to
#       MERGEABLE → human; still null → skipped with a retry deadline;
#       merged → dropped; the refresh budget is two per run.
#   T7: MERGEABLE + fleet:merger-cooldown (no other skip label) →
#       stale-cooldown cleanup, no re-arm.
#   T8: CONFLICTING + fleet:wip → skip-labels, no re-arm.
#   T9: the 2026-09-09 slice shape (synthetic numbers) — five human-owned approved PRs plus
#       one semantic-conflict PR → llm_remaining=0, no re-arm.
#   T13: several targets are emitted in slice order; a record with no PR
#        number never becomes one (a missing head still does — the LLM pass
#        reads the PR live by number); a stale line is replaced, not
#        appended to.
#
# The gh stub records calls and answers only `api repos/<slug>/pulls/<N>
# --jq <the exact program fleet-rebase sends>` from a per-PR TSV fixture
# (any other call, or a missing fixture, exits 1); everything runs --dry-run
# so no git worktree is needed, and no verdict but T6's should cost a gh call.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
source "$(dirname "$0")/lib_preflight.sh"
REBASE="$SCRIPT_DIR/fleet-rebase"

if [[ ! -x "$REBASE" ]]; then
    echo "SKIP: fleet-rebase not executable at $REBASE" >&2
    exit 3
fi

source "$(dirname "$0")/lib_assert.sh"

TMPROOT=""
cleanup() { [[ -n "$TMPROOT" && -d "$TMPROOT" ]] && rm -rf "$TMPROOT"; }
trap cleanup EXIT

TMPROOT=$(mktemp -d)
export HOME="$TMPROOT"
export FLEET_STATE_DIR="$TMPROOT/.fleet/state"
export FLEET_REBASE_SCRATCH="$TMPROOT/.fleet/rebase-scratch"
export GH_STUB_LOG="$TMPROOT/gh-calls.log"
TRIGGER="$FLEET_STATE_DIR/triggers/merger"
mkdir -p "$FLEET_STATE_DIR/projections" "$TMPROOT/bin"

export GH_PULLS_FIXTURES="$TMPROOT/pulls"
mkdir -p "$GH_PULLS_FIXTURES"
cat > "$TMPROOT/bin/gh" <<'GHEOF'
#!/usr/bin/env bash
echo "$*" >> "$GH_STUB_LOG"
# Models exactly fleet-rebase's refresh_unknown read: `gh api
# repos/<owner>/<repo>/pulls/<N> --jq '<program>'`, answered from a TSV
# fixture <N>.tsv (state, merged, mergeable). Anything else fails closed.
if [[ "${1:-}" == api && "${2:-}" =~ ^repos/[^/]+/[^/]+/pulls/([0-9]+)$ && "${3:-}" == --jq ]]; then
    n="${BASH_REMATCH[1]}"
    [[ "${4:-}" == '[.state, (.merged|tostring), (.mergeable|tostring)] | @tsv' ]] || exit 1
    [[ -f "$GH_PULLS_FIXTURES/$n.tsv" ]] || exit 1
    cat "$GH_PULLS_FIXTURES/$n.tsv"
    exit 0
fi
exit 1
GHEOF
chmod +x "$TMPROOT/bin/gh"
export PATH="$TMPROOT/bin:$PATH"

write_slice() { printf '{"prs": %s}\n' "$1" > "$FLEET_STATE_DIR/projections/merger.json"; }
reset_stub() { rm -f "$GH_STUB_LOG" "$TRIGGER" "$GH_PULLS_FIXTURES"/*.tsv; touch "$GH_STUB_LOG"; }
# pull_fixture <N> <state> <merged> <mergeable> — what the stub serves for pulls/<N>.
pull_fixture() { printf '%s\t%s\t%s\n' "$2" "$3" "$4" > "$GH_PULLS_FIXTURES/$1.tsv"; }
# Every run passes --rearm-trigger so the trigger file is the observable.
run_rebase() { "$REBASE" --auto --dry-run --rearm-trigger 2>&1 || true; }
iso_ago() { python3 -c 'import datetime,sys;print((datetime.datetime.now(datetime.timezone.utc)-datetime.timedelta(seconds=int(sys.argv[1]))).strftime("%Y-%m-%dT%H:%M:%SZ"))' "$1"; }

assert_trigger_absent() {
    if [[ -e "$TRIGGER" ]]; then bad "$1 (trigger file exists: '$(cat "$TRIGGER")')"; else ok "$1"; fi
}
# assert_trigger_targets "<expected lines, newline-separated>" "<label>"
assert_trigger_targets() {
    if [[ -f "$TRIGGER" && "$(cat "$TRIGGER")" == "$1" ]]; then ok "$2"; else bad "$2 (trigger: '$(cat "$TRIGGER" 2>/dev/null | tr '\n' ' ' || echo MISSING)')"; fi
}
assert_trigger_llm() { assert_trigger_targets "$2" "$1"; }
dispatch_due() {
    if "$SCRIPT_DIR/fleet-dispatcher" --rearm-merger-due >/dev/null; then
        ok "dispatcher deadline hook succeeds"
    else
        bad "dispatcher deadline hook must succeed"
    fi
}

# === T1 ======================================================================
echo "T1: approved + MERGEABLE + disqualifying label -> human, no re-arm"
reset_stub
write_slice "[{\"repo\":\"game\",\"number\":901,\"headRefName\":\"claude/911-op1\",\"baseRefName\":\"master\",\"mergeable\":\"MERGEABLE\",\"updatedAt\":\"$(iso_ago 86400)\",\"labels\":[\"fleet:approved\",\"fleet:needs-human\"]}]"
T1=$(run_rebase)
assert_contains "$T1" "llm_remaining=0 human_remaining=1" "T1 counted as human_remaining"
assert_absent "$T1" "re-armed merger trigger" "T1 no re-arm logged"
assert_trigger_absent "T1 no trigger written"

# === T2 ======================================================================
echo "T2: approved + MERGEABLE, benign labels only -> human, no re-arm, no gh call"
reset_stub
write_slice "[{\"repo\":\"game\",\"number\":902,\"headRefName\":\"claude/912-coding-improvement\",\"baseRefName\":\"master\",\"mergeable\":\"MERGEABLE\",\"updatedAt\":\"$(iso_ago 86400)\",\"labels\":[\"fleet:approved\"]}]"
T2=$(run_rebase)
assert_contains "$T2" "llm_remaining=0 human_remaining=1" "T2 counted as human_remaining"
assert_absent "$(cat "$GH_STUB_LOG")" "902" "T2 a PR on the human's click costs no gh call"
assert_trigger_absent "T2 no trigger written"

# === T3 ======================================================================
echo "T3: unapproved CONFLICTING, no skip labels -> llm-other, re-arms"
reset_stub
write_slice "[{\"repo\":\"engine\",\"number\":500,\"headRefName\":\"claude/500-x\",\"baseRefName\":\"master\",\"mergeable\":\"CONFLICTING\",\"updatedAt\":\"$(iso_ago 3600)\",\"labels\":[]}]"
T3=$(run_rebase)
assert_contains "$T3" "llm_remaining=1 human_remaining=0" "T3 counted as llm_remaining"
assert_contains "$T3" "re-armed merger trigger for the LLM pass: merge:engine:500" "T3 re-arm logged with its target"
assert_trigger_llm "T3 trigger names the PR" "merge:engine:500"

# === T4 / T5 =================================================================
echo "T4: CONFLICTING + fresh fleet:merger-cooldown -> cooling, no re-arm"
reset_stub
write_slice "[{\"repo\":\"engine\",\"number\":501,\"headRefName\":\"claude/501-x\",\"baseRefName\":\"master\",\"mergeable\":\"CONFLICTING\",\"updatedAt\":\"$(iso_ago 30)\",\"labels\":[\"fleet:merger-cooldown\"]}]"
T4=$(run_rebase)
assert_contains "$T4" "not re-arming the LLM pass yet" "T4 cooling log line"
assert_contains "$T4" "llm_remaining=0 human_remaining=0 cooling=1" "T4 counted as cooling"
assert_trigger_absent "T4 no trigger written"

echo "T5: CONFLICTING + fleet:merger-cooldown older than the cooldown -> re-arms"
reset_stub
write_slice "[{\"repo\":\"engine\",\"number\":501,\"headRefName\":\"claude/501-x\",\"baseRefName\":\"master\",\"mergeable\":\"CONFLICTING\",\"updatedAt\":\"$(iso_ago 1200)\",\"labels\":[\"fleet:merger-cooldown\"]}]"
T5=$(run_rebase)
assert_contains "$T5" "llm_remaining=1" "T5 cooldown elapsed, counted as llm_remaining"
assert_trigger_llm "T5 trigger names the PR" "merge:engine:501"

# A shorter configured cooldown flips T4's verdict — the knob is live.
reset_stub
write_slice "[{\"repo\":\"engine\",\"number\":501,\"headRefName\":\"claude/501-x\",\"baseRefName\":\"master\",\"mergeable\":\"CONFLICTING\",\"updatedAt\":\"$(iso_ago 30)\",\"labels\":[\"fleet:merger-cooldown\"]}]"
T5b=$(FLEET_MERGER_COOLDOWN_SECONDS=10 run_rebase)
assert_contains "$T5b" "llm_remaining=1" "T5b FLEET_MERGER_COOLDOWN_SECONDS honored"

# === T6 ======================================================================
echo "T6: UNKNOWN is refreshed, then excluded — never counted on the cached value"
unknown_row() {  # $1 = number, $2 = updatedAt age (s)
    printf '{"repo":"engine","number":%s,"headRefName":"claude/%s-x","baseRefName":"master","mergeable":"UNKNOWN","updatedAt":"%s","labels":[]}' "$1" "$1" "$(iso_ago "$2")"
}
reset_stub
write_slice "[$(unknown_row 502 600)]"
T6a=$(run_rebase)
assert_contains "$T6a" "mergeable UNKNOWN; refresh failed — skipped, not counted" "T6a unrefreshable UNKNOWN is logged and skipped"
assert_contains "$T6a" "llm_remaining=0 human_remaining=0 cooling=0 unknown_skipped=1" "T6a stale UNKNOWN alone is not remaining"
assert_absent "$T6a" "re-armed merger trigger" "T6a no re-arm logged"
assert_trigger_absent "T6a no trigger written"
assert_contains "$(cat "$GH_STUB_LOG")" "api repos/jakildev/IrredenEngine/pulls/502 --jq" "T6a the row was re-read over REST"

reset_stub
write_slice "[$(unknown_row 502 600)]"
pull_fixture 502 open false false
T6b=$(run_rebase)
assert_contains "$T6b" "engine#502: mergeable UNKNOWN refreshed to CONFLICTING — LLM target" "T6b confirmed conflict is targeted"
assert_contains "$T6b" "llm_remaining=1 human_remaining=0 cooling=0 unknown_skipped=0" "T6b counted as llm_remaining"
assert_trigger_llm "T6b trigger names the PR" "merge:engine:502"

reset_stub
write_slice "[$(unknown_row 502 600)]"
pull_fixture 502 open false true
T6c=$(run_rebase)
assert_contains "$T6c" "refreshed to MERGEABLE — on the human's click" "T6c settled MERGEABLE is human work"
assert_contains "$T6c" "llm_remaining=0 human_remaining=1 cooling=0 unknown_skipped=0" "T6c counted as human_remaining"
assert_trigger_absent "T6c no trigger written"

reset_stub
write_slice "[$(unknown_row 502 60)]"
pull_fixture 502 open false null
T6d=$(run_rebase)
assert_contains "$T6d" "mergeable still UNKNOWN after refresh — skipped, not counted" "T6d still-computing row is skipped"
assert_contains "$T6d" "llm_remaining=0 human_remaining=0 cooling=0 unknown_skipped=1" "T6d not remaining"
assert_trigger_absent "T6d no trigger written"
deadline=$(cat "$FLEET_STATE_DIR/merger-retry-at" 2>/dev/null || true)
if [[ "$deadline" =~ ^[0-9]+$ ]] && (( deadline > $(date +%s) )); then
    ok "T6d a still-UNKNOWN row leaves tier-0 a retry deadline"
else
    bad "T6d a still-UNKNOWN row must leave tier-0 a retry deadline (got '${deadline:-none}')"
fi

reset_stub
write_slice "[$(unknown_row 3455 600)]"
pull_fixture 3455 closed true null
T6e=$(run_rebase)
assert_contains "$T6e" "engine#3455: cached UNKNOWN row is closed or merged — skipped" "T6e merged-while-UNKNOWN row is dropped"
assert_contains "$T6e" "llm_remaining=0" "T6e not remaining"
assert_trigger_absent "T6e no trigger written"

reset_stub
write_slice "[$(unknown_row 510 600), $(unknown_row 511 600), $(unknown_row 512 600)]"
pull_fixture 510 open false false
pull_fixture 511 open false false
pull_fixture 512 open false false
T6f=$(run_rebase)
assert_contains "$T6f" "engine#512: mergeable UNKNOWN; refresh budget spent — skipped, not counted" "T6f the third UNKNOWN row is over budget"
assert_contains "$T6f" "llm_remaining=2 human_remaining=0 cooling=0 unknown_skipped=1" "T6f two refreshed, one deferred"
assert_trigger_llm "T6f trigger names the two confirmed PRs" $'merge:engine:510\nmerge:engine:511'
assert_absent "$(cat "$GH_STUB_LOG")" "pulls/512" "T6f the over-budget row cost no gh call"
T6g=$(FLEET_REBASE_UNKNOWN_REFRESH_BUDGET=3 run_rebase)
assert_contains "$T6g" "llm_remaining=3" "T6g FLEET_REBASE_UNKNOWN_REFRESH_BUDGET honored"

# === T7 ======================================================================
echo "T7: MERGEABLE + fleet:merger-cooldown -> stale-cooldown cleanup, no re-arm"
reset_stub
write_slice "[{\"repo\":\"engine\",\"number\":503,\"headRefName\":\"claude/503-x\",\"baseRefName\":\"master\",\"mergeable\":\"MERGEABLE\",\"updatedAt\":\"$(iso_ago 60)\",\"labels\":[\"fleet:merger-cooldown\"]}]"
T7=$(run_rebase)
assert_contains "$T7" "stale fleet:merger-cooldown on MERGEABLE PR — would remove (dry-run)" "T7 cleanup fires"
assert_contains "$T7" "llm_remaining=0 human_remaining=0" "T7 cleanup counts nowhere"
assert_trigger_absent "T7 no trigger written"

# === T8 ======================================================================
echo "T8: CONFLICTING + fleet:wip -> skip-labels, no re-arm"
reset_stub
write_slice "[{\"repo\":\"game\",\"number\":903,\"headRefName\":\"claude/903-x\",\"baseRefName\":\"master\",\"mergeable\":\"CONFLICTING\",\"updatedAt\":\"$(iso_ago 3600)\",\"labels\":[\"fleet:changes-made\",\"fleet:wip\"]}]"
T8=$(run_rebase)
assert_contains "$T8" "llm_remaining=0 human_remaining=0" "T8 skip label keeps it out of every tally"
assert_trigger_absent "T8 no trigger written"

# === T9 ======================================================================
echo "T9: the 2026-09-09 slice shape (synthetic numbers) -> nothing re-arms"
reset_stub
old=$(iso_ago 86400)
write_slice "[
  {\"repo\":\"engine\",\"number\":3081,\"headRefName\":\"claude/2917-x\",\"baseRefName\":\"master\",\"mergeable\":\"CONFLICTING\",\"updatedAt\":\"$old\",\"labels\":[\"fleet:resolving-mac-pool-3\",\"fleet:semantic-conflict\"]},
  {\"repo\":\"game\",\"number\":901,\"headRefName\":\"claude/911-x\",\"baseRefName\":\"master\",\"mergeable\":\"MERGEABLE\",\"updatedAt\":\"$old\",\"labels\":[\"fleet:approved\",\"fleet:needs-human\"]},
  {\"repo\":\"game\",\"number\":902,\"headRefName\":\"claude/912-x\",\"baseRefName\":\"master\",\"mergeable\":\"MERGEABLE\",\"updatedAt\":\"$old\",\"labels\":[\"fleet:approved\"]},
  {\"repo\":\"game\",\"number\":903,\"headRefName\":\"claude/903-x\",\"baseRefName\":\"master\",\"mergeable\":\"CONFLICTING\",\"updatedAt\":\"$old\",\"labels\":[\"fleet:changes-made\",\"fleet:wip\"]},
  {\"repo\":\"game\",\"number\":904,\"headRefName\":\"claude/913-x\",\"baseRefName\":\"master\",\"mergeable\":\"MERGEABLE\",\"updatedAt\":\"$old\",\"labels\":[\"fleet:approved\"]},
  {\"repo\":\"game\",\"number\":905,\"headRefName\":\"claude/914-x\",\"baseRefName\":\"master\",\"mergeable\":\"MERGEABLE\",\"updatedAt\":\"$old\",\"labels\":[\"fleet:approved\"]},
  {\"repo\":\"game\",\"number\":907,\"headRefName\":\"claude/915-x\",\"baseRefName\":\"master\",\"mergeable\":\"MERGEABLE\",\"updatedAt\":\"$old\",\"labels\":[\"fleet:design-proposed\",\"fleet:wip\"]}
]"
T9=$(run_rebase)
assert_contains "$T9" "llm_remaining=0 human_remaining=4 cooling=0" "T9 four human-owned PRs, zero LLM work"
assert_absent "$T9" "re-armed merger trigger" "T9 no re-arm logged"
assert_trigger_absent "T9 no trigger written"

# === T10 =====================================================================
echo "T10: UNSTABLE / BEHIND are not LLM candidates (role step 3 admits only CONFLICTING + stale UNKNOWN)"
for st in UNSTABLE BEHIND; do
    reset_stub
    write_slice "[{\"repo\":\"engine\",\"number\":504,\"headRefName\":\"claude/504-x\",\"baseRefName\":\"master\",\"mergeable\":\"$st\",\"updatedAt\":\"$(iso_ago 3600)\",\"labels\":[]}]"
    T10=$(run_rebase)
    assert_contains "$T10" "llm_remaining=0 human_remaining=1" "T10 $st is not counted as llm_remaining"
    assert_trigger_absent "T10 $st writes no trigger"
done

echo "T11: deferred cooldown gets a one-shot dispatcher wakeup"
reset_stub
write_slice "[{\"repo\":\"engine\",\"number\":505,\"headRefName\":\"claude/505-x\",\"baseRefName\":\"master\",\"mergeable\":\"CONFLICTING\",\"updatedAt\":\"$(iso_ago 30)\",\"labels\":[\"fleet:merger-cooldown\"]}]"
run_rebase >/dev/null
deadline=$(cat "$FLEET_STATE_DIR/merger-retry-at" 2>/dev/null || true)
if [[ "$deadline" =~ ^[0-9]+$ ]] && (( deadline > $(date +%s) )); then
    ok "cooldown writes a future eligibility deadline"
else
    bad "cooldown must write a future eligibility deadline"
fi
dispatch_due
assert_trigger_absent "future deadline does not wake the merger early"
printf '1\n' > "$FLEET_STATE_DIR/merger-retry-at"
dispatch_due
if [[ -f "$TRIGGER" && ! -s "$TRIGGER" && ! -f "$FLEET_STATE_DIR/merger-retry-at" ]]; then
    ok "elapsed deadline creates one empty tier-0 trigger and consumes the timer"
else
    bad "elapsed deadline must create one empty tier-0 trigger and consume the timer"
fi
printf 'merge:engine:505\n' > "$TRIGGER"
printf '1\n' > "$FLEET_STATE_DIR/merger-retry-at"
dispatch_due
assert_trigger_llm "deadline preserves already pending target lines" "merge:engine:505"
reset_stub
write_slice '[]'
printf '1\n' > "$FLEET_STATE_DIR/merger-retry-at"
run_rebase >/dev/null
if [[ ! -f "$FLEET_STATE_DIR/merger-retry-at" ]]; then
    ok "a new empty projection cancels an obsolete deadline"
else
    bad "a new empty projection must cancel an obsolete deadline"
fi

echo "T12: deadline publication during consumption survives"
export FLEET_TEST_REAL_CAT
FLEET_TEST_REAL_CAT=$(command -v cat)
cat > "$TMPROOT/bin/cat" <<'CATSTUB'
#!/usr/bin/env bash
case "${1:-}" in
    */.merger-deadline.*|*/merger-retry-at)
        value=$("$FLEET_TEST_REAL_CAT" "$@")
        printf '4102444800\n' > "$FLEET_STATE_DIR/merger-retry-at"
        printf '%s\n' "$value"
        exit 0
        ;;
esac
exec "$FLEET_TEST_REAL_CAT" "$@"
CATSTUB
chmod +x "$TMPROOT/bin/cat"
printf '1\n' > "$FLEET_STATE_DIR/merger-retry-at"
dispatch_due
assert_eq "$("$FLEET_TEST_REAL_CAT" "$FLEET_STATE_DIR/merger-retry-at" 2>/dev/null || true)" \
    "4102444800" "a concurrent replacement is not deleted by the dispatcher"

# === T13 =====================================================================
echo "T13: targets are emitted in slice order, a numberless record never, and a stale line is replaced"
rm -f "$TMPROOT/bin/cat"
reset_stub
printf 'merge:engine:999\n' > "$TRIGGER"
old=$(iso_ago 86400)
write_slice "[
  {\"repo\":\"engine\",\"number\":520,\"headRefName\":\"claude/520-x\",\"baseRefName\":\"master\",\"mergeable\":\"CONFLICTING\",\"updatedAt\":\"$old\",\"labels\":[]},
  {\"repo\":\"engine\",\"number\":521,\"headRefName\":\"\",\"baseRefName\":\"master\",\"mergeable\":\"CONFLICTING\",\"updatedAt\":\"$old\",\"labels\":[]},
  {\"repo\":\"engine\",\"headRefName\":\"claude/no-number\",\"baseRefName\":\"master\",\"mergeable\":\"CONFLICTING\",\"updatedAt\":\"$old\",\"labels\":[]},
  {\"repo\":\"game\",\"number\":77,\"headRefName\":\"claude/77-x\",\"baseRefName\":\"master\",\"mergeable\":\"CONFLICTING\",\"updatedAt\":\"$old\",\"labels\":[]}
]"
T13=$(run_rebase)
assert_contains "$T13" "llm_remaining=4" "T13 every LLM-rule row counts (the malformed ones included)"
assert_trigger_targets $'merge:engine:520\nmerge:engine:521\nmerge:game:77' "T13 trigger carries every numbered target in slice order and nothing stale"

summarize "fleet-rebase LLM target predicate tests"
