#!/usr/bin/env bash
# Tests for scripts/fleet/fleet-review-verdict.
#
# fleet-review-verdict guards both the reviewing claim (when --agent is used)
# and the existence of a submitted review pinned to the PR's current head.
# On pass it delegates to fleet-transition (the sole state-machine mechanism).
#
# These tests stub `gh`, `fleet-transition`, and `fleet-claim` on PATH so
# the run is hermetic (no network, no real labels). The gh stub is
# file-backed; the fleet-transition and fleet-claim stubs log their argv so
# delegation + arg-threading can be asserted.
#
#   T1: --agent + claim present   → delegates to fleet-transition, exit 0
#   T2: --agent + claim ABSENT    → guard fires, exit 4, NO delegation,
#                                   labels unchanged
#   T3: --agent omitted (carve-out) → delegates, exit 0, no claim read
#   T4: non-verdict transition name → exit 2, no delegation
#   T5: usage errors (missing / non-int number) → exit 2
#   T6: --repo + --dry-run threaded through to fleet-transition
#   T7: fleet-transition non-zero exit propagates (exec)
#   T8: PR not found (gh view fails) under --agent → exit 1, no delegation
#   T9:  --agent=<name> equals-form + claim present → delegates, exit 0
#   T10: --agent=<name> equals-form + claim ABSENT → guard fires, exit 4
#   T11: --agent= (empty equals-form) → exit 2, no delegation (guard-bypass
#        regression: empty value must reject like the space-form, not skip
#        the claim check via the interactive-human carve-out)
#   T12: worktree-scope guard remains in front of the claim read
#   T13: submitted review pins current head → delegates
#   T14: stale submitted review + current-head PENDING review → exit 5
#   T15: zero reviews → exit 5
#   T16: --no-review-check restores the fenced label-fixup path
#   T17: reviews read failure → exit 1

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
source "$(dirname "$0")/lib_preflight.sh"
WRAPPER="$SCRIPT_DIR/fleet-review-verdict"

if [[ ! -x "$WRAPPER" ]]; then
    echo "test setup: fleet-review-verdict not executable at $WRAPPER" >&2
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

# --- Sandbox + stubs -------------------------------------------------------
TMPROOT=$(mktemp -d)
BIN="$TMPROOT/bin"
export STORE="$TMPROOT/store"        # one file per PR: pr-<N>, one label per line
export FT_LOG="$TMPROOT/ft.log"      # fleet-transition invocations (argv per line)
export GH_LOG="$TMPROOT/gh.log"      # gh invocations
export HOST_KEY="mac"                # what the fleet-claim stub reports
export FT_RC=0                       # exit code the fleet-transition stub returns
export FLEET_CLAIMS_DIR="$TMPROOT/claims"
mkdir -p "$BIN" "$STORE" "$FLEET_CLAIMS_DIR"

# gh stub — validates the argument shapes the wrapper uses and serves labels,
# headRefOid, and paginated reviews from separate per-PR store files.
cat >"$BIN/gh" <<'GHEOF'
#!/usr/bin/env bash
printf '%s\n' "$*" >>"$GH_LOG"
if [[ "${1:-}" == "pr" && "${2:-}" == "view" ]]; then
    num="${3:-}"; shift 3
    json=""; jq_expr=""
    while [[ $# -gt 0 ]]; do
        case "$1" in
            --repo) [[ -n "${2:-}" ]] || exit 2; shift 2 ;;
            --json) json="${2:-}"; shift 2 ;;
            --jq) jq_expr="${2:-}"; shift 2 ;;
            *) exit 2 ;;
        esac
    done
    case "$json:$jq_expr" in
        'labels:.labels[].name') file="$STORE/labels-$num" ;;
        'headRefOid:.headRefOid') file="$STORE/head-$num" ;;
        *) exit 2 ;;
    esac
    [[ -f "$file" ]] || exit 1
    grep -v '^$' "$file" || true
    exit 0
fi
if [[ "${1:-}" == "api" ]]; then
    path="${2:-}"; shift 2
    paginate=0; jq_expr=""
    while [[ $# -gt 0 ]]; do
        case "$1" in
            --paginate) paginate=1; shift ;;
            --jq) jq_expr="${2:-}"; shift 2 ;;
            *) exit 2 ;;
        esac
    done
    [[ "$paginate" -eq 1 ]] || exit 2
    [[ "$jq_expr" == '.[] | select(.state != "PENDING") | .commit_id' ]] || exit 2
    [[ "$path" =~ /pulls/([0-9]+)/reviews$ ]] || exit 2
    file="$STORE/reviews-${BASH_REMATCH[1]}"
    [[ -f "$file" ]] || exit 1
    while read -r commit_id state submitted_at; do
        [[ -n "$commit_id" && "$state" != "PENDING" ]] && printf '%s\n' "$commit_id"
    done < "$file"
    exit 0
fi
exit 2
GHEOF
chmod +x "$BIN/gh"

cat >"$BIN/sleep" <<'SLEEPEOF'
#!/usr/bin/env bash
exit 0
SLEEPEOF
chmod +x "$BIN/sleep"

# fleet-transition stub — record argv, return $FT_RC. Reaching it proves the
# wrapper delegated after both guards passed.
cat >"$BIN/fleet-transition" <<'FTEOF'
#!/usr/bin/env bash
printf '%s\n' "$*" >>"$FT_LOG"
exit "${FT_RC:-0}"
FTEOF
chmod +x "$BIN/fleet-transition"

# fleet-claim stub — only `fleet-claim host` is used by the guard.
cat >"$BIN/fleet-claim" <<'FCEOF'
#!/usr/bin/env bash
if [[ "$1" == "host" ]]; then echo "${HOST_KEY:-mac}"; exit 0; fi
exit 0
FCEOF
chmod +x "$BIN/fleet-claim"

export PATH="$BIN:$PATH"

# The #2402 worktree-scope assert (fleet-assert-worktree "$agent") now fronts the
# --agent path. These cases exercise the CLAIM guard, not worktree scoping, and
# run from an arbitrary cwd — allow the main-clone override so they stay
# cwd-independent. T12 drops it to prove the worktree assert actually fires.
export FLEET_ALLOW_MAIN_CLONE=1

set_labels() {  # set_labels <N> <label...>
    local num="$1"; shift
    : >"$STORE/labels-${num}"
    local l; for l in "$@"; do echo "$l" >>"$STORE/labels-${num}"; done
}
set_review_data() {  # set_review_data <N> <head> [<commit> <state> <submitted-at>]...
    local num="$1" head="$2"; shift 2
    printf '%s\n' "$head" >"$STORE/head-${num}"
    : >"$STORE/reviews-${num}"
    while [[ $# -gt 0 ]]; do
        printf '%s %s %s\n' "$1" "$2" "$3" >>"$STORE/reviews-${num}"
        shift 3
    done
}
get_labels() { sort "$STORE/labels-${1}" 2>/dev/null | tr '\n' ' ' | sed 's/ $//'; }
ft_calls() { grep -c . "$FT_LOG" 2>/dev/null || true; }
reset_logs() {
    : >"$FT_LOG"
    : >"$GH_LOG"
    rm -f "$FLEET_CLAIMS_DIR/_review-verdict-worker-2"
}

run() {  # capture rc without tripping set -e
    set +e; "$WRAPPER" "$@" >"$TMPROOT/out" 2>&1; local rc=$?; set -e
    echo "$rc"
}

# === T1: --agent + claim present → delegate, exit 0 =======================
echo "T1: --agent + reviewing claim present → delegates, exit 0"
reset_logs
set_labels 100 fleet:reviewing-mac-worker-2 fleet:wip
set_review_data 100 head-100 head-100 COMMENTED 2026-01-02T00:00:00Z
assert_eq "$(run verdict-needs-fix 100 --agent worker-2)" "0" "T1 exits 0"
assert_eq "$(ft_calls)" "1" "T1 delegated to fleet-transition exactly once"
grep -q "verdict-needs-fix 100" "$FT_LOG" && \
    { PASS=$((PASS+1)); echo "  ok: T1 delegated the right edge+number"; } || \
    { FAIL=$((FAIL+1)); echo "  FAIL: T1 delegated the right edge+number"; }

# === T2: --agent + claim ABSENT → guard fires, exit 4, no delegation ======
echo "T2: --agent + reviewing claim absent → exit 4, no delegation"
reset_logs
set_labels 101 fleet:reviewing-mac-worker-9 fleet:wip   # claim is a DIFFERENT agent
assert_eq "$(run verdict-needs-fix 101 --agent worker-2)" "4" "T2 guard exits 4"
assert_eq "$(ft_calls)" "0" "T2 did NOT delegate (no verdict applied)"
assert_eq "$(get_labels 101)" "fleet:reviewing-mac-worker-9 fleet:wip" \
    "T2 labels unchanged"
grep -q "does not hold" "$TMPROOT/out" && \
    { PASS=$((PASS+1)); echo "  ok: T2 reports misroute guard"; } || \
    { FAIL=$((FAIL+1)); echo "  FAIL: T2 reports misroute guard"; }

# === T3: --agent omitted (carve-out) → delegate, exit 0, no claim read ====
echo "T3: --agent omitted → applies (human carve-out), no claim read"
reset_logs
set_labels 102 fleet:wip                                # no reviewing claim at all
set_review_data 102 head-102 head-102 COMMENTED 2026-01-02T00:00:00Z
assert_eq "$(run verdict-approve 102)" "0" "T3 exits 0 without --agent"
assert_eq "$(ft_calls)" "1" "T3 delegated (carve-out)"
assert_eq "$(grep -c -- '--json labels' "$GH_LOG" 2>/dev/null || true)" "0" \
    "T3 did NOT read claim labels (claim guard skipped when --agent omitted)"

# === T4: non-verdict transition name → exit 2, no delegation ==============
echo "T4: non-verdict transition name → exit 2"
reset_logs
assert_eq "$(run design-block 103 --agent worker-2)" "2" "T4 rejects non-verdict edge"
assert_eq "$(ft_calls)" "0" "T4 did not delegate a non-verdict edge"
grep -q "not a verdict transition" "$TMPROOT/out" && \
    { PASS=$((PASS+1)); echo "  ok: T4 explains the lane"; } || \
    { FAIL=$((FAIL+1)); echo "  FAIL: T4 explains the lane"; }

# === T5: usage errors → exit 2 ===========================================
echo "T5: usage errors → exit 2"
assert_eq "$(run verdict-approve)" "2" "T5 missing number exits 2"
assert_eq "$(run verdict-approve abc)" "2" "T5 non-int number exits 2"
assert_eq "$(run verdict-approve 100 --agent)" "2" "T5 --agent without value exits 2"

# === T6: --repo + --dry-run threaded through =============================
echo "T6: --repo + --dry-run threaded through to fleet-transition"
reset_logs
set_labels 104 fleet:reviewing-mac-worker-2
set_review_data 104 head-104 head-104 COMMENTED 2026-01-02T00:00:00Z
assert_eq "$(run verdict-blocker 104 --agent worker-2 --repo jakildev/irreden --dry-run)" "0" \
    "T6 exits 0"
grep -q -- "--repo jakildev/irreden" "$FT_LOG" && grep -q -- "--dry-run" "$FT_LOG" && \
    { PASS=$((PASS+1)); echo "  ok: T6 threaded --repo and --dry-run"; } || \
    { FAIL=$((FAIL+1)); echo "  FAIL: T6 threaded --repo and --dry-run"; }
# the guard's own label read must also carry --repo
grep -q -- "pr view 104 --repo jakildev/irreden" "$GH_LOG" && \
    { PASS=$((PASS+1)); echo "  ok: T6 claim read used --repo"; } || \
    { FAIL=$((FAIL+1)); echo "  FAIL: T6 claim read used --repo"; }

# === T7: fleet-transition failure propagates (exec) ======================
echo "T7: fleet-transition non-zero exit propagates"
reset_logs
set_labels 105 fleet:reviewing-mac-worker-2
set_review_data 105 head-105 head-105 COMMENTED 2026-01-02T00:00:00Z
FT_RC=1 assert_eq "$(FT_RC=1 run verdict-needs-fix 105 --agent worker-2)" "1" \
    "T7 propagates fleet-transition's exit code"

# === T8: PR not found under --agent → exit 1, no delegation ==============
echo "T8: PR not found (gh view fails) under --agent → exit 1"
reset_logs                                              # no store file for 999
assert_eq "$(run verdict-approve 999 --agent worker-2)" "1" "T8 exits 1 on unreadable PR"
assert_eq "$(ft_calls)" "0" "T8 did not delegate when labels unreadable"

# === T9: --agent=<name> equals-form + claim present → delegate, exit 0 ====
echo "T9: --agent=<name> equals-form + reviewing claim present → delegates, exit 0"
reset_logs
set_labels 106 fleet:reviewing-mac-worker-2 fleet:wip
set_review_data 106 head-106 head-106 COMMENTED 2026-01-02T00:00:00Z
assert_eq "$(run verdict-needs-fix 106 --agent=worker-2)" "0" "T9 equals-form exits 0"
assert_eq "$(ft_calls)" "1" "T9 delegated to fleet-transition exactly once"

# === T10: --agent=<name> equals-form + claim ABSENT → guard fires, exit 4 ==
# Proves the equals-form flows through the guard (not just the parser) — an
# equals-form claim mismatch must reject exactly like the space-form (T2).
echo "T10: --agent=<name> equals-form + reviewing claim absent → exit 4, no delegation"
reset_logs
set_labels 107 fleet:reviewing-mac-worker-9 fleet:wip   # claim is a DIFFERENT agent
assert_eq "$(run verdict-needs-fix 107 --agent=worker-2)" "4" "T10 equals-form guard exits 4"
assert_eq "$(ft_calls)" "0" "T10 did NOT delegate (no verdict applied)"

# === T11: --agent= (empty equals-form) → exit 2, no delegation ============
# Regression for the guard-bypass fix: an empty --agent= must reject like the
# space-form's missing value (T5), NOT fall through to agent="" and silently
# take the interactive-human carve-out (which skips the claim check entirely).
echo "T11: --agent= (empty equals-form) → exit 2, no delegation (guard-bypass regression)"
reset_logs
set_labels 108 fleet:wip                                # no reviewing claim at all
assert_eq "$(run verdict-approve 108 --agent=)" "2" "T11 empty --agent= exits 2"
assert_eq "$(ft_calls)" "0" "T11 did NOT delegate on empty --agent="
assert_eq "$(grep -c 'pr view' "$GH_LOG" 2>/dev/null || true)" "0" \
    "T11 did NOT read labels (rejected before the guard, not carved out)"

# === T12: #2402 worktree-scope assert fronts the --agent path =============
# With the override dropped and cwd a non-worktree dir, the worktree assert must
# block BEFORE the claim read (exit 1, no gh view). The no-agent carve-out is
# unaffected — still delegates from anywhere.
echo "T12: worktree-scope assert fronts --agent (fires from a non-worktree cwd)"
reset_logs
set_labels 109 fleet:reviewing-mac-worker-2 fleet:wip
rc=$(cd "$TMPROOT" && FLEET_ALLOW_MAIN_CLONE= "$WRAPPER" verdict-approve 109 --agent worker-2 >/dev/null 2>&1; echo $?)
assert_eq "$rc" "1" "T12 worktree assert blocks the --agent verdict from a non-worktree cwd"
assert_eq "$(grep -c 'pr view' "$GH_LOG" 2>/dev/null || true)" "0" \
    "T12 blocked before the claim read"
reset_logs
set_labels 110 fleet:wip
set_review_data 110 head-110 head-110 COMMENTED 2026-01-02T00:00:00Z
rc=$(cd "$TMPROOT" && FLEET_ALLOW_MAIN_CLONE= "$WRAPPER" verdict-approve 110 >/dev/null 2>&1; echo $?)
assert_eq "$rc" "0" "T12 no-agent carve-out still delegates from a non-worktree cwd"

# === T13: submitted review pins current head → delegate ===================
echo "T13: submitted review pins current head → delegates"
reset_logs
set_labels 111 fleet:reviewing-mac-worker-2
set_review_data 111 head-111 old-111 COMMENTED 2026-01-01T00:00:00Z \
    head-111 APPROVED 2026-01-03T00:00:00Z
assert_eq "$(run verdict-approve 111 --agent worker-2)" "0" "T13 exits 0"
assert_eq "$(ft_calls)" "1" "T13 delegates after finding the head-pinning review"
assert_eq "$(cat "$FLEET_CLAIMS_DIR/_review-verdict-worker-2")" "111" \
    "T13 records the current-claim verdict for guarded release"

# === T14: stale submitted + current-head PENDING → exit 5 =================
echo "T14: stale submitted review and current-head PENDING review → exit 5"
reset_logs
set_labels 112 fleet:reviewing-mac-worker-2 fleet:wip
set_review_data 112 head-112 old-112 COMMENTED 2026-01-01T00:00:00Z \
    head-112 PENDING 2026-01-03T00:00:00Z
assert_eq "$(run verdict-approve 112 --agent worker-2)" "5" "T14 review-body guard exits 5"
assert_eq "$(ft_calls)" "0" "T14 does not delegate"
assert_eq "$(get_labels 112)" "fleet:reviewing-mac-worker-2 fleet:wip" "T14 labels unchanged"
assert_eq "$(grep -c '/pulls/112/reviews' "$GH_LOG")" "2" "T14 retries the reviews read once"

# === T15: zero reviews → exit 5 ===========================================
echo "T15: zero reviews → exit 5"
reset_logs
set_labels 113 fleet:wip
set_review_data 113 head-113
assert_eq "$(run verdict-needs-fix 113)" "5" "T15 empty review history exits 5"
assert_eq "$(ft_calls)" "0" "T15 does not delegate"

# === T16: explicit bypass restores the fenced fixup lane ==================
echo "T16: --no-review-check bypasses only the review-body guard"
reset_logs
set_labels 114 fleet:wip
assert_eq "$(run verdict-approve 114 --no-review-check)" "0" "T16 bypass delegates"
assert_eq "$(ft_calls)" "1" "T16 delegates exactly once"
assert_eq "$(grep -c -- '--json headRefOid' "$GH_LOG" 2>/dev/null || true)" "0" \
    "T16 performs no review-body queries"

# === T17: reviews read failure → exit 1 ===================================
echo "T17: reviews read failure → exit 1"
reset_logs
set_labels 115 fleet:reviewing-mac-worker-2
printf '%s\n' head-115 >"$STORE/head-115"              # reviews-115 absent
assert_eq "$(run verdict-blocker 115 --agent worker-2)" "1" "T17 fails closed"
assert_eq "$(ft_calls)" "0" "T17 does not delegate"

echo ""
echo "PASS: $PASS  FAIL: $FAIL"
(( FAIL == 0 ))
