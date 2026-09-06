#!/usr/bin/env bash
# Tests for the planning claim class (#1932 PR3, re-scope of #1889):
# planning-claim / planning-release on a fleet:needs-plan ISSUE via the
# fleet:planning-<host>-<agent> label, the `## Plan`-comment dedup early-out
# (return 3 when planning already happened), and the cleanup --gh fourth pass
# that sweeps stale planning labels off open fleet:needs-plan issues after
# FLEET_CLAIM_STALE_SECS_PLANNING.
#
# The claim path reuses _acquire_label_on / _claim_decision (exhaustively
# covered by test_fleet_claim_acquire.sh) and mirrors steward-claim — here we
# pin the planning-specific wiring: the prefix, the issue wording, the dedup
# early-out, and the TTL sweep (separate pass over open fleet:needs-plan issues).

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
cleanup() { [[ -n "$TMPROOT" && -d "$TMPROOT" ]] && rm -rf "$TMPROOT"; }
trap cleanup EXIT
ok()  { PASS=$((PASS + 1)); echo "  ok: $1"; }
bad() { FAIL=$((FAIL + 1)); echo "  FAIL: $1"; }
assert_exit() {
    local actual="$1" expected="$2" msg="$3"
    if [[ "$actual" -eq "$expected" ]]; then ok "$msg"; else bad "$msg (expected exit $expected, got $actual)"; fi
}

TMPROOT=$(mktemp -d)
export FLEET_ORPHANS_DIR="$TMPROOT/orphans"
# Hermetic claims dir: the fourth pass consults $CLAIMS_DIR for the planning
# liveness marker, so an unset FLEET_CLAIMS_DIR would read the host's real
# ~/.fleet/claims and make the sweep asserts depend on live fleet state.
export FLEET_CLAIMS_DIR="$TMPROOT/claims"
mkdir -p "$FLEET_CLAIMS_DIR"
export FLEET_TEST_HOST="mac"
export FLEET_CLAIM_NO_SLEEP=1
export FLEET_CLAIM_ACQUIRE_RETRIES=2

# Source fleet-claim as a library (defines helpers, skips the dispatch).
set --
FLEET_CLAIM_LIB=1 source "$FLEET_CLAIM"

P="fleet:planning-mac-"
MINE="${P}worker"
OTHER="fleet:planning-linux-worker"
REMOVE_LOG="$TMPROOT/remove.log"; : > "$REMOVE_LOG"

# Stateful gh stub. STUB_HOLDERS = planning labels already on the issue; the
# POST echoes those + the just-posted label. STUB_PLAN_COMMENTS is the count
# `_issue_has_plan_comment`'s --jq returns (the dedup probe). STUB_LABELS is the
# issue's label set, which the `--json labels` arm answers *by evaluating the
# --jq program against* rather than by returning a canned boolean. The two
# issue-view probes are disambiguated by their --json arg. issue-edit calls are
# logged for self-removal / release.
#
# Three gates now share the `--json labels` call shape (--replan's
# fleet:needs-plan guard, the first-plan stale-candidate guard, and the
# fleet:needs-human park — #3034), so the old single STUB_NEEDS_PLAN boolean
# would certify whichever gate it was written for while silently answering the
# others wrong. Modelling the program instead is the scripts/fleet/CLAUDE.md
# rule ("evaluate the program against fixture JSON … fail closed on a program
# shape you don't model"); STUB_LABELS_FAIL simulates a gh lookup failure so the
# fail-open policy on `unknown` is exercised rather than assumed.
STUB_HOLDERS=""
STUB_PLAN_COMMENTS=0
STUB_LABELS=""
STUB_LABELS_FAIL=0
gh() {
    case "${1:-}" in
        issue)
            case "${2:-}" in
                view)
                    if printf '%s ' "$@" | grep -q -- "--json labels"; then
                        [[ "${STUB_LABELS_FAIL:-0}" -eq 1 ]] && return 1
                        local jq_prog="" prev="" a
                        for a in "$@"; do
                            [[ "$prev" == "--jq" ]] && jq_prog="$a"
                            prev="$a"
                        done
                        local label_re='^any\(\.labels\[\]; \.name == "(.+)"\)$'
                        if [[ ! "$jq_prog" =~ $label_re ]]; then
                            echo "gh stub: unmodelled --json labels --jq program: $jq_prog" >&2
                            return 1
                        fi
                        local want="${BASH_REMATCH[1]}" l found=false
                        for l in $STUB_LABELS; do
                            [[ "$l" == "$want" ]] && found=true
                        done
                        printf '%s\n' "$found"
                    else
                        printf '%s\n' "${STUB_PLAN_COMMENTS:-0}"    # dedup probe (--json comments)
                    fi
                    return 0 ;;
                edit) printf '%s\n' "$*" >> "$REMOVE_LOG"; return 0 ;;
                *) return 0 ;;
            esac ;;
        api)
            local posted="" a
            for a in "$@"; do case "$a" in labels\[\]=*) posted="${a#labels[]=}" ;; esac; done
            local out='[' first=1 h
            for h in $STUB_HOLDERS $posted; do
                [[ $first -eq 1 ]] || out+=','
                out+="{\"name\":\"$h\"}"; first=0
            done
            out+=']'; printf '%s\n' "$out"; return 0 ;;
        *) return 0 ;;
    esac
}

echo "== planning-claim / planning-release wrappers (gh stub) =="

echo "T1: no ## Plan comment + sole holder → planning-claim acquires (exit 0), issue-worded"
STUB_HOLDERS=""; STUB_PLAN_COMMENTS=0; STUB_LABELS="fleet:needs-plan"
rc=0; out=$(cmd_planning_claim 740 worker 2>&1) || rc=$?
assert_exit "$rc" 0 "no plan comment, no holder → exit 0"
case "$out" in *"issue#740"*) ok "acquire message names the target as an issue" ;; *) bad "acquire message should say issue#740, got: $out" ;; esac

echo "T2: a ## Plan comment already exists → dedup early-out (exit 3, already planned)"
STUB_HOLDERS=""; STUB_PLAN_COMMENTS=1; STUB_LABELS="fleet:needs-plan"
rc=0; out=$(cmd_planning_claim 740 worker 2>&1) || rc=$?
assert_exit "$rc" 3 "## Plan comment present → exit 3 (skip, already planned)"
case "$out" in *"already planned"*) ok "dedup message explains the skip" ;; *) bad "expected 'already planned', got: $out" ;; esac

echo "T3: another host already planning (no plan comment) → yield (exit 1) + self-remove"
STUB_HOLDERS="$OTHER"; STUB_PLAN_COMMENTS=0; STUB_LABELS="fleet:needs-plan"; : > "$REMOVE_LOG"
rc=0; cmd_planning_claim 740 worker >/dev/null 2>&1 || rc=$?
assert_exit "$rc" 1 "persistent planning holder → exit 1 (never a co-win)"
if grep -q -- "--remove-label $MINE" "$REMOVE_LOG"; then ok "losing claimant self-removed its planning label"; else bad "losing claimant did not self-remove (log: $(cat "$REMOVE_LOG"))"; fi

echo "T4: non-numeric issue rejected with issue wording (exit 2)"
STUB_PLAN_COMMENTS=0
rc=0; out=$(cmd_planning_claim not-a-number worker 2>&1) || rc=$?
assert_exit "$rc" 2 "non-numeric target → exit 2"
case "$out" in *"issue must be a number"*) ok "rejection uses issue wording" ;; *) bad "expected 'issue must be a number', got: $out" ;; esac

echo "T5: planning-release removes the label"
: > "$REMOVE_LOG"; rc=0
cmd_planning_release 740 worker >/dev/null 2>&1 || rc=$?
assert_exit "$rc" 0 "release exits 0"
if grep -q -- "--remove-label $MINE" "$REMOVE_LOG"; then ok "release removed $MINE"; else bad "release did not remove $MINE (log: $(cat "$REMOVE_LOG"))"; fi

echo "T5b: the claim/release pair maintains the same-host liveness marker (#2711)"
# Without this marker a leaked planning label is indistinguishable from a live
# claim, so the sole-holder lock strands a fleet:needs-plan issue for every
# planner on every host — and `fleet-claim list` cannot tell the two apart
# because a planning claim writes no task-claim lock at all.
MARKER="$FLEET_CLAIMS_DIR/_prlabel-planning-worker"
rm -f "$MARKER"; STUB_HOLDERS=""; STUB_PLAN_COMMENTS=0; STUB_LABELS="fleet:needs-plan"
cmd_planning_claim 741 worker >/dev/null 2>&1
if [[ -f "$MARKER" ]]; then ok "planning-claim wrote the liveness marker"; else bad "planning-claim wrote no marker at $MARKER"; fi
if [[ "$(cat "$MARKER" 2>/dev/null)" == "741" ]]; then ok "marker content is the claimed issue number"; else bad "marker content should be 741, got '$(cat "$MARKER" 2>/dev/null)'"; fi
cmd_planning_release 741 worker >/dev/null 2>&1
if [[ ! -f "$MARKER" ]]; then ok "planning-release removed the liveness marker"; else bad "marker survived release (would read as a live claim forever)"; fi

echo "== --replan re-plan path (#1999): bypass dedup, gate on needs-plan present =="

echo "T8: --replan with a ## Plan comment AND fleet:needs-plan present → acquires (exit 0), bypassing dedup"
STUB_HOLDERS=""; STUB_PLAN_COMMENTS=1; STUB_LABELS="fleet:needs-plan"
rc=0; out=$(cmd_planning_claim 740 worker --replan 2>&1) || rc=$?
assert_exit "$rc" 0 "--replan + plan comment + needs-plan present → exit 0 (re-plan lock armed, not exit 3)"
case "$out" in *"issue#740"*) ok "re-plan acquire names the target as an issue" ;; *) bad "expected acquire message naming issue#740, got: $out" ;; esac

echo "T9: --replan WITHOUT fleet:needs-plan present → refuses (exit 2, misuse)"
STUB_HOLDERS=""; STUB_PLAN_COMMENTS=1; STUB_LABELS=""
rc=0; out=$(cmd_planning_claim 740 worker --replan 2>&1) || rc=$?
assert_exit "$rc" 2 "--replan with needs-plan absent → exit 2"
case "$out" in *"not flagged fleet:needs-plan"*) ok "refusal explains the missing needs-plan label" ;; *) bad "expected 'not flagged fleet:needs-plan', got: $out" ;; esac

echo "T10: --replan loser path (another host already holds fleet:planning-*) → yield (exit 1) + self-remove"
STUB_HOLDERS="$OTHER"; STUB_PLAN_COMMENTS=1; STUB_LABELS="fleet:needs-plan"; : > "$REMOVE_LOG"
rc=0; cmd_planning_claim 740 worker --replan >/dev/null 2>&1 || rc=$?
assert_exit "$rc" 1 "--replan with a persistent planning holder → exit 1 (never a co-win)"
if grep -q -- "--remove-label $MINE" "$REMOVE_LOG"; then ok "losing re-plan claimant self-removed its planning label"; else bad "losing re-plan claimant did not self-remove (log: $(cat "$REMOVE_LOG"))"; fi

echo "== #3034: the fleet:needs-human park and the first-plan stale-candidate gate =="

echo "T11: parked fleet:needs-human → planning-claim refuses (exit 1) and takes no lock"
# The park is the planner's terminal state for an unplannable issue. Before this
# gate the label was honored by ingest and worker pickup but not by the claim, so
# the dispatcher's pre-claim still succeeded and the lane head was re-dispatched
# every tick (game #94: 13 dispatches, then 2 more after the park was applied).
rm -f "$MARKER"; STUB_HOLDERS=""; STUB_PLAN_COMMENTS=0
STUB_LABELS="fleet:needs-plan fleet:needs-human"
rc=0; out=$(cmd_planning_claim 742 worker 2>&1) || rc=$?
assert_exit "$rc" 1 "parked issue → exit 1 (dispatcher falls through to the next pick)"
case "$out" in *"parked fleet:needs-human"*) ok "refusal names the park" ;; *) bad "expected 'parked fleet:needs-human', got: $out" ;; esac
if [[ -f "$MARKER" ]]; then bad "refused claim still wrote a liveness marker"; else ok "refused claim took no lock"; fi

echo "T12: the park outranks --replan (exit 1, not the re-plan path)"
# A re-plan of a parked issue is still unplannable — routing precedes replanning.
STUB_HOLDERS=""; STUB_PLAN_COMMENTS=1
STUB_LABELS="fleet:needs-plan fleet:needs-human"
rc=0; out=$(cmd_planning_claim 742 worker --replan 2>&1) || rc=$?
assert_exit "$rc" 1 "--replan on a parked issue → exit 1"
case "$out" in *"parked fleet:needs-human"*) ok "--replan hits the same park refusal" ;; *) bad "expected the park refusal, got: $out" ;; esac

echo "T13: first-plan with fleet:needs-plan ABSENT → exit 1 (stale candidate), not the exit-3 dedup"
# The exact game #94 shape: needs-plan cleared, a ## Plan comment present, and
# the dispatcher's pre-claim granted anyway — only the worker's own step-2
# re-check caught it, a whole opus dispatch later. Exiting 1 here (rather than
# falling to the dedup's 3) also saves the --replan retry that 3 provokes, which
# the --replan guard would refuse anyway.
rm -f "$MARKER"; STUB_HOLDERS=""; STUB_PLAN_COMMENTS=1; STUB_LABELS=""
rc=0; out=$(cmd_planning_claim 743 worker 2>&1) || rc=$?
assert_exit "$rc" 1 "needs-plan absent on the first-plan path → exit 1"
case "$out" in *"candidate is stale"*) ok "refusal names the stale candidate" ;; *) bad "expected 'candidate is stale', got: $out" ;; esac
if [[ -f "$MARKER" ]]; then bad "stale candidate still wrote a liveness marker"; else ok "stale candidate took no lock"; fi

echo "T14: a failed label lookup fails OPEN — both new gates let the claim through"
# _issue_label_probe returns `unknown` when gh fails, and neither new gate treats
# that as a refusal: a GitHub outage must not wedge every planning claim on the
# host. Pinning it so a future "tighten the guard" edit has to argue with a test.
rm -f "$MARKER"; STUB_HOLDERS=""; STUB_PLAN_COMMENTS=0; STUB_LABELS=""; STUB_LABELS_FAIL=1
rc=0; out=$(cmd_planning_claim 744 worker 2>&1) || rc=$?
assert_exit "$rc" 0 "label lookup failure → claim still acquires"
case "$out" in *"issue#744"*) ok "fail-open path still reaches the acquire" ;; *) bad "expected acquire naming issue#744, got: $out" ;; esac
STUB_LABELS_FAIL=0
cmd_planning_release 744 worker >/dev/null 2>&1 || true

echo "== cleanup --gh fourth pass: stale planning sweep over fleet:needs-plan =="
NOW_EPOCH=$(date +%s)
STALE_AT=$(python3 -c "import datetime;print(datetime.datetime.fromtimestamp($NOW_EPOCH-7200,datetime.timezone.utc).strftime('%Y-%m-%dT%H:%M:%SZ'))")
FRESH_AT=$(python3 -c "import datetime;print(datetime.datetime.fromtimestamp($NOW_EPOCH-10,datetime.timezone.utc).strftime('%Y-%m-%dT%H:%M:%SZ'))")
export STALE_AT FRESH_AT
NP_JSON="$TMPROOT/needsplan.json"
cat > "$NP_JSON" <<JSON
[
  {"number":80,"labels":[{"name":"fleet:needs-plan"},{"name":"fleet:planning-mac-worker"}]},
  {"number":81,"labels":[{"name":"fleet:needs-plan"},{"name":"fleet:planning-linux-worker"}]},
  {"number":82,"labels":[{"name":"fleet:needs-plan"},{"name":"fleet:planning-mac-fresh"}]}
]
JSON
export NP_JSON
gh() {
    case "${1:-}" in
        pr) [[ "${2:-}" == "list" ]] && { echo "[]"; return 0; }; return 0 ;;
        issue)
            case "${2:-}" in
                list)
                    if printf '%s ' "$@" | grep -q -- "--label fleet:needs-plan"; then cat "$NP_JSON"; else echo "[]"; fi; return 0 ;;
                edit) printf '%s\n' "$*" >> "$REMOVE_LOG"; return 0 ;;
                *) return 0 ;;
            esac ;;
        api)
            if printf '%s ' "$@" | grep -q "issues/80/"; then echo "$STALE_AT"; else echo "$FRESH_AT"; fi; return 0 ;;
        *) return 0 ;;
    esac
}

echo "T6: default TTL (3600) sweeps the 2h-old planning label, keeps the fresh one"
: > "$REMOVE_LOG"; unset FLEET_CLAIM_STALE_SECS_PLANNING
out=$(cmd_cleanup_gh "jakildev/IrredenEngine" 2>&1)
if grep -q -- "--remove-label fleet:planning-mac-worker" "$REMOVE_LOG"; then ok "stale planning label on needs-plan#80 swept"; else bad "stale label not swept (log: $(cat "$REMOVE_LOG"); out: $out)"; fi
if grep -q -- "--remove-label fleet:planning-linux-worker" "$REMOVE_LOG"; then bad "fresh planning label on #81 wrongly swept"; else ok "fresh planning label on #81 kept"; fi

echo "T7: FLEET_CLAIM_STALE_SECS_PLANNING override keeps even the 2h-old VOUCHED label"
# Post-#2711 the TTL knob governs claims the sweep cannot confirm dead:
# cross-host labels, and same-host labels with a matching liveness marker. Give
# #80 its marker so it is a vouched live claim — that is the claim class the
# override is meant to protect. (A same-host label with NO marker is a confirmed
# orphan and deliberately bypasses the TTL; T7b pins that.)
: > "$REMOVE_LOG"; export FLEET_CLAIM_STALE_SECS_PLANNING=999999
printf '80\n' > "$FLEET_CLAIMS_DIR/_prlabel-planning-worker"
cmd_cleanup_gh "jakildev/IrredenEngine" >/dev/null 2>&1
if grep -q -- "--remove-label fleet:planning-" "$REMOVE_LOG"; then bad "vouched label swept despite TTL override (log: $(cat "$REMOVE_LOG"))"; else ok "TTL override respected for a vouched claim; nothing swept"; fi

echo "T7b: a same-host planning label with NO marker is a confirmed orphan — swept despite the TTL override (#2711)"
: > "$REMOVE_LOG"; rm -f "$FLEET_CLAIMS_DIR/_prlabel-planning-worker"
cmd_cleanup_gh "jakildev/IrredenEngine" >/dev/null 2>&1
if grep -q -- "--remove-label fleet:planning-mac-worker" "$REMOVE_LOG"; then ok "confirmed orphan swept on the fast path, not held for the TTL"; else bad "orphaned planning label survived the TTL override (log: $(cat "$REMOVE_LOG"))"; fi
if grep -q -- "--remove-label fleet:planning-linux-worker" "$REMOVE_LOG"; then bad "cross-host label wrongly swept (cannot be vouched for locally)"; else ok "cross-host label kept on the TTL"; fi

echo "T7c: a FRESH same-host no-marker label is spared by the grace (claim/marker write race)"
# #82's label is 10s old (< the 120s grace), so the fast path must not reap it
# even though it has no marker — that is the window between _acquire_label_on
# returning and the marker write landing.
if grep -q -- "--remove-label fleet:planning-mac-fresh" "$REMOVE_LOG"; then bad "fresh no-marker label reaped inside the grace"; else ok "fresh no-marker label spared by the grace"; fi
unset FLEET_CLAIM_STALE_SECS_PLANNING

echo "T11: CRLF-emitting python3 stub (regression guard for #3060) — vouched claim still kept"
# Native python3 on Windows (MSYS2) CRLF-terminates every print() to stdout,
# same class of bug as native jq (#3029): `while IFS=$'\t' read -r n label`
# strips only the trailing \n, so the CR rides along on `label`. POSIX python3
# emits LF, so T7 above is a vacuous pass on Linux/macOS CI whether or not the
# `tr -d '\r'` fix is in place — this stub reproduces the Windows byte stream
# hermetically so the regression is visible on every host. It CRLF-terminates
# only the planning sweep's JSON-extraction script (matched by the
# "fleet:planning-" substring literal in its source) and passes every other
# python3 -c call (e.g. label_added_epoch's epoch parse) through untouched.
REAL_PYTHON3="$(command -v python3)"
python3() {
    if [[ "${1:-}" == "-c" && "${2:-}" == *'fleet:planning-'* ]]; then
        "$REAL_PYTHON3" "$@" | sed 's/$/\r/'
    else
        "$REAL_PYTHON3" "$@"
    fi
}
: > "$REMOVE_LOG"; export FLEET_CLAIM_STALE_SECS_PLANNING=999999
printf '80\n' > "$FLEET_CLAIMS_DIR/_prlabel-planning-worker"
cmd_cleanup_gh "jakildev/IrredenEngine" >/dev/null 2>&1
if grep -q -- "--remove-label fleet:planning-" "$REMOVE_LOG"; then bad "vouched label swept under CRLF-corrupted producer (log: $(cat "$REMOVE_LOG"))"; else ok "vouched claim survives a CRLF-corrupted producer; nothing swept"; fi
unset -f python3
unset FLEET_CLAIM_STALE_SECS_PLANNING

echo
echo "================================"
echo "  PASS: $PASS    FAIL: $FAIL"
echo "================================"
[[ "$FAIL" -eq 0 ]]
