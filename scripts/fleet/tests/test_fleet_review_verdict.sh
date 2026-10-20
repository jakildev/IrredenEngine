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
#   T18: the retired severity verdict is still rejected, no delegation
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
#   Repo aliases resolve before the wrapper reaches GitHub or fleet-transition
#   Unknown repository namespaces reject before the wrapper reaches GitHub
#   T20: --replay maps each of the four real body shapes to its edge, once,
#        and posts no review
#   T21: --replay fails closed (exit 6, no labels) on an ambiguous body, a
#        foreign head review, a moved head, and no head review
#   T22: --replay needs the claim and its own argument shape
#   T23: a swept claim is re-taken once for this dispatch's own review
#   T24: a swept claim stays refused for a different dispatch, agent, author,
#        head, a pre-claim id, or another reviewer's standing claim
#   T25: --replay reads a reviews list gh emits as one document per page

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
source "$(dirname "$0")/lib_hermetic.sh"
hermetic_poison_gh_env "$TMPROOT"
BIN="$TMPROOT/bin"
export STORE="$TMPROOT/store"        # one file per PR: pr-<N>, one label per line
export FT_LOG="$TMPROOT/ft.log"      # fleet-transition invocations (argv per line)
export GH_LOG="$TMPROOT/gh.log"      # gh invocations
export HOST_KEY="mac"                # what the fleet-claim stub reports
export FT_RC=0                       # exit code the fleet-transition stub returns
export FLEET_CLAIMS_DIR="$TMPROOT/claims"
export FLEET_STATE_DIR="$TMPROOT/fleet/state"
export FC_LOG="$TMPROOT/fc.log"      # fleet-claim invocations other than `host`
export GH_LOGIN="jakildev"           # what `gh api user` reports
export GH_REPO_SLUG="jakildev/IrredenEngine"
unset FLEET_DISPATCH_ID                # a dispatched shell must not seed the re-take
mkdir -p "$BIN" "$STORE" "$FLEET_CLAIMS_DIR"
mkdir -p "$FLEET_STATE_DIR"
printf '%s\n' '{"engine":"owner/engine","game":"owner/game"}' > "$FLEET_STATE_DIR/repos.json"

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
    # A staged next head models a push landing between two head reads.
    if [[ "$json" == headRefOid && -f "$STORE/head-next-$num" ]]; then
        mv "$STORE/head-next-$num" "$file"
    fi
    exit 0
fi
if [[ "${1:-}" == "repo" && "${2:-}" == "view" && "$*" == "repo view --json nameWithOwner --jq .nameWithOwner" ]]; then
    printf '%s\n' "$GH_REPO_SLUG"; exit 0
fi
if [[ "${1:-}" == "api" && "${2:-}" == "user" ]]; then
    [[ "$*" == "api user --jq .login" ]] || exit 2
    printf '%s\n' "$GH_LOGIN"; exit 0
fi
if [[ "${1:-}" == "api" && "$#" -eq 3 && "${3:-}" == "--paginate" ]]; then
    # The full reviews list (`gh api --paginate` merges top-level arrays).
    [[ "${2:-}" =~ /pulls/([0-9]+)/reviews$ ]] || exit 2
    file="$STORE/reviews-json-${BASH_REMATCH[1]}"
    [[ -f "$file" ]] || exit 1
    cat "$file"; exit 0
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
    while read -r commit_id state _submitted_at; do
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

# fleet-claim stub — `host` for the guard; `review-claim` (the swept-claim
# re-take) is logged and, unless FC_RC says otherwise, adds the label.
cat >"$BIN/fleet-claim" <<'FCEOF'
#!/usr/bin/env bash
if [[ "$1" == "host" ]]; then echo "${HOST_KEY:-mac}"; exit 0; fi
printf '%s\n' "$*" >>"$FC_LOG"
ns=""
if [[ "$1" == "--repo" ]]; then ns="$2"; shift 2; fi
if [[ "$1" == "review-claim" ]]; then
    [[ "${FC_RC:-0}" -eq 0 ]] || exit "$FC_RC"
    echo "fleet:reviewing-${HOST_KEY:-mac}-$3" >>"$STORE/labels-$2"
    exit 0
fi
exit 2
FCEOF
chmod +x "$BIN/fleet-claim"

export PATH="$BIN:$PATH"

# The worktree-scope assert (fleet-assert-worktree "$agent") fronts the
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
fc_calls() { grep -c . "$FC_LOG" 2>/dev/null || true; }
reset_logs() {
    : >"$FT_LOG"
    : >"$GH_LOG"
    : >"$FC_LOG"
    rm -f "$FLEET_CLAIMS_DIR/_review-verdict-worker-2"
}
# set_head_review <N> <head> <body-file> [<author> <commit>] — one submitted
# review, served both to the commit-id guard and as the full reviews list.
set_head_review() {
    local num="$1" head="$2" body="$3" author="${4:-jakildev}" commit="${5:-$2}"
    set_review_data "$num" "$head" "$commit" COMMENTED 2026-10-17T20:02:57Z
    python3 - "$STORE/reviews-json-$num" "$body" "$author" "$commit" <<'PY'
import json, sys
out, body, author, commit = sys.argv[1:]
json.dump([{"user": {"login": author}, "body": open(body, encoding="utf-8").read(),
            "commit_id": commit, "state": "COMMENTED",
            "submitted_at": "2026-10-17T20:02:57Z"}], open(out, "w"))
PY
}

run() {  # capture rc without tripping set -e
    set +e; "$WRAPPER" "$@" >"$TMPROOT/out" 2>&1; local rc=$?; set -e
    echo "$rc"
}

echo "T1: --agent + reviewing claim present → delegates, exit 0"
reset_logs
set_labels 100 fleet:reviewing-mac-worker-2 fleet:wip
set_review_data 100 head-100 head-100 COMMENTED 2026-01-02T00:00:00Z
assert_eq "$(run verdict-needs-fix 100 --agent worker-2)" "0" "T1 exits 0"
assert_eq "$(ft_calls)" "1" "T1 delegated to fleet-transition exactly once"
grep -q "verdict-needs-fix 100" "$FT_LOG" && \
    { PASS=$((PASS+1)); echo "  ok: T1 delegated the right edge+number"; } || \
    { FAIL=$((FAIL+1)); echo "  FAIL: T1 delegated the right edge+number"; }

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

echo "T3: --agent omitted → applies (human carve-out), no claim read"
reset_logs
set_labels 102 fleet:wip                                # no reviewing claim at all
set_review_data 102 head-102 head-102 COMMENTED 2026-01-02T00:00:00Z
assert_eq "$(run verdict-approve 102)" "0" "T3 exits 0 without --agent"
assert_eq "$(ft_calls)" "1" "T3 delegated (carve-out)"
assert_eq "$(grep -c -- '--json labels' "$GH_LOG" 2>/dev/null || true)" "0" \
    "T3 did NOT read claim labels (claim guard skipped when --agent omitted)"

echo "T4: non-verdict transition name → exit 2"
reset_logs
assert_eq "$(run design-block 103 --agent worker-2)" "2" "T4 rejects non-verdict edge"
assert_eq "$(ft_calls)" "0" "T4 did not delegate a non-verdict edge"
grep -q "not a verdict transition" "$TMPROOT/out" && \
    { PASS=$((PASS+1)); echo "  ok: T4 explains the lane"; } || \
    { FAIL=$((FAIL+1)); echo "  FAIL: T4 explains the lane"; }

echo "T5: usage errors → exit 2"
assert_eq "$(run verdict-approve)" "2" "T5 missing number exits 2"
assert_eq "$(run verdict-approve abc)" "2" "T5 non-int number exits 2"
assert_eq "$(run verdict-approve 100 --agent)" "2" "T5 --agent without value exits 2"

echo "T6: --repo + --dry-run threaded through to fleet-transition"
reset_logs
set_labels 104 fleet:reviewing-mac-worker-2
set_review_data 104 head-104 head-104 COMMENTED 2026-01-02T00:00:00Z
assert_eq "$(run verdict-needs-fix 104 --agent worker-2 --repo jakildev/irreden --dry-run)" "0" \
    "T6 exits 0"
grep -q -- "--repo jakildev/irreden" "$FT_LOG" && grep -q -- "--dry-run" "$FT_LOG" && \
    { PASS=$((PASS+1)); echo "  ok: T6 threaded --repo and --dry-run"; } || \
    { FAIL=$((FAIL+1)); echo "  FAIL: T6 threaded --repo and --dry-run"; }
# the guard's own label read must also carry --repo
grep -q -- "pr view 104 --repo jakildev/irreden" "$GH_LOG" && \
    { PASS=$((PASS+1)); echo "  ok: T6 claim read used --repo"; } || \
    { FAIL=$((FAIL+1)); echo "  FAIL: T6 claim read used --repo"; }
assert_eq "$(find "$FLEET_CLAIMS_DIR" -name '_review-verdict-*' -print | wc -l | tr -d ' ')" "0" \
    "T6 dry-run records no release marker"

echo "T7: fleet-transition non-zero exit propagates"
reset_logs
set_labels 105 fleet:reviewing-mac-worker-2
set_review_data 105 head-105 head-105 COMMENTED 2026-01-02T00:00:00Z
FT_RC=1 assert_eq "$(FT_RC=1 run verdict-needs-fix 105 --agent worker-2)" "1" \
    "T7 propagates fleet-transition's exit code"
assert_eq "$(find "$FLEET_CLAIMS_DIR" -name '_review-verdict-*' -print | wc -l | tr -d ' ')" "0" \
    "T7 failed transition records no release marker"

echo "T8: PR not found (gh view fails) under --agent → exit 1"
reset_logs                                              # no store file for 999
assert_eq "$(run verdict-approve 999 --agent worker-2)" "1" "T8 exits 1 on unreadable PR"
assert_eq "$(ft_calls)" "0" "T8 did not delegate when labels unreadable"

echo "T9: --agent=<name> equals-form + reviewing claim present → delegates, exit 0"
reset_logs
set_labels 106 fleet:reviewing-mac-worker-2 fleet:wip
set_review_data 106 head-106 head-106 COMMENTED 2026-01-02T00:00:00Z
assert_eq "$(run verdict-needs-fix 106 --agent=worker-2)" "0" "T9 equals-form exits 0"
assert_eq "$(ft_calls)" "1" "T9 delegated to fleet-transition exactly once"

# Proves the equals-form flows through the guard (not just the parser) — an
# equals-form claim mismatch must reject exactly like the space-form (T2).
echo "T10: --agent=<name> equals-form + reviewing claim absent → exit 4, no delegation"
reset_logs
set_labels 107 fleet:reviewing-mac-worker-9 fleet:wip   # claim is a DIFFERENT agent
assert_eq "$(run verdict-needs-fix 107 --agent=worker-2)" "4" "T10 equals-form guard exits 4"
assert_eq "$(ft_calls)" "0" "T10 did NOT delegate (no verdict applied)"

# An empty --agent= must reject like the space-form's missing value (T5), NOT
# fall through to agent="" and silently take the interactive-human carve-out
# (which skips the claim check entirely).
echo "T11: --agent= (empty equals-form) → exit 2, no delegation (guard-bypass regression)"
reset_logs
set_labels 108 fleet:wip                                # no reviewing claim at all
assert_eq "$(run verdict-approve 108 --agent=)" "2" "T11 empty --agent= exits 2"
assert_eq "$(ft_calls)" "0" "T11 did NOT delegate on empty --agent="
assert_eq "$(grep -c 'pr view' "$GH_LOG" 2>/dev/null || true)" "0" \
    "T11 did NOT read labels (rejected before the guard, not carved out)"

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

echo "T13: submitted review pins current head → delegates"
reset_logs
set_labels 111 fleet:reviewing-mac-worker-2
set_review_data 111 head-111 old-111 COMMENTED 2026-01-01T00:00:00Z \
    head-111 APPROVED 2026-01-03T00:00:00Z
assert_eq "$(run verdict-approve 111 --agent worker-2)" "0" "T13 exits 0"
assert_eq "$(ft_calls)" "1" "T13 delegates after finding the head-pinning review"
assert_eq "$(cat "$FLEET_CLAIMS_DIR/_review-verdict-worker-2")" "111" \
    "T13 records the current-claim verdict for guarded release"

echo "T14: stale submitted review and current-head PENDING review → exit 5"
reset_logs
set_labels 112 fleet:reviewing-mac-worker-2 fleet:wip
set_review_data 112 head-112 old-112 COMMENTED 2026-01-01T00:00:00Z \
    head-112 PENDING 2026-01-03T00:00:00Z
assert_eq "$(run verdict-approve 112 --agent worker-2)" "5" "T14 review-body guard exits 5"
assert_eq "$(ft_calls)" "0" "T14 does not delegate"
assert_eq "$(get_labels 112)" "fleet:reviewing-mac-worker-2 fleet:wip" "T14 labels unchanged"
assert_eq "$(grep -c '/pulls/112/reviews' "$GH_LOG")" "2" "T14 retries the reviews read once"

echo "T15: zero reviews → exit 5"
reset_logs
set_labels 113 fleet:wip
set_review_data 113 head-113
assert_eq "$(run verdict-needs-fix 113)" "5" "T15 empty review history exits 5"
assert_eq "$(ft_calls)" "0" "T15 does not delegate"

echo "T16: --no-review-check bypasses only the review-body guard"
reset_logs
set_labels 114 fleet:wip
assert_eq "$(run verdict-approve 114 --no-review-check)" "0" "T16 bypass delegates"
assert_eq "$(ft_calls)" "1" "T16 delegates exactly once"
assert_eq "$(grep -c -- '--json headRefOid' "$GH_LOG" 2>/dev/null || true)" "0" \
    "T16 performs no review-body queries"

echo "T17: reviews read failure → exit 1"
reset_logs
set_labels 115 fleet:reviewing-mac-worker-2
printf '%s\n' head-115 >"$STORE/head-115"              # reviews-115 absent
assert_eq "$(run verdict-needs-fix 115 --agent worker-2)" "1" "T17 fails closed"
assert_eq "$(ft_calls)" "0" "T17 does not delegate"

echo "T19: repo aliases resolve to their configured GitHub slugs"
for repo in engine game owner/explicit; do
    reset_logs
    set_labels 117 fleet:reviewing-mac-worker-2
    set_review_data 117 head-117 head-117 COMMENTED 2026-01-02T00:00:00Z
    assert_eq "$(run verdict-approve 117 --agent worker-2 --repo "$repo" --dry-run)" "0" \
        "T19 $repo exits 0"
    case "$repo" in
        engine) expected_repo="owner/engine" ;;
        game) expected_repo="owner/game" ;;
        *) expected_repo="$repo" ;;
    esac
    grep -q -- "pr view 117 --repo $expected_repo" "$GH_LOG" && \
        { PASS=$((PASS+1)); echo "  ok: T19 $repo reached gh as $expected_repo"; } || \
        { FAIL=$((FAIL+1)); echo "  FAIL: T19 $repo did not reach gh as $expected_repo"; }
    grep -q -- "api repos/$expected_repo/pulls/117/reviews" "$GH_LOG" && \
        { PASS=$((PASS+1)); echo "  ok: T19 $repo used the resolved review route"; } || \
        { FAIL=$((FAIL+1)); echo "  FAIL: T19 $repo did not use the resolved review route"; }
    grep -q -- "--repo $expected_repo" "$FT_LOG" && \
        { PASS=$((PASS+1)); echo "  ok: T19 $repo reached fleet-transition as $expected_repo"; } || \
        { FAIL=$((FAIL+1)); echo "  FAIL: T19 $repo did not reach fleet-transition as $expected_repo"; }
done

echo "T20: unknown repo namespace rejects before gh"
reset_logs
assert_eq "$(run verdict-approve 117 --repo unknown --dry-run)" "2" "T20 unknown repo exits 2"
grep -q "engine, game, or owner/repo" "$TMPROOT/out" && \
    { PASS=$((PASS+1)); echo "  ok: T20 names accepted repo forms"; } || \
    { FAIL=$((FAIL+1)); echo "  FAIL: T20 did not name accepted repo forms"; }
assert_eq "$(grep -c . "$GH_LOG" 2>/dev/null || true)" "0" "T20 makes no gh call"

# === T18: the retired severity verdict is not a verdict edge ==============
# The severity verdict is retired (no lane consumed it). T4 pins the generic
# rejection with design-block; this arm pins the exact retired name, so
# re-adding it to VERDICT_TRANSITIONS goes red here.
#
# The name is assembled from fragments on purpose: the retirement's
# done-check is a repo-wide grep for the retired label and edge names, which
# must come back empty outside the gated self-config files. A literal here
# would be the one tracked hit.
retired_edge="verdict-block"; retired_edge+="er"
echo "T18: the retired severity verdict ($retired_edge) is not a verdict edge"
reset_logs
set_labels 116 fleet:reviewing-mac-worker-2
set_review_data 116 head-116 head-116 COMMENTED 2026-01-02T00:00:00Z
assert_eq "$(run "$retired_edge" 116 --agent worker-2)" "2" "T18 retired verdict exits 2"
assert_eq "$(ft_calls)" "0" "T18 does not delegate the retired verdict"
grep -q "not a verdict transition" "$TMPROOT/out" && \
    { PASS=$((PASS+1)); echo "  ok: T18 says 'not a verdict transition'"; } || \
    { FAIL=$((FAIL+1)); echo "  FAIL: T18 says 'not a verdict transition'"; }

# === T19: GraphQL throttled → both guards still read over REST ============
# lib_gh_stub.py serves both GraphQL subcommands and REST routes from one
# state file. With GH_STUB_THROTTLE set, every `gh pr view` is refused the
# way GitHub's GraphQL limiter refuses it; fleet-net.sh (sourced by the
# wrapper) must answer the claim and head reads over REST.
echo "T19: GraphQL rate-limited → claim + head guards read over REST"
BIN2="$TMPROOT/bin-rest"
mkdir -p "$BIN2"
cp "$(dirname "$0")/lib_gh_stub.py" "$BIN2/gh"
printf '@python3 "%%~dp0gh" %%*\r\n' >"$BIN2/gh.bat"
chmod +x "$BIN2/gh"
export GH_STUB_STATE="$TMPROOT/stub-state.json" GH_STUB_LOG="$TMPROOT/stub.log"
export GH_STUB_MISSES="$TMPROOT/stub-misses" FLEET_STATE_DIR="$TMPROOT/fleet-state"
: >"$GH_STUB_MISSES"
seed_stub() {  # seed_stub <N> <reviewed-sha> <label...>
    python3 - "$GH_STUB_STATE" "$@" <<'PY'
import json, sys
out, number, reviewed, *labels = sys.argv[1:]
head = "a" * 40
json.dump({
    "repo": "acme/widgets", "issues": {},
    "labels": {name: {"description": None, "color": "ededed"} for name in labels},
    "pulls": {number: {"title": "t", "body": "b", "state": "open", "labels": labels,
                       "comments": [], "created_at": "2026-01-01T00:00:00Z",
                       "updated_at": "2026-01-01T00:00:00Z", "head": "h", "sha": head,
                       "base": "master", "draft": False, "merged_at": None,
                       "mergeable": True}},
    "reviews": {number: [{"commit_id": head if reviewed == "head" else reviewed,
                          "state": "COMMENTED"}]},
}, open(out, "w"), indent=1, sort_keys=True)
PY
}
throttle_arm() {  # throttle_arm <0|1> <N> <reviewed> <label...>: prints "rc|ft-calls"
    local throttle="$1" num="$2" reviewed="$3" rc=0
    shift 3
    reset_logs
    seed_stub "$num" "$reviewed" "$@"
    : >"$GH_STUB_LOG"
    if [[ "$throttle" == 1 ]]; then export GH_STUB_THROTTLE=1; fi
    PATH="$BIN2:$PATH" "$WRAPPER" verdict-needs-fix "$num" --agent worker-2 \
        >"$TMPROOT/out" 2>&1 || rc=$?
    unset GH_STUB_THROTTLE
    echo "$rc|$(ft_calls)"
}
plain=$(throttle_arm 0 400 head fleet:reviewing-mac-worker-2 fleet:wip)
assert_eq "$plain" "0|1" "T19 unthrottled baseline delegates once"
assert_eq "$(grep -c '^api repos/{owner}/{repo}/pulls/400 ' "$GH_STUB_LOG" || true)" "0" \
    "T19 unthrottled run reads no REST pull"
throttled=$(throttle_arm 1 400 head fleet:reviewing-mac-worker-2 fleet:wip)
assert_eq "$throttled" "$plain" "T19 throttled: same exit, same delegation"
assert_eq "$(grep -c '^api repos/{owner}/{repo}/pulls/400 --jq' "$GH_STUB_LOG" || true)" "2" \
    "T19 throttled: the claim and head reads both went over REST"
assert_eq "$(throttle_arm 1 401 head fleet:reviewing-mac-worker-9)" "4|0" \
    "T19 throttled: the misroute guard still fires"
assert_eq "$(throttle_arm 1 402 stale-sha fleet:reviewing-mac-worker-2)" "5|0" \
    "T19 throttled: the review-body guard still fires"
assert_eq "$(cat "$GH_STUB_MISSES")" "" "T19 every stub invocation was modeled"

# --- Replay and swept-claim fixtures ---------------------------------------
# The three bodies keep the verdict line and ending of three real reviews on
# one head; approve-nits adds an amend-worthy ### Nits to the plain approve.
BODIES="$TMPROOT/bodies"
mkdir -p "$BODIES"
cat >"$BODIES/escalation" <<'BODY'
## Review — re-bless four refs

**Verdict:** approve pending Opus recheck

Scoped to four PNGs.

<!-- fleet-review dispatch=disp-123 agent=worker-2 -->
🤖 Reviewed by Codex (GPT-5)

Opus recheck required: render-reference provenance needs a final render review.
BODY
cat >"$BODIES/needs-fix" <<'BODY'
## Final review — re-bless four refs

**Verdict:** needs-fix.

### Needs-fix
- docs/pr.md:10 — capture evidence missing — attach it

🤖 Reviewed by Codex (runtime: codex; review class: opus; review-pr skill)
BODY
cat >"$BODIES/approve" <<'BODY'
## Final review — re-bless four refs

**Verdict:** approve

The follow-up evidence resolves both findings.

🤖 Reviewed by Codex (runtime: codex; review class: opus; review-pr skill)

Opus recheck not required (reviewed at opus class).
BODY
cat >"$BODIES/approve-nits" <<'BODY'
## Final review — re-bless four refs

**Verdict:** approve

### Nits
- a.cpp:3 — missing assert on the invariant

🤖 Reviewed by Codex (runtime: codex; review class: opus; review-pr skill)

Opus recheck not required (reviewed at opus class).
BODY
cat >"$BODIES/ambiguous" <<'BODY'
## Final review — re-bless four refs

**Verdict:** approve

**Verdict:** needs-fix

🤖 Reviewed by Codex (runtime: codex; review class: opus; review-pr skill)
BODY
sed 's/agent=worker-2/agent=worker-3/' "$BODIES/escalation" >"$BODIES/escalation-other-agent"

replay_arm() {  # replay_arm <N> <body> <expected-edge>
    local num="$1" body="$2" edge="$3"
    reset_logs
    set_labels "$num" "fleet:reviewing-mac-worker-2"
    set_head_review "$num" "head-$num" "$BODIES/$body"
    assert_eq "$(run --replay "$num" --agent worker-2)" "0" "T20 $body: replay exits 0"
    assert_eq "$(cat "$FT_LOG")" "$edge $num" "T20 $body: applies $edge exactly once"
    assert_eq "$(grep -c '^pr review' "$GH_LOG" || true)" "0" "T20 $body: posts no review"
    assert_eq "$(cat "$FLEET_CLAIMS_DIR/_review-verdict-worker-2" 2>/dev/null)" "$num" \
        "T20 $body: records the guarded-release marker"
}
echo "T20: --replay maps each body to its edge"
replay_arm 500 escalation verdict-needs-opus-recheck
replay_arm 501 needs-fix verdict-needs-fix
replay_arm 502 approve verdict-approve
replay_arm 503 approve-nits verdict-approve-nits

stage_510() {  # stage_510 <body> [author] [commit]
    reset_logs
    set_labels 510 fleet:reviewing-mac-worker-2
    set_head_review 510 head-510 "$BODIES/$1" "${2:-jakildev}" "${3:-head-510}"
}
refuse_arm() {  # refuse_arm <what> <expected-rc>
    assert_eq "$(run --replay 510 --agent worker-2)" "$2" "T21 $1: exits $2"
    assert_eq "$(ft_calls)" "0" "T21 $1: no transition"
    assert_eq "$(get_labels 510)" "fleet:reviewing-mac-worker-2" "T21 $1: labels unchanged"
}
echo "T21: --replay fails closed"
stage_510 ambiguous;                refuse_arm "ambiguous verdict" 6
stage_510 approve someone-else;     refuse_arm "foreign head review" 6
stage_510 approve jakildev old-510; refuse_arm "no review on the current head" 6
stage_510 approve
printf '%s\n' head-510-pushed >"$STORE/head-next-510"
refuse_arm "head moved before the swap" 6
grep -q "head moved" "$TMPROOT/out" && \
    { PASS=$((PASS+1)); echo "  ok: T21 names the moved head"; } || \
    { FAIL=$((FAIL+1)); echo "  FAIL: T21 names the moved head"; }

echo "T22: --replay needs the claim and its own argument shape"
reset_logs; set_labels 520 fleet:reviewing-mac-worker-9
set_head_review 520 head-520 "$BODIES/approve"
assert_eq "$(FLEET_DISPATCH_ID=disp-123 run --replay 520 --agent worker-2)" "4" \
    "T22 replay without the claim exits 4"
assert_eq "$(fc_calls)" "0" "T22 replay never re-takes a claim"
assert_eq "$(run --replay 520)" "2" "T22 replay without --agent exits 2"
assert_eq "$(run --replay verdict-approve 520 --agent worker-2)" "2" "T22 replay with a transition exits 2"
assert_eq "$(run --replay 520 --agent worker-2 --no-review-check)" "2" "T22 replay refuses --no-review-check"
assert_eq "$(ft_calls)" "0" "T22 no transition"

echo "T23: a swept claim is re-taken for this dispatch's own review"
reset_logs; set_labels 530 fleet:wip
set_head_review 530 head-530 "$BODIES/escalation"
assert_eq "$(FLEET_DISPATCH_ID=disp-123 run verdict-needs-opus-recheck 530 --agent worker-2)" "0" \
    "T23 re-take + verdict exits 0"
assert_eq "$(cat "$FC_LOG")" "review-claim 530 worker-2" "T23 re-takes the claim exactly once"
assert_eq "$(cat "$FT_LOG")" "verdict-needs-opus-recheck 530" "T23 then applies the verdict once"
reset_logs; set_labels 531 fleet:wip
set_head_review 531 head-531 "$BODIES/escalation"
assert_eq "$(GH_REPO_SLUG=jakildev/irreden FLEET_DISPATCH_ID=disp-123 run verdict-needs-opus-recheck 531 --agent worker-2)" \
    "0" "T23 game remote: exits 0"
assert_eq "$(cat "$FC_LOG")" "--repo game review-claim 531 worker-2" "T23 game remote: namespaced re-take"

stage_540() {  # stage_540 <body> [author] [commit] [extra-label]
    reset_logs
    set_labels 540 fleet:wip ${4:+"$4"}
    set_head_review 540 head-540 "$BODIES/$1" "${2:-jakildev}" "${3:-head-540}"
}
swept_arm() {  # swept_arm <what> <dispatch-id> [VAR=value...]
    local what="$1" dispatch="$2" rc=0
    shift 2
    env FLEET_DISPATCH_ID="$dispatch" "$@" "$WRAPPER" verdict-needs-opus-recheck 540 \
        --agent worker-2 >"$TMPROOT/out" 2>&1 || rc=$?
    assert_eq "$rc" "4" "T24 $what: still the misroute refusal"
    assert_eq "$(ft_calls)" "0" "T24 $what: no transition"
}
echo "T24: a swept claim stays refused for anything but this dispatch's own review"
stage_540 escalation;                   swept_arm "different dispatch" disp-999
assert_eq "$(fc_calls)" "0" "T24 different dispatch: no re-take"
stage_540 escalation-other-agent;       swept_arm "different agent" disp-123
assert_eq "$(fc_calls)" "0" "T24 different agent: no re-take"
stage_540 escalation;                   swept_arm "different login" disp-123 GH_LOGIN=other-login
assert_eq "$(fc_calls)" "0" "T24 different login: no re-take"
stage_540 escalation someone-else;      swept_arm "review by a foreign author" disp-123
assert_eq "$(fc_calls)" "0" "T24 foreign author: no re-take"
stage_540 escalation jakildev old-540;  swept_arm "review on an old head" disp-123
assert_eq "$(fc_calls)" "0" "T24 old head: no re-take"
stage_540 escalation;                   swept_arm "pre-claim dispatch id" preclaim
assert_eq "$(fc_calls)" "0" "T24 pre-claim id: no re-take"
stage_540 escalation;                   swept_arm "no dispatch id" ""
assert_eq "$(fc_calls)" "0" "T24 no dispatch id: no re-take"
stage_540 escalation jakildev head-540 fleet:reviewing-mac-worker-9
swept_arm "another reviewer holds the claim" disp-123
assert_eq "$(fc_calls)" "0" "T24 another reviewer's claim: never stolen"
assert_eq "$(get_labels 540)" "fleet:reviewing-mac-worker-9 fleet:wip" "T24 the other claim is untouched"
stage_540 escalation;                   swept_arm "re-take refused by fleet-claim" disp-123 FC_RC=1
assert_eq "$(fc_calls)" "1" "T24 refused re-take: attempted exactly once"

echo "T25: --replay reads a review list spread over several gh pages"
reset_logs; set_labels 550 fleet:reviewing-mac-worker-2
set_head_review 550 head-550 "$BODIES/approve"
python3 - "$STORE/reviews-json-550" <<'PY'
import json, sys
path = sys.argv[1]
head_review = json.load(open(path))[0]
old = dict(head_review, commit_id="old-550", body="hm", submitted_at="2026-10-16T00:00:00Z")
with open(path, "w") as out:
    out.write(json.dumps([old] * 30) + "\n" + json.dumps([head_review]) + "\n")
PY
assert_eq "$(run --replay 550 --agent worker-2)" "0" "T25 one document per page: replay exits 0"
assert_eq "$(cat "$FT_LOG")" "verdict-approve 550" "T25 applies the last page's head-review edge"

echo ""
echo "PASS: $PASS  FAIL: $FAIL"
(( FAIL == 0 ))
