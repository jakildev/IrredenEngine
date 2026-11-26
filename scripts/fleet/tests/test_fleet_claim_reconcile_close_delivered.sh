#!/usr/bin/env bash
# Tests for reconcile R13: closing a queued issue that a merged PR delivered
# and a worker declined as delivered by that PR.
#
#   close             one `issue close --reason completed`, one comment citing
#                     the PR, merge commit, decline reason and residual, one
#                     ledger line; a second pass closes nothing
#   report-only       the same finding, nothing written
#   no close          one case per condition: the classifier's, and the apply
#                     arm's live re-reads
#   window fallback   the decline memory's branch match stands in for a row
#                     that lost its own
#   failures          a failed close writes nothing else; a failed comment is
#                     recorded and not retried
#   park release      a PR parked on the issue is un-parked in the same pass
#
# gh is a fail-closed PATH stub answering from canned JSON and logging every
# call; state.json and the decline memory are real files under a sandboxed
# HOME.

set -euo pipefail
SCRIPT_DIR=$(cd "$(dirname "$0")/.." && pwd)
source "$(dirname "$0")/lib_preflight.sh"
source "$(dirname "$0")/lib_assert.sh"
FLEET_CLAIM="$SCRIPT_DIR/fleet-claim"
[[ -x "$FLEET_CLAIM" ]] || { echo "SKIP: fleet-claim not found at $FLEET_CLAIM" >&2; exit 3; }

TMPROOT=""; cleanup(){ [[ -n "$TMPROOT" && -d "$TMPROOT" ]] && rm -rf "$TMPROOT"; }
trap cleanup EXIT
TMPROOT=$(mktemp -d "${TMPDIR:-/tmp}/close-delivered.XXXXXX")
source "$(dirname "$0")/lib_hermetic.sh"
hermetic_poison_gh_env "$TMPROOT"

unset FLEET_DISPATCH_ID FLEET_ROLE_MODEL FLEET_DISPATCH_TARGET FLEET_DISPATCH_KIND \
    FLEET_CLAIM_STALE_SECS FLEET_AMEND_SNAPSHOTS_DIR FLEET_HOME \
    FLEET_PUBLIC_TEXT_REPO_MAP FLEET_PUBLIC_TEXT_TERM_FILE
export HOME="$TMPROOT/home"
export FLEET_ENGINE_ROOT="$TMPROOT/engine"
export FLEET_CLAIMS_DIR="$TMPROOT/claims"
export FLEET_RESERVATIONS_DIR="$TMPROOT/reservations"
export FLEET_HEARTBEATS_DIR="$TMPROOT/heartbeats"
export FLEET_STATE_DIR="$TMPROOT/state"
export FLEET_ORPHANS_DIR="$TMPROOT/orphans"
export FLEET_ALERTS_DIR="$TMPROOT/alerts"
export FLEET_TEST_HOST="mac"
export FLEET_CLAIM_NO_SLEEP=1
export FLEET_SKIP_CLONE_FRESHNESS=1
LEDGER="$HOME/.fleet/reconcile/closed-delivered.jsonl"

export STUB_DATA="$TMPROOT/data"
CALLS="$STUB_DATA/calls.log"
STUB_BIN="$TMPROOT/bin"; mkdir -p "$STUB_BIN" "$STUB_DATA"
cat > "$STUB_BIN/gh" <<'PY'
#!/usr/bin/env python3
import json, os, sys
data = os.environ["STUB_DATA"]
argv = sys.argv[1:]

def load(name, default=None):
    try:
        with open(os.path.join(data, name)) as fh:
            return json.load(fh)
    except (OSError, ValueError):
        return default

def unmodelled(why):
    print("gh stub: %s: %s" % (why, " ".join(argv)), file=sys.stderr)
    with open(os.path.join(data, "unmodelled.log"), "a") as fh:
        fh.write(" ".join(argv) + "\n")
    sys.exit(64)

# gh's own flag sets, long form and shorthand, for the calls reconcile makes.
VALUE_FLAGS = {
    ("issue", "list"): {"--repo", "--state", "--search", "--json", "--limit", "--label"},
    ("pr", "list"): {"--repo", "--state", "--json", "--limit", "--search", "--head", "--base"},
    ("issue", "view"): {"--repo", "--json", "--jq"},
    ("pr", "view"): {"--repo", "--json", "--jq"},
    ("issue", "close"): {"--repo", "--reason", "--comment", "--duplicate-of"},
    ("issue", "comment"): {"--repo", "--body", "--body-file"},
    ("issue", "edit"): {"--repo", "--add-label", "--remove-label"},
}
with open(os.path.join(data, "calls.log"), "a") as fh:
    fh.write(" ".join(argv) + "\n")
if argv[:2] == ["api", "--paginate"] and "labels=fleet:state-drift" in argv[2]:
    sys.exit(0)
known = VALUE_FLAGS.get(tuple(argv[:2]))
if known is None:
    unmodelled("unmodelled command")
opts, args, i = {}, [], 2
while i < len(argv):
    if argv[i].startswith("-") and argv[i] != "-":
        if argv[i] not in known or i + 1 >= len(argv):
            print("unknown flag: %s" % argv[i], file=sys.stderr)
            unmodelled("flag")
        opts[argv[i]] = argv[i + 1]
        i += 2
    else:
        args.append(argv[i])
        i += 1
verb = tuple(argv[:2])
if verb[1] != "list" and len(args) != 1:
    unmodelled("expected one number")
fields = (opts.get("--json") or "").split(",")

def project(obj):
    missing = [f for f in fields if f not in obj]
    if missing:
        unmodelled("fixture lacks %s" % missing)
    return {f: obj[f] for f in fields}

def fail_if(name):
    if os.path.exists(os.path.join(data, name)):
        print("gh stub: forced failure", file=sys.stderr)
        sys.exit(1)

if verb == ("issue", "list"):
    fail_if("fail-issue-list")
    if opts.get("--search") != 'label:"fleet:queued"':
        unmodelled("issue list search")
    print(json.dumps([project(x) for x in load("issues.json", [])]))
elif verb == ("pr", "list"):
    # The classifier's listing asks for labels; the apply arm's re-read does not.
    apply_read = "labels" not in fields
    fail_if("fail-pr-list-live" if apply_read else "fail-pr-list")
    prs = load("prs.json", [])
    if apply_read:
        prs = load("prs-live.json", prs)
    print(json.dumps([project(x) for x in prs]))
elif verb[1] == "view":
    fail_if("fail-%s-view" % verb[0])
    obj = load("%s-%s.json" % (verb[0], args[0]))
    if obj is None:
        print("gh stub: no fixture for %s %s" % (verb[0], args[0]), file=sys.stderr)
        sys.exit(1)
    if opts.get("--jq") == ".state" and fields == ["state"]:
        print(obj["state"])
    elif "--jq" in opts:
        unmodelled("jq")
    else:
        print(json.dumps(project(obj)))
elif verb == ("issue", "close"):
    if opts.get("--reason") not in ("completed", "not planned", "duplicate"):
        print("invalid argument for \"-r, --reason\" flag", file=sys.stderr)
        sys.exit(1)
    fail_if("fail-close")
    obj = load("issue-%s.json" % args[0])
    if obj is None:
        unmodelled("close of an unknown issue")
    obj["state"] = "CLOSED"
    json.dump(obj, open(os.path.join(data, "issue-%s.json" % args[0]), "w"))
    json.dump([x for x in load("issues.json", []) if x["number"] != int(args[0])],
              open(os.path.join(data, "issues.json"), "w"))
elif verb == ("issue", "comment"):
    if opts.get("--body-file") == "-":
        body = sys.stdin.read()
    elif "--body-file" in opts:
        body = open(opts["--body-file"]).read()
    else:
        body = opts.get("--body") or unmodelled("no body")
    fail_if("fail-comment")
    with open(os.path.join(data, "comments.log"), "a") as fh:
        fh.write("== %s\n%s\n" % (args[0], body))
elif verb == ("issue", "edit"):
    pass
PY
chmod +x "$STUB_BIN/gh"
printf '@python3 "%%~dp0gh" %%*\r\n' > "$STUB_BIN/gh.bat"
export PATH="$STUB_BIN:$PATH"

SLUG="jakildev/IrredenEngine"
SHA="abcdef1230000000000000000000000000000001"
STAMP="2026-11-22T10:00:00Z"
REASON="PR #6200 shipped every criterion of #6100; the reference re-read is #6300 (and #6300 alone)"

# The close fixture: queued issue 6100, its scout row matched to PR 6200, a
# current worker decline delivered-by 6200, PR 6200 merged into master from
# the issue's branch, and no open PR. Each case then changes one thing.
arm() {
    rm -rf "$FLEET_CLAIMS_DIR" "$FLEET_RESERVATIONS_DIR" "$FLEET_HEARTBEATS_DIR" \
        "$FLEET_STATE_DIR" "$FLEET_ORPHANS_DIR" "$FLEET_ALERTS_DIR" "$STUB_DATA" "$HOME"
    mkdir -p "$FLEET_CLAIMS_DIR" "$FLEET_RESERVATIONS_DIR" "$FLEET_HEARTBEATS_DIR" \
        "$FLEET_STATE_DIR/declined" "$FLEET_ORPHANS_DIR" "$FLEET_ALERTS_DIR" "$STUB_DATA" "$HOME"
    python3 - "$STUB_DATA" "$FLEET_STATE_DIR/state.json" "$SHA" "$STAMP" <<'PY'
import json, os, sys, time
data, state, sha, stamp = sys.argv[1:]
def put(name, obj):
    json.dump(obj, open(os.path.join(data, name), "w"))
labels = [{"name": "fleet:queued"}, {"name": "human:approved"}]
put("issues.json", [{"number": 6100, "state": "OPEN", "labels": labels}])
put("issue-6100.json", {"state": "OPEN", "title": "delivered", "labels": labels,
                        "updatedAt": stamp})
put("pr-6200.json", {"state": "MERGED", "baseRefName": "master",
                     "headRefName": "codex/6100-thing", "mergeCommit": {"oid": sha}})
put("prs.json", [])
json.dump({"generated_at": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()),
           "repos": {"engine": {"prs": [], "tasks": {"in_progress": [], "open": [
               {"id": "#6100", "issue": "#6100", "updatedAt": stamp, "blocked": False,
                "has_open_blocker": False, "shadow_merged_pr": {"number": 6200}}]}}}},
          open(state, "w"))
PY
    decline "[delivered-by #6200] $REASON" 6200
    : > "$CALLS"
}
# The decline memory as fleet-dispatcher writes it: <detail> [<shadow pr>] [<stamp>]
decline() {
    {
        printf '%s\n' "${3:-$STAMP}" "declined this iteration: $1" "worker"
        [[ -z "${2:-}" ]] || printf 'shadow_merged_pr=%s\n' "$2"
        printf 'runtime=codex\n'
    } > "$FLEET_STATE_DIR/declined/task-engine-6100"
}
# Edit one JSON file in place: <file> <python statement over `d`>
edit() {
    python3 - "$1" "$2" <<'PY'
import json, sys
path, stmt = sys.argv[1:]
d = json.load(open(path))
exec(stmt)
json.dump(d, open(path, "w"))
PY
}
STATE="$FLEET_STATE_DIR/state.json"
ROW='d["repos"]["engine"]["tasks"]["open"][0]'
label() {  # <label> on both the queued listing and the live issue
    edit "$STUB_DATA/issues.json" "d[0]['labels'].append({'name': '$1'})"
    edit "$STUB_DATA/issue-6100.json" "d['labels'].append({'name': '$1'})"
}
OPEN_PR='{"number": 6400, "headRefName": "claude/6100-again", "labels": [], "body": ""}'
OPEN_PR_CLOSES='{"number": 6400, "headRefName": "claude/fleet-topic", "labels": [], "body": "Closes #6100"}'
reconcile() { "$FLEET_CLAIM" reconcile "$@" --repo "$SLUG" 2>&1 || true; }
closes()   { grep -c '^issue close ' "$CALLS" || true; }
comments() { grep -c '^issue comment ' "$CALLS" || true; }
ledger_lines() { if [[ -f "$LEDGER" ]]; then wc -l < "$LEDGER" | tr -d ' '; else echo 0; fi; }
unmodelled() { cat "$STUB_DATA/unmodelled.log" 2>/dev/null || true; }
FIX="[R13 fix ] $SLUG issue#6100"
FLAG="[R13 flag] $SLUG issue#6100"
# One --apply pass over the armed fixture must close nothing: <what> [<needle>]
no_close() {
    local out
    : > "$CALLS"
    out=$(reconcile --apply)
    assert_eq "$(closes) $(comments) $(ledger_lines)" "0 0 0" "$1: no close, no comment, no ledger line"
    [[ -z "${2:-}" ]] || assert_contains "$out" "$2" "$1: the pass says why"
    assert_eq "$(unmodelled)" "" "$1: no unmodelled gh call"
}

echo "=== 1. close ==="
arm
out=$(reconcile --apply)
assert_contains "$out" "$FIX: delivered by merged PR #6200" "the finding names the PR"
assert_contains "$out" "R13 closed $SLUG #6100, delivered by PR #6200 (abcdef123)" "--apply reports the close"
assert_eq "$(closes)" 1 "exactly one close"
assert_eq "$(grep -cxF -- "issue close 6100 --repo $SLUG --reason completed" "$CALLS" || true)" 1 "closed as completed"
assert_eq "$(comments)" 1 "exactly one comment"
assert_eq "$(grep -c "^issue comment 6100 --repo $SLUG " "$CALLS" || true)" 1 "on that issue"
assert_eq "$(grep -nE '^issue (close|comment) ' "$CALLS" | sed 's/^[0-9]*://; s/ 6100.*//' | tr '\n' ',')" \
    "issue close,issue comment," "the close comes before the comment"
body=$(cat "$STUB_DATA/comments.log" 2>/dev/null || true)
assert_contains "$body" "PR #6200 merged as $SHA" "the comment cites the PR and its merge commit"
assert_contains "$body" "> $REASON" "the comment quotes the bare decline reason"
assert_absent "$body" "[delivered-by" "the marker is stripped"
assert_contains "$body" "The decline also names: #6300." "the comment lists the residual once, and neither the issue nor the PR"
assert_contains "$body" "Reopen the issue" "the comment says how to undo"
assert_eq "$(ledger_lines)" 1 "one ledger line"
entry=$(python3 -c 'import json, sys; e = json.load(open(sys.argv[1])); print(e["repo"], e["issue"], e["pr"], e["merge_sha"], e["residuals"], e["commented"], e["host"], bool(e["ts"]))' "$LEDGER" 2>/dev/null || true)
assert_eq "$entry" "$SLUG 6100 6200 $SHA ['#6300'] True mac True" "the ledger line records the close"
assert_eq "$(unmodelled)" "" "no unmodelled gh call"
: > "$CALLS"; out=$(reconcile --apply)
assert_contains "$out" "$FLAG: declined as delivered by PR #6200, not closed: it is not in the live fleet:queued listing" \
    "a second pass over the stale snapshot only flags it"
assert_eq "$(closes) $(comments) $(ledger_lines)" "0 0 1" "and closes, comments and records nothing more"

echo "=== 2. report-only ==="
arm
out=$(reconcile)
assert_contains "$out" "$FIX: delivered by merged PR #6200" "report-only prints the finding"
assert_eq "$(closes) $(comments) $(ledger_lines)" "0 0 0" "report-only writes nothing"

echo "=== 3. no close: the classifier's conditions ==="
arm; edit "$STUB_DATA/prs.json" "d.append($OPEN_PR)"
no_close "open PR on the issue's branch" "an open PR resolves it"
arm; edit "$STUB_DATA/prs.json" "d.append($OPEN_PR_CLOSES)"
no_close "open PR closing the issue" "an open PR resolves it"
arm; edit "$STATE" "$ROW['inflight_pr'] = {'number': 6400}"
no_close "in-flight PR on the row" "an open PR resolves it"
arm; decline "waiting on a host with a display" 6200
no_close "unmarked decline"
arm; decline "[objection] the plan is wrong" 6200
no_close "objection decline"
arm; edit "$STATE" "$ROW['shadow_merged_pr']['number'] = 6201"
no_close "decline and branch match differ" "the decline names PR #6200 but the branch match is PR #6201"
arm; edit "$STATE" "$ROW['blocked'] = True"
no_close "blocked row"
arm; edit "$STATE" "$ROW['has_open_blocker'] = True"
no_close "row with an open blocker"
for lab in fleet:epic human:owned fleet:needs-human fleet:gated fleet:blocked; do
    arm; label "$lab"
    no_close "$lab" "$FLAG: declined as delivered by PR #6200, not closed: it carries $lab"
done
arm; edit "$STATE" "d['generated_at'] = '2026-01-01T00:00:00Z'"
no_close "stale snapshot"
arm; edit "$STATE" "$ROW['updatedAt'] = '2026-11-22T10:00:01Z'"
no_close "row newer than the decline"
arm; : > "$STUB_DATA/fail-pr-list"
no_close "failed PR listing" "R13 skipped $SLUG this pass"
arm; edit "$STUB_DATA/prs.json" "d.extend({'number': 7000 + i, 'headRefName': 'x', 'labels': [], 'body': ''} for i in range(300))"
no_close "PR listing at its limit" "R13 skipped $SLUG this pass"

echo "=== 4. no close: the apply arm's live re-reads ==="
arm; edit "$STUB_DATA/pr-6200.json" "d['baseRefName'] = 'claude/6000-parent'"
no_close "PR merged into another base" "not MERGED into master"
arm; edit "$STUB_DATA/pr-6200.json" "d['state'] = 'OPEN'"
no_close "PR not merged" "not MERGED into master"
arm; edit "$STUB_DATA/pr-6200.json" "d['headRefName'] = 'codex/6101-thing'"
no_close "PR head names another issue" "head branch does not match"
arm; edit "$STUB_DATA/pr-6200.json" "d['mergeCommit'] = None"
no_close "PR with no merge commit" "no merge commit"
arm; edit "$STUB_DATA/issue-6100.json" "d['updatedAt'] = '2026-11-22T10:00:01Z'"
no_close "issue newer than the decline" "it changed after the decline"
arm; edit "$STUB_DATA/issue-6100.json" "d['state'] = 'CLOSED'"
no_close "issue already closed" "the issue reads CLOSED"
for lab in fleet:epic human:owned fleet:needs-human fleet:gated fleet:blocked; do
    arm; edit "$STUB_DATA/issue-6100.json" "d['labels'].append({'name': '$lab'})"
    no_close "$lab since the listing" "it carries $lab"
done
arm; printf '[%s]' "$OPEN_PR" > "$STUB_DATA/prs-live.json"
no_close "PR opened since the listing" "open PR #6400 resolves it"
arm; printf '[%s]' "$OPEN_PR_CLOSES" > "$STUB_DATA/prs-live.json"
no_close "closing PR opened since the listing" "open PR #6400 resolves it"
arm; cp "$STUB_DATA/prs.json" "$STUB_DATA/prs-live.json"
edit "$STUB_DATA/prs-live.json" "d.extend({'number': 7000 + i, 'headRefName': 'x', 'body': ''} for i in range(300))"
no_close "live PR listing at its limit" "the open-PR listing may be cut off"
for read in issue-view pr-view pr-list-live; do
    arm; : > "$STUB_DATA/fail-$read"
    no_close "failed $read" "could not be read"
done
arm; decline "[delivered-by #6200] private-term-fixture shipped" 6200
mkdir -p "$HOME/.fleet"; printf 'private-term-fixture\n' > "$HOME/.fleet/public-text-terms"
no_close "comment refused public text" "refused public text"

echo "=== 5. window fallback ==="
arm; edit "$STATE" "del $ROW['shadow_merged_pr']"
out=$(reconcile --apply)
assert_eq "$(closes) $(comments) $(ledger_lines)" "1 1 1" "the memory's branch match stands in for the row's"
arm; edit "$STATE" "del $ROW['shadow_merged_pr']"; decline "[delivered-by #6200] $REASON" 6201
no_close "memory's branch match differs" "the branch match is PR #6201"
arm; edit "$STATE" "del $ROW['shadow_merged_pr']"; decline "[delivered-by #6200] $REASON"
no_close "no branch match anywhere" "no merged PR's branch matches it"

echo "=== 6. write failures ==="
arm; : > "$STUB_DATA/fail-close"
out=$(reconcile --apply)
assert_contains "$out" "R13 left $SLUG #6100 open: the close failed" "a failed close is reported"
assert_eq "$(closes) $(comments) $(ledger_lines)" "1 0 0" "a failed close posts and records nothing"
arm; : > "$STUB_DATA/fail-comment"
out=$(reconcile --apply)
assert_contains "$out" "its comment failed (not retried)" "a failed comment is reported"
assert_eq "$(closes) $(comments) $(ledger_lines)" "1 1 1" "a failed comment is tried once and the close recorded"
assert_contains "$(cat "$LEDGER" 2>/dev/null || true)" '"commented": false' "the ledger line says the comment failed"

echo "=== 7. a PR parked on the closed issue is released in the same pass ==="
arm
edit "$STUB_DATA/prs.json" "d.append({'number': 6500, 'headRefName': 'claude/6499-parked', 'body': 'Parked-until: #6100', 'labels': [{'name': 'fleet:wip'}, {'name': 'fleet:awaiting-infra'}]})"
out=$(reconcile --apply)
assert_eq "$(closes)" 1 "the issue closes"
assert_eq "$(grep -cxF -- "issue edit 6500 --repo $SLUG --remove-label fleet:awaiting-infra" "$CALLS" || true)" 1 "R8 lifts the park"
assert_eq "$(unmodelled)" "" "no unmodelled gh call"

echo "=== 8. stub fidelity ==="
arm
assert_eq "$(gh issue close 6100 --repo "$SLUG" --reason delivered >/dev/null 2>&1; echo $?)" 1 "the stub rejects a close reason gh rejects"
assert_eq "$(gh issue close 6100 --repo "$SLUG" --body x >/dev/null 2>&1; echo $?)" 64 "the stub rejects a flag gh issue close does not take"

summarize "reconcile R13 close delivered"
