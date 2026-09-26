#!/usr/bin/env bash
# Tests for fleet_gh_fallback.py through fleet-net.sh's gh() shim.
#
# Every case runs the real shim against lib_gh_stub.py, whose GraphQL arm
# builds gh's objects from the same state file its REST arm serves. An
# identity case runs one gh call twice from the same seed: once with GraphQL
# answering, once with every `pr|issue` subcommand refused. The throttled run
# must print the same stdout bytes, exit the same, and leave the same state.
#
#   T1  view / list / edit / comment / create: throttled == unthrottled, and
#       only the throttled run reaches `gh api` (positive fire)
#   T2  unmodeled shapes keep the GraphQL refusal and make no REST call
#   T3  label writes keep gh's semantics: an unknown label fails with no
#       POST, removing an absent label is a no-op
#   T4  a --jq list whose first page is not the whole result fails closed;
#       a REST failure surfaces its own error
#   T5  a refusal latches github-graphql.rejected.json for the usage gate;
#       a non-refusal failure latches nothing and replays gh verbatim
#   T6  the fallback still runs when a timeout runner wraps gh

set -euo pipefail

SCRIPT_DIR=$(cd "$(dirname "$0")/.." && pwd)
source "$(dirname "$0")/lib_preflight.sh"
source "$(dirname "$0")/lib_assert.sh"
LIB="$SCRIPT_DIR/fleet-net.sh"
STUB="$(dirname "$0")/lib_gh_stub.py"
DISPATCHER="$SCRIPT_DIR/fleet-dispatcher"

if [[ ! -f "$LIB" ]]; then
    echo "SKIP: subject not found at $LIB" >&2
    exit 3
fi
if [[ ! -f "$STUB" ]]; then
    echo "test setup: gh stub not found at $STUB" >&2
    exit 2
fi
if ! command -v jq >/dev/null 2>&1; then
    echo "SKIP: jq unavailable; the stub cannot model --jq" >&2
    exit 0
fi

TMPROOT=$(mktemp -d)
source "$(dirname "$0")/lib_hermetic.sh"
hermetic_poison_gh_env "$TMPROOT"
cleanup() { [[ -n "${TMPROOT:-}" && -d "$TMPROOT" ]] && rm -rf "$TMPROOT"; return 0; }
trap cleanup EXIT

BIN="$TMPROOT/bin"
OUT="$TMPROOT/out"
mkdir -p "$BIN" "$OUT"
cp "$STUB" "$BIN/gh"
printf '@python3 "%%~dp0gh" %%*\r\n' > "$BIN/gh.bat"
cat > "$BIN/timeout" <<'EOF'
#!/usr/bin/env bash
echo ran >> "$TIMEOUT_MARKER"
shift
exec "$@"
EOF
chmod +x "$BIN/gh" "$BIN/timeout"
export PATH="$BIN:$PATH"

export FLEET_STATE_DIR="$TMPROOT/state"
USAGE="$FLEET_STATE_DIR/usage"
LATCH="$USAGE/github-graphql.rejected.json"
export GH_STUB_STATE="$TMPROOT/stub-state.json"
export GH_STUB_LOG="$TMPROOT/stub.log"
export GH_STUB_MISSES="$TMPROOT/stub-misses"
export TIMEOUT_MARKER="$TMPROOT/timeout-ran"
SEED="$TMPROOT/seed.json"
REFUSAL="GraphQL: API rate limit already exceeded for user ID 1234567."
: > "$GH_STUB_MISSES"

# Isolate from the operator's fleet-up.conf threshold overrides.
export FLEET_CONF=/dev/null
for _v in $(compgen -A variable | grep '^FLEET_DISPATCHER_USAGE_GATE' || true); do
    unset "$_v"
done
unset _v

export FLEET_TIMEOUT_CMD=""
# shellcheck source=/dev/null
source "$LIB"

# 130 PRs and 140 issues: enough for the list routes to page at 100, with
# null bodies, null label descriptions, HTML-significant text, drafts, and
# every mergeable state.
python3 - "$SEED" <<'PY'
import json, sys
labels = {
    "fleet:wip": {"description": "Work in progress", "color": "ededed"},
    "fleet:approved": {"description": "Approved <b> & ready", "color": "0e8a16"},
    "fleet:queued": {"description": None, "color": "c5def5"},
    "bug": {"description": "", "color": "d73a4a"},
}
def stamp(n):
    return f"2026-01-{1 + n % 28:02d}T{n % 24:02d}:00:00Z"
pulls, issues = {}, {}
for n in range(1, 131):
    closed = n % 3 == 0
    pulls[str(n)] = {
        "title": f"PR {n}", "body": None if n % 11 == 0 else f'PR {n} <x> & "q"\nline two',
        "state": "closed" if closed else "open",
        "labels": ["fleet:wip"] * (n % 4 == 0) + ["fleet:approved"] * (n % 5 == 0),
        "created_at": stamp(n), "updated_at": stamp(n + 1), "comments": [],
        "head": f"feature/{n}", "sha": f"{n:040x}", "base": "release" if n % 10 == 0 else "master",
        "draft": n % 7 == 0, "merged_at": stamp(n + 2) if closed and n % 2 == 0 else None,
        "mergeable": [True, False, None][n % 3],
    }
for n in range(131, 271):
    issues[str(n)] = {
        "title": f"Issue {n}   sep", "body": None if n % 9 == 0 else f"Issue {n} body",
        "state": "closed" if n % 4 == 0 else "open",
        "labels": ["fleet:queued"] * (n % 2 == 0) + ["bug"] * (n % 3 == 0),
        "created_at": stamp(n), "updated_at": stamp(n + 1), "comments": [],
    }
issues["131"]["comments"] = [
    {"id": 11, "body": "ordinary", "user": "a", "created_at": stamp(1)},
    {"id": 12, "body": "## Plan\n\nsteps", "user": "b", "created_at": stamp(2)},
]
json.dump({"repo": "acme/widgets", "labels": labels, "pulls": pulls, "issues": issues,
           "reviews": {}}, open(sys.argv[1], "w"), indent=1, sort_keys=True)
PY

# --- harness ------------------------------------------------------------------------

# arm <name> <throttle 0|1> <gh argv...>: one shim call from the seed.
# Leaves $OUT/<name>.{out,err,rc,state,log}.
arm() {
    local name="$1" throttle="$2" rc=0
    shift 2
    cp "$SEED" "$GH_STUB_STATE"
    : > "$GH_STUB_LOG"
    rm -f "$USAGE"/*.json
    if [[ "$throttle" == 1 ]]; then export GH_STUB_THROTTLE=1; else unset GH_STUB_THROTTLE; fi
    gh "$@" > "$OUT/$name.out" 2> "$OUT/$name.err" < /dev/null || rc=$?
    unset GH_STUB_THROTTLE
    echo "$rc" > "$OUT/$name.rc"
    cp "$GH_STUB_STATE" "$OUT/$name.state"
    cp "$GH_STUB_LOG" "$OUT/$name.log"
}

api_calls() { grep -c '^api ' "$OUT/$1.log" || true; }
same_bytes() { cmp -s "$1" "$2"; }

# identity <label> <gh argv...>
identity() {
    local label="$1"
    shift
    arm gql 0 "$@"
    arm rest 1 "$@"
    assert_eq "$(cat "$OUT/rest.rc")" "$(cat "$OUT/gql.rc")" "$label: exit status matches"
    assert_eq "$(cat "$OUT/gql.rc")" "0" "$label: unthrottled call succeeds"
    [[ -s "$OUT/gql.out" ]] && ok "$label: unthrottled call printed something" \
        || bad "$label: unthrottled call printed nothing (vacuous comparison)"
    if same_bytes "$OUT/gql.out" "$OUT/rest.out"; then
        ok "$label: stdout bytes identical"
    else
        bad "$label: stdout bytes differ"
        echo "        graphql: $(head -c 300 "$OUT/gql.out" | od -c | head -5)"
        echo "        rest:    $(head -c 300 "$OUT/rest.out" | od -c | head -5)"
        echo "        rest stderr: $(cat "$OUT/rest.err")"
    fi
    assert_eq "$(wc -c < "$OUT/rest.err" | tr -d ' ')" "0" "$label: throttled run prints no stderr"
    if same_bytes "$OUT/gql.state" "$OUT/rest.state"; then
        ok "$label: resulting state identical"
    else
        bad "$label: resulting state differs"
        { diff "$OUT/gql.state" "$OUT/rest.state" || true; } | head -20 | sed 's/^/        /'
    fi
    assert_eq "$(api_calls gql)" "0" "$label: unthrottled run makes no REST call"
    if (( $(api_calls rest) >= 1 )); then
        ok "$label: throttled run fell back to REST"
    else
        bad "$label: throttled run made no REST call"
    fi
}

# refused_verbatim <label> <gh argv...>: throttled, unmodeled => the refusal as-is.
refused_verbatim() {
    local label="$1"
    shift
    arm rest 1 "$@"
    assert_eq "$(cat "$OUT/rest.rc")" "1" "$label: exits 1"
    assert_eq "$(cat "$OUT/rest.err")" "$REFUSAL" "$label: replays the GraphQL stderr"
    assert_eq "$(wc -c < "$OUT/rest.out" | tr -d ' ')" "0" "$label: prints no stdout"
    assert_eq "$(api_calls rest)" "0" "$label: makes no REST call"
    same_bytes "$SEED" "$OUT/rest.state" && ok "$label: state untouched" \
        || bad "$label: state changed"
}

BODY_FILE="$TMPROOT/comment-body.md"
printf 'from a file\n\n- with <markup> & "quotes"\n' > "$BODY_FILE"
TSV='[.state, .baseRefName, ([.labels[].name] | join(","))] | @tsv'
PLAN='[.comments[] | select(.body | test("^## Plan"))] | length'

echo "T1: modeled shapes are identical over REST"
identity "issue view" issue view 200 --json number,title,body,state,url,labels,createdAt,updatedAt
identity "issue view of a merged PR" issue view 60 --repo acme/widgets --json number,state,url,labels
identity "pr view, every field" pr view 12 --repo acme/widgets \
    --json number,title,body,state,url,labels,createdAt,updatedAt,headRefName,headRefOid,baseRefName,isDraft,mergedAt,mergeable
identity "pr view, null body + draft" pr view 77 -R acme/widgets --json body,isDraft,mergeable,labels
identity "pr view --jq @tsv" pr view 20 --json state,baseRefName,labels --jq "$TSV"
identity "pr view -q labels" pr view 5 --json labels -q '.labels[].name'
identity "issue view comments --jq" issue view 131 --json comments --jq "$PLAN"
identity "issue view null body --jq" issue view 135 --json body --jq .body
identity "pr list, default limit" pr list --json number,title
identity "pr list --state all across pages" pr list --state all --limit 120 --json number,state,isDraft,mergedAt
identity "pr list --state merged" pr list --state merged --limit 5 --json number,headRefName
identity "pr list --state closed" pr list --state=closed --limit 8 --json number,state
identity "pr list --base" pr list --base release --json number,baseRefName
identity "pr list --head" pr list --head feature/7 --state all --json number,headRefName
identity "issue list --state all across pages" issue list --state all --limit 150 --json number,state,labels
identity "issue list, two labels" issue list --label fleet:queued --label bug --json number,labels --limit 200
identity "issue list --jq" issue list --state open --json number --jq length
identity "issue list --jq, label" issue list -l fleet:queued --json number --jq '.[0].number // empty'
identity "pr edit add+remove" pr edit 8 --add-label fleet:approved --remove-label fleet:wip
identity "issue edit on a PR number" issue edit 10 --repo acme/widgets --remove-label fleet:approved
identity "issue edit, comma-joined labels" issue edit 133 --add-label fleet:queued,bug
identity "issue comment --body" issue comment 140 --body 'hello <world>'
identity "pr comment --body-file" pr comment 4 --body-file "$BODY_FILE"
identity "issue create" issue create --title "new thing" --body "b" --label bug

echo "T2: unmodeled shapes fail closed"
refused_verbatim "unmodeled field" pr view 12 --json statusCheckRollup
refused_verbatim "author field" issue view 131 --json author
refused_verbatim "branch positional" pr view feature/12 --json number
refused_verbatim "edit --title" pr edit 12 --title renamed
refused_verbatim "list --search" pr list --search foo --json number
refused_verbatim "comments beside another field" issue view 131 --json comments,title --jq .title
refused_verbatim "body from stdin" issue comment 131 --body-file -

echo "T3: label writes keep gh's semantics"
arm rest 1 issue edit 131 --add-label bug --add-label no-such-label
assert_eq "$(cat "$OUT/rest.rc")" "1" "unknown label: exits 1"
assert_contains "$(cat "$OUT/rest.err")" "'no-such-label' not found" "unknown label: gh's not-found error"
assert_eq "$(grep -c -- '-X POST' "$OUT/rest.log" || true)" "0" "unknown label: no POST"
same_bytes "$SEED" "$OUT/rest.state" && ok "unknown label: the valid label was not added either" \
    || bad "unknown label: state changed"
arm rest 1 issue edit 132 --remove-label fleet:wip
assert_eq "$(cat "$OUT/rest.rc")" "0" "absent label removal: exits 0"
assert_eq "$(cat "$OUT/rest.out")" "https://github.com/acme/widgets/issues/132" "absent label removal: prints the target"
assert_contains "$(cat "$OUT/rest.log")" "-X DELETE repos/{owner}/{repo}/issues/132/labels/fleet%3Awip" \
    "absent label removal: tried the DELETE"
arm rest 1 pr edit 131 --add-label bug
assert_eq "$(cat "$OUT/rest.rc")" "1" "pr edit on an issue number: exits 1"
assert_eq "$(grep -c -- '-X ' "$OUT/rest.log" || true)" "0" "pr edit on an issue number: no write"

echo "T4: one-page --jq lists and REST failures"
arm rest 1 issue list --state all --limit 150 --json number --jq length
assert_eq "$(cat "$OUT/rest.rc")" "1" "multi-page --jq list: exits 1"
assert_eq "$(cat "$OUT/rest.err")" "$REFUSAL" "multi-page --jq list: replays the refusal"
assert_eq "$(wc -c < "$OUT/rest.out" | tr -d ' ')" "0" "multi-page --jq list: prints nothing"
arm gql 0 pr view 131 --json number,title
arm rest 1 pr view 131 --json number,title
assert_eq "$(cat "$OUT/rest.rc")" "$(cat "$OUT/gql.rc")" "pr view of an issue: fails both ways"
assert_contains "$(cat "$OUT/rest.err")" "HTTP 404" "pr view of an issue: surfaces the REST error"

echo "T5: the refusal latches the usage gate"
NOW=$(date +%s)
FUTURE=$((NOW + 2400))
arm rest 1 issue view 200 --json number
python3 - "$LATCH" "$NOW" <<'PY' > "$OUT/latch-check" || true
import json, sys
try:
    d = json.load(open(sys.argv[1]))
except OSError:
    sys.exit("no latch written")
now = int(sys.argv[2])
print(d["rateLimitType"], d["status"], d["utilization"],
      "fallback-window" if 0 <= d["resetsAt"] - d["observed_at"] == 900 and d["observed_at"] >= now else d["resetsAt"])
print(d["reason"])
PY
assert_eq "$(head -1 "$OUT/latch-check")" "github_graphql rejected 1.0 fallback-window" \
    "no sampled reset: latch runs a 900 s window from the refusal"
assert_eq "$(tail -1 "$OUT/latch-check")" "gh issue view: $REFUSAL" "latch reason names the call and the refusal"
cp "$SEED" "$GH_STUB_STATE"
mkdir -p "$USAGE"
printf '{"rateLimitType":"github_graphql","utilization":0.22,"resetsAt":%s,"observed_at":%s,"limit":5000,"remaining":3900}\n' \
    "$FUTURE" "$NOW" > "$USAGE/github-graphql.json"
export GH_STUB_THROTTLE=1
gh pr list --json number > /dev/null 2>&1 || true
unset GH_STUB_THROTTLE
assert_eq "$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1]))["resetsAt"])' "$LATCH")" "$FUTURE" \
    "sampled future reset: latch carries it"
if [[ -x "$DISPATCHER" ]]; then
    assert_eq "$("$DISPATCHER" --gate-status)" "closed:github_graphql rejected util=100% (>= 90%) resets=$FUTURE" \
        "dispatcher gate closes on the shim's latch despite a 22% self-report"
fi
arm gql 0 pr view 131 --json number
assert_eq "$(cat "$OUT/gql.rc")" "1" "non-refusal failure: exit status kept"
assert_contains "$(cat "$OUT/gql.err")" "Could not resolve to a PullRequest" "non-refusal failure: gh's stderr replayed"
assert_eq "$(api_calls gql)" "0" "non-refusal failure: no REST call"
[[ -e "$LATCH" ]] && bad "non-refusal failure wrote a latch" || ok "non-refusal failure: no latch"

echo "T6: the fallback runs behind a timeout runner"
: > "$TIMEOUT_MARKER"
FLEET_TIMEOUT_CMD=timeout
arm gql 0 pr view 20 --json state,baseRefName,labels --jq "$TSV"
arm rest 1 pr view 20 --json state,baseRefName,labels --jq "$TSV"
FLEET_TIMEOUT_CMD=""
same_bytes "$OUT/gql.out" "$OUT/rest.out" && ok "timeout-wrapped: stdout identical" \
    || bad "timeout-wrapped: stdout differs"
assert_eq "$(grep -c ran "$TIMEOUT_MARKER" || true)" "2" "timeout-wrapped: the runner wrapped both calls"

echo "T7: every stub invocation was modeled"
assert_eq "$(cat "$GH_STUB_MISSES")" "" "no unmodeled gh calls reached the stub"

summarize "fleet_gh_fallback REST fallback"
