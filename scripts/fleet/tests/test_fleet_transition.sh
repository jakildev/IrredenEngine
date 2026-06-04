#!/usr/bin/env bash
# Tests for scripts/fleet/fleet-transition.
#
# fleet-transition reads a named edge from a state-machine JSON, computes
# the effective label delta against the LIVE label set, and applies it in
# a single `gh ... edit` call (idempotent). These tests stub `gh` on PATH
# with a file-backed label store and assert control flow + the resulting
# label set + the number of edit calls (the idempotency signal):
#   T1: unknown edge → exit 2, NO gh calls at all (lookup precedes I/O)
#   T2: missing/!int args → exit 2
#   T3: scope mismatch (pr edge, target is an issue) → exit 2, no edits
#   T4: first apply → effective delta only (present removed, absent added,
#       untouched labels left alone), exactly one edit call
#   T5: idempotent re-apply → exit 0, ZERO edit calls, "already satisfied"
#   T6: --dry-run → exit 0, ZERO edit calls, labels unchanged
#   T7: design-block edge → swaps design labels, leaves fleet:wip intact
#   T8: smoke-verify-linux edge → needs-linux-smoke → verified-linux
#   T9: typo protection — edge references a non-node label (fixture JSON)
#       → exit 2, no edits
#   T10: verdict-needs-opus-recheck → adds fleet:needs-opus-recheck,
#        clears awaiting-upstream-review, leaves other labels intact

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
WRAPPER="$SCRIPT_DIR/fleet-transition"
STATE_MACHINE="$SCRIPT_DIR/../../docs/agents/fleet-state-machine.json"

if [[ ! -x "$WRAPPER" ]]; then
    echo "test setup: fleet-transition not executable at $WRAPPER" >&2
    exit 1
fi
if [[ ! -f "$STATE_MACHINE" ]]; then
    echo "test setup: state-machine JSON not found at $STATE_MACHINE" >&2
    exit 1
fi

PASS=0
FAIL=0
TMPROOT=""

cleanup() { [[ -n "$TMPROOT" && -d "$TMPROOT" ]] && rm -rf "$TMPROOT"; }
trap cleanup EXIT

assert_eq() {
    local actual="$1" expected="$2" msg="$3"
    if [[ "$actual" == "$expected" ]]; then
        PASS=$((PASS + 1)); echo "  ok: $msg"
    else
        FAIL=$((FAIL + 1)); echo "  FAIL: $msg"
        echo "        expected: $expected"
        echo "        actual:   $actual"
    fi
}

# --- Sandbox + gh stub -----------------------------------------------------
TMPROOT=$(mktemp -d)
BIN="$TMPROOT/bin"
export STORE="$TMPROOT/store"        # one file per target: <kind>-<N>, label per line
export GH_LOG="$TMPROOT/gh.log"      # every gh invocation, argv joined
mkdir -p "$BIN" "$STORE"

# File-backed `gh` stub. Grammar mirrors the real calls fleet-transition
# makes: `gh <kind> view <N> [--repo X] --json (labels|number) [--jq ...]`
# and `gh <kind> edit <N> [--repo X] (--add-label L | --remove-label L)...`.
cat >"$BIN/gh" <<'GHEOF'
#!/usr/bin/env bash
printf '%s\n' "$*" >>"$GH_LOG"
kind="$1"; action="$2"; num="$3"; shift 3 || true
file="$STORE/${kind}-${num}"
want_json=""
adds=(); dels=()
while [[ $# -gt 0 ]]; do
    case "$1" in
        --repo) shift 2;;
        --json) want_json="$2"; shift 2;;
        --jq) shift 2;;
        --add-label) adds+=("$2"); shift 2;;
        --remove-label) dels+=("$2"); shift 2;;
        *) shift;;
    esac
done
case "$action" in
    view)
        [[ -f "$file" ]] || exit 1          # not found as this kind
        if [[ "$want_json" == "labels" ]]; then
            # emulate --jq '.labels[].name': one label name per line
            grep -v '^$' "$file" || true
        fi
        exit 0;;
    edit)
        # Target must exist to edit it.
        [[ -f "$file" ]] || exit 1
        for l in "${dels[@]}"; do
            grep -Fxv -- "$l" "$file" >"$file.tmp" 2>/dev/null || true
            mv "$file.tmp" "$file"
        done
        for l in "${adds[@]}"; do
            grep -Fxq -- "$l" "$file" 2>/dev/null || echo "$l" >>"$file"
        done
        exit 0;;
    *) exit 1;;
esac
GHEOF
chmod +x "$BIN/gh"

export PATH="$BIN:$PATH"
export FLEET_STATE_MACHINE="$STATE_MACHINE"

set_labels() {  # set_labels <kind> <N> <label...>
    local kind="$1" num="$2"; shift 2
    : >"$STORE/${kind}-${num}"
    local l; for l in "$@"; do echo "$l" >>"$STORE/${kind}-${num}"; done
}
get_labels() {  # sorted, space-joined live labels
    sort "$STORE/${1}-${2}" 2>/dev/null | tr '\n' ' ' | sed 's/ $//'
}
edit_calls() { grep -cE "^(pr|issue) edit " "$GH_LOG" 2>/dev/null || true; }
reset_log() { : >"$GH_LOG"; }

run() {  # capture rc without tripping set -e
    set +e; "$WRAPPER" "$@" >"$TMPROOT/out" 2>&1; local rc=$?; set -e
    echo "$rc"
}

# === T1: unknown edge → exit 2, no gh I/O =================================
echo "T1: unknown edge → exit 2, no gh calls"
reset_log
assert_eq "$(run bogus-edge 100)" "2" "T1 unknown edge exits 2"
assert_eq "$(wc -l <"$GH_LOG" | tr -d ' ')" "0" "T1 made no gh calls (lookup precedes I/O)"
grep -q "Valid transitions:" "$TMPROOT/out" && \
    { PASS=$((PASS+1)); echo "  ok: T1 lists valid transitions"; } || \
    { FAIL=$((FAIL+1)); echo "  FAIL: T1 lists valid transitions"; }

# === T2: usage errors → exit 2 ===========================================
echo "T2: usage errors → exit 2"
assert_eq "$(run verdict-approve)" "2" "T2 missing number exits 2"
assert_eq "$(run verdict-approve abc)" "2" "T2 non-int number exits 2"

# === T3: scope mismatch (pr edge, issue target) → exit 2 =================
echo "T3: scope mismatch → exit 2, no edits"
reset_log
set_labels issue 200 fleet:needs-fix          # #200 exists only as an issue
assert_eq "$(run verdict-approve 200)" "2" "T3 pr-scope edge on an issue exits 2"
assert_eq "$(edit_calls)" "0" "T3 made no edit calls"
grep -q "is a issue" "$TMPROOT/out" && \
    { PASS=$((PASS+1)); echo "  ok: T3 reports scope mismatch"; } || \
    { FAIL=$((FAIL+1)); echo "  FAIL: T3 reports scope mismatch"; }

# === T4: first apply → effective delta only ==============================
echo "T4: verdict-approve first apply → effective delta, one edit call"
reset_log
set_labels pr 100 fleet:needs-fix fleet:wip   # blocker/has-nits absent
assert_eq "$(run verdict-approve 100)" "0" "T4 applies, exit 0"
assert_eq "$(get_labels pr 100)" "fleet:approved fleet:wip" \
    "T4 needs-fix removed, approved added, wip untouched"
assert_eq "$(edit_calls)" "1" "T4 exactly one edit call (single combined delta)"

# === T5: idempotent re-apply → no-op =====================================
echo "T5: re-apply on satisfied PR → zero edits, exit 0"
reset_log
assert_eq "$(run verdict-approve 100)" "0" "T5 re-apply exits 0"
assert_eq "$(edit_calls)" "0" "T5 made ZERO edit calls (idempotent)"
grep -q "already satisfied" "$TMPROOT/out" && \
    { PASS=$((PASS+1)); echo "  ok: T5 reports already-satisfied no-op"; } || \
    { FAIL=$((FAIL+1)); echo "  FAIL: T5 reports already-satisfied no-op"; }

# === T6: --dry-run → no writes ===========================================
echo "T6: --dry-run → zero edits, labels unchanged"
reset_log
set_labels pr 101 fleet:needs-fix
assert_eq "$(run verdict-approve 101 --dry-run)" "0" "T6 dry-run exits 0"
assert_eq "$(edit_calls)" "0" "T6 dry-run made no edit calls"
assert_eq "$(get_labels pr 101)" "fleet:needs-fix" "T6 labels unchanged after dry-run"

# === T7: design-block → swap design labels, keep wip =====================
echo "T7: design-block → design-unblocked→design-blocked, wip intact"
reset_log
set_labels pr 102 fleet:design-unblocked fleet:wip
assert_eq "$(run design-block 102)" "0" "T7 design-block exits 0"
assert_eq "$(get_labels pr 102)" "fleet:design-blocked fleet:wip" \
    "T7 design-unblocked→design-blocked, fleet:wip preserved"

# === T8: smoke-verify-linux ==============================================
echo "T8: smoke-verify-linux → needs-linux-smoke → verified-linux"
reset_log
set_labels pr 103 fleet:needs-linux-smoke fleet:approved
assert_eq "$(run smoke-verify-linux 103)" "0" "T8 smoke-verify-linux exits 0"
assert_eq "$(get_labels pr 103)" "fleet:approved fleet:verified-linux" \
    "T8 needs-linux-smoke→verified-linux, approved untouched"

# === T9: typo protection (edge names a non-node label) ===================
echo "T9: edge references unknown label → exit 2, no edits"
reset_log
BAD_SM="$TMPROOT/bad-state-machine.json"
cat >"$BAD_SM" <<'JSON'
{
  "labels": [ {"name":"fleet:approved","scope":"pr","color":"0e8a16","description":"x"} ],
  "transitions": [ {"name":"bad-edge","scope":"pr","owner":"x","remove":["fleet:typo-nonexistent"],"add":["fleet:approved"],"note":"x"} ]
}
JSON
set_labels pr 104 fleet:approved
assert_eq "$(FLEET_STATE_MACHINE="$BAD_SM" run bad-edge 104)" "2" \
    "T9 unknown-label edge exits 2"
assert_eq "$(edit_calls)" "0" "T9 made no edit calls"

# === T10: verdict-needs-opus-recheck → adds needs-opus-recheck ===========
echo "T10: verdict-needs-opus-recheck → fleet:needs-opus-recheck added, awaiting-upstream-review cleared"
reset_log
set_labels pr 105 fleet:awaiting-upstream-review fleet:wip
assert_eq "$(run verdict-needs-opus-recheck 105)" "0" "T10 exits 0"
assert_eq "$(get_labels pr 105)" "fleet:needs-opus-recheck fleet:wip" \
    "T10 awaiting-upstream-review removed, needs-opus-recheck added, wip preserved"
assert_eq "$(edit_calls)" "1" "T10 exactly one edit call"

echo ""
echo "PASS: $PASS  FAIL: $FAIL"
(( FAIL == 0 ))
