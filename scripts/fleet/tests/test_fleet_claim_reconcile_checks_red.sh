#!/usr/bin/env bash
# Tests for reconcile R12: fleet:checks-red on an approved PR whose head has a
# failed check of its own.
#
#   positive fire     approved + a failed linux-build is a finding; --apply
#                     makes exactly one write, adding the label, no comment
#   removal           one write each: head green, a new head still running,
#                     fleet:approved gone
#   once per failure  a label event after the failure blocks the add; a
#                     later head's failure is labeled
#   not its own       a workflow red on master (its master-CI alert), and a
#                     job GitHub never started, give no finding and no write;
#                     a labeled PR whose failure goes red on master loses it
#   unfinished        a check still running is not a failure
#   live confirmation the classifier's live read, and the apply arm's re-read,
#                     each block a write the cache alone would make
#   gates             a missing label is reported once, not retried; a host
#                     with no fresh master-CI watch reports and writes nothing
#
# gh is a fail-closed PATH stub answering from canned JSON and logging every
# call; state.json, the master-CI marker and the alert files are real files
# under a sandboxed HOME.

set -euo pipefail
SCRIPT_DIR=$(cd "$(dirname "$0")/.." && pwd)
source "$(dirname "$0")/lib_preflight.sh"
source "$(dirname "$0")/lib_assert.sh"
FLEET_CLAIM="$SCRIPT_DIR/fleet-claim"
[[ -x "$FLEET_CLAIM" ]] || { echo "SKIP: fleet-claim not found at $FLEET_CLAIM" >&2; exit 3; }

TMPROOT=""; cleanup(){ [[ -n "$TMPROOT" && -d "$TMPROOT" ]] && rm -rf "$TMPROOT"; }
trap cleanup EXIT
TMPROOT=$(mktemp -d "${TMPDIR:-/tmp}/checks-red.XXXXXX")
source "$(dirname "$0")/lib_hermetic.sh"
hermetic_poison_gh_env "$TMPROOT"

unset FLEET_DISPATCH_ID FLEET_ROLE_MODEL FLEET_DISPATCH_TARGET FLEET_DISPATCH_KIND \
    FLEET_CLAIM_STALE_SECS FLEET_CLAIM_CROSSHOST_STALE_SECS FLEET_AMEND_SNAPSHOTS_DIR \
    FLEET_MASTER_CI_INTERVAL_SECS
export HOME="$TMPROOT/home"
export FLEET_CLAIMS_DIR="$TMPROOT/claims"
export FLEET_RESERVATIONS_DIR="$TMPROOT/reservations"
export FLEET_HEARTBEATS_DIR="$TMPROOT/heartbeats"
export FLEET_STATE_DIR="$TMPROOT/state"
export FLEET_ORPHANS_DIR="$TMPROOT/orphans"
export FLEET_ALERTS_DIR="$TMPROOT/alerts"
export FLEET_TEST_HOST="mac"
export FLEET_CLAIM_NO_SLEEP=1
export FLEET_SKIP_CLONE_FRESHNESS=1

export STUB_DATA="$TMPROOT/data"
mkdir -p "$STUB_DATA"
CALLS="$STUB_DATA/calls.log"
STUB_BIN="$TMPROOT/bin"; mkdir -p "$STUB_BIN"
cat > "$STUB_BIN/gh_stub.py" <<'PY'
import json, os, sys
data = os.environ["STUB_DATA"]
argv = sys.argv[1:]
LABEL = "fleet:checks-red"
EVENTS_JQ = '.[] | select(.label.name == "%s") | {event, created_at, label}' % LABEL

def load(name, default):
    try:
        with open(os.path.join(data, name)) as fh:
            return json.load(fh)
    except (OSError, ValueError):
        return default

def opt(flag):
    return argv[argv.index(flag) + 1] if flag in argv else None

with open(os.path.join(data, "calls.log"), "a") as fh:
    fh.write(" ".join(argv) + "\n")

if argv[:2] == ["pr", "list"]:
    print(json.dumps(load("prs.json", [])))
elif argv[:2] == ["issue", "list"]:
    print("[]")
elif argv[:2] == ["pr", "view"] and opt("--json"):
    # A list of reads is served in order, the last one repeating.
    reads = load("live-%s.json" % argv[2], None)
    if reads is None:
        print("gh stub: no live fixture for PR %s" % argv[2], file=sys.stderr)
        sys.exit(1)
    count_path = os.path.join(data, "live-%s.count" % argv[2])
    try:
        n = int(open(count_path).read())
    except (OSError, ValueError):
        n = 0
    open(count_path, "w").write(str(n + 1))
    print(json.dumps(reads[min(n, len(reads) - 1)]))
elif argv[:2] == ["issue", "edit"] and (opt("--add-label") or opt("--remove-label")):
    if opt("--add-label") and os.path.exists(os.path.join(data, "label-missing")):
        print("'%s' not found" % opt("--add-label"), file=sys.stderr)
        sys.exit(1)
elif argv[:1] == ["api"] and argv[1].endswith("/events") and "--paginate" in argv:
    if opt("--jq") != EVENTS_JQ:
        print("gh stub: unmodelled --jq %r" % opt("--jq"), file=sys.stderr)
        sys.exit(64)
    n = argv[1].split("/issues/")[1].split("/")[0]
    for e in load("events.json", {}).get(n, []):
        if (e.get("label") or {}).get("name") == LABEL:
            print(json.dumps({"event": e["event"], "created_at": e["created_at"],
                              "label": e["label"]}))
elif argv[:2] == ["api", "--paginate"] and "labels=fleet:state-drift" in argv[2]:
    pass  # no open drift tracker
elif argv[:1] == ["repo"]:
    sys.exit(1)
else:
    print("gh stub: unmodelled call: %s" % " ".join(argv), file=sys.stderr)
    with open(os.path.join(data, "unmodelled.log"), "a") as fh:
        fh.write(" ".join(argv) + "\n")
    sys.exit(64)
PY
cat > "$STUB_BIN/gh" <<'GH'
#!/usr/bin/env bash
exec python3 "$(dirname "$0")/gh_stub.py" "$@"
GH
chmod +x "$STUB_BIN/gh"
export PATH="$STUB_BIN:$PATH"

SLUG="jakildev/IrredenEngine"
H1="1111111110000000000000000000000000000001"
H2="2222222220000000000000000000000000000002"
T_FAIL="2026-11-10T12:06:46Z"

# A rollup: <name>:<workflow>:<status>:<conclusion>:<completedAt>, comma-separated.
rollup() {
    python3 - "$1" <<'PY'
import json, sys
out = []
for spec in filter(None, sys.argv[1].split(",")):
    name, wf, status, conclusion, done = spec.split(":", 4)
    out.append({"__typename": "CheckRun", "name": name, "workflowName": wf,
                "status": status, "conclusion": conclusion or None,
                "completedAt": done or None, "detailsUrl": "https://x/" + name})
print(json.dumps(out))
PY
}
GREEN_SET="comment-refs:Comment Refs:COMPLETED:SUCCESS:2026-11-10T11:30:00Z,whitespace:Whitespace:COMPLETED:SUCCESS:2026-11-10T11:30:00Z"
RED_BUILD="$GREEN_SET,linux-build:Engine Tests:COMPLETED:FAILURE:$T_FAIL"

# Fresh surfaces for PR 5101: <cached-labels> <cached-checks> <cached-head>
# [not_started]. The reconcile PR list carries the same labels.
arm() {
    rm -rf "$FLEET_CLAIMS_DIR" "$FLEET_RESERVATIONS_DIR" "$FLEET_HEARTBEATS_DIR" \
        "$FLEET_STATE_DIR" "$FLEET_ORPHANS_DIR" "$FLEET_ALERTS_DIR" "$STUB_DATA"
    mkdir -p "$FLEET_CLAIMS_DIR" "$FLEET_RESERVATIONS_DIR" "$FLEET_HEARTBEATS_DIR" \
        "$FLEET_STATE_DIR/dispatch" "$FLEET_STATE_DIR/seen-hashes" "$FLEET_ORPHANS_DIR" \
        "$FLEET_ALERTS_DIR" "$STUB_DATA"
    date +%s > "$FLEET_STATE_DIR/seen-hashes/master-ci.last-run"
    python3 - "$1" "$2" "$3" "${4:-}" "$STUB_DATA/prs.json" "$FLEET_STATE_DIR/state.json" <<'PY'
import json, sys, time
labels, checks, head, not_started, prs_path, state_path = sys.argv[1:]
labels = [l for l in labels.split(",") if l]
json.dump([{"number": 5101, "headRefName": "claude/fleet-thing-5100",
            "body": "Closes #5100", "labels": [{"name": l} for l in labels]}],
          open(prs_path, "w"))
record = {"number": 5101, "labels": sorted(labels), "checks": checks,
          "headRefOid": head, "headRefName": "claude/fleet-thing-5100"}
if not_started:
    record["not_started"] = not_started
json.dump({"generated_at": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()),
           "repos": {"engine": {"prs": [record], "tasks": {"open": [], "in_progress": []}}}},
          open(state_path, "w"))
PY
    : > "$CALLS"
}
# One live read of PR 5101 (repeat the call to queue further reads):
# <labels> <head> <rollup-spec>
live() {
    python3 - "$1" "$2" "$(rollup "$3")" "$STUB_DATA/live-5101.json" <<'PY'
import json, os, sys
labels, head, rollup, path = sys.argv[1:]
reads = json.load(open(path)) if os.path.exists(path) else []
reads.append({"number": 5101, "state": "OPEN", "headRefOid": head,
              "labels": [{"name": l} for l in labels.split(",") if l],
              "statusCheckRollup": json.loads(rollup)})
json.dump(reads, open(path, "w"))
PY
}
event() {  # <event> <created_at>
    python3 - "$1" "$2" "$STUB_DATA/events.json" <<'PY'
import json, os, sys
event, at, path = sys.argv[1:]
data = json.load(open(path)) if os.path.exists(path) else {}
data.setdefault("5101", []).append({"event": event, "created_at": at,
                                    "label": {"name": "fleet:checks-red"}})
json.dump(data, open(path, "w"))
PY
}
reconcile() { "$FLEET_CLAIM" reconcile "$@" --repo "$SLUG" 2>&1 || true; }
writes() { grep -cE '^(issue|pr) (edit|comment)|^api .*-X' "$CALLS" || true; }
added()   { grep -cxF -- "issue edit 5101 --repo $SLUG --add-label fleet:checks-red" "$CALLS" || true; }
removed() { grep -cxF -- "issue edit 5101 --repo $SLUG --remove-label fleet:checks-red" "$CALLS" || true; }
unmodelled() { cat "$STUB_DATA/unmodelled.log" 2>/dev/null || true; }
R12_ADD="[R12 fix ] $SLUG pr#5101: add fleet:checks-red"
R12_REMOVE="[R12 fix ] $SLUG pr#5101: remove fleet:checks-red"

echo "=== 1. positive fire: approved + a failed linux-build ==="
arm "fleet:approved" red "$H1"
live "fleet:approved" "$H1" "$RED_BUILD"
out=$(reconcile)
assert_contains "$out" "$R12_ADD: failed on head 111111111: linux-build" "report-only names the PR and the failed check"
assert_eq "$(writes)" 0 "report-only writes nothing"
: > "$CALLS"; out=$(reconcile --apply)
assert_contains "$out" "R12 added fleet:checks-red on $SLUG PR#5101" "--apply reports the add"
assert_eq "$(added)" 1 "--apply adds the label once"
assert_eq "$(writes)" 1 "exactly one write"
assert_absent "$(cat "$CALLS")" "comment" "no comment is posted"
assert_eq "$(unmodelled)" "" "no unmodelled gh call"
report=$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1]))["summary"])' "$FLEET_STATE_DIR/drift-report.json")
assert_contains "$report" "'R12': 1" "the drift report's summary counts R12"

echo "=== 2a. removal: the head is green ==="
arm "fleet:approved,fleet:checks-red" green "$H1"
live "fleet:approved,fleet:checks-red" "$H1" "$GREEN_SET,linux-build:Engine Tests:COMPLETED:SUCCESS:$T_FAIL"
out=$(reconcile --apply)
assert_contains "$out" "$R12_REMOVE: head 111111111 has no failed check of its own" "a green head is reported for removal"
assert_eq "$(removed)" 1 "the label is removed"
assert_eq "$(writes)" 1 "exactly one write"

echo "=== 2b. removal: a new head was pushed and its checks are running ==="
arm "fleet:approved,fleet:checks-red" unread "$H2"
live "fleet:approved,fleet:checks-red" "$H2" "$GREEN_SET,linux-build:Engine Tests:IN_PROGRESS::"
out=$(reconcile --apply)
assert_contains "$out" "$R12_REMOVE: head 222222222 has no failed check of its own" "a running new head is reported for removal"
assert_eq "$(removed)" 1 "the label is removed"
assert_eq "$(writes)" 1 "exactly one write"

echo "=== 2c. removal: fleet:approved is gone ==="
arm "fleet:checks-red,human:re-review" red "$H1"
live "fleet:checks-red,human:re-review" "$H1" "$RED_BUILD"
out=$(reconcile --apply)
assert_contains "$out" "$R12_REMOVE: fleet:approved is gone" "a lost approval is reported for removal"
assert_eq "$(removed)" 1 "the label is removed"
assert_eq "$(writes)" 1 "exactly one write"

echo "=== 3. once per failure ==="
arm "fleet:approved" red "$H1"
live "fleet:approved" "$H1" "$RED_BUILD"
event labeled "2026-11-10T13:00:00Z"
event unlabeled "2026-11-10T14:00:00Z"
out=$(reconcile --apply)
assert_absent "$out" "[R12" "a label removed after this failure is not re-added"
assert_eq "$(writes)" 0 "nothing is written"
arm "fleet:approved" red "$H2"
live "fleet:approved" "$H2" "$GREEN_SET,linux-build:Engine Tests:COMPLETED:FAILURE:2026-11-10T15:00:00Z"
event labeled "2026-11-10T13:00:00Z"
event unlabeled "2026-11-10T14:00:00Z"
out=$(reconcile --apply)
assert_contains "$out" "$R12_ADD: failed on head 222222222" "a later head's failure is reported"
assert_eq "$(added)" 1 "a later head's failure is labeled"
assert_eq "$(writes)" 1 "exactly one write"

echo "=== 4a. not its own: the workflow is red on master ==="
arm "fleet:approved" red "$H1"
live "fleet:approved" "$H1" "$RED_BUILD"
printf 'master CI red: engine-tests.yml\n' > "$FLEET_ALERTS_DIR/scout-master-ci-engine-tests.yml"
out=$(reconcile --apply)
assert_absent "$out" "[R12" "an inherited master red is not reported"
assert_eq "$(writes)" 0 "nothing is written"
rm -f "$STUB_DATA"/live-5101.*; live "fleet:approved" "$H1" "$RED_BUILD,perf-gate:Perf Gate:COMPLETED:FAILURE:$T_FAIL"
: > "$CALLS"; out=$(reconcile --apply)
assert_contains "$out" "$R12_ADD: failed on head 111111111: perf-gate" "control: a failure master does not share is still the PR's own"
assert_eq "$(added)" 1 "control: and it is labeled"

echo "=== 4b. not its own: a job GitHub never started ==="
arm "fleet:approved" red "$H1" "The job was not started because your spending limit needs to be increased."
live "fleet:approved" "$H1" "$RED_BUILD"
out=$(reconcile --apply)
assert_absent "$out" "[R12" "a never-started failure is not reported"
assert_eq "$(writes)" 0 "nothing is written"

echo "=== 4c. not its own any more: a labeled PR's failure goes red on master ==="
arm "fleet:approved,fleet:checks-red" red "$H1"
live "fleet:approved,fleet:checks-red" "$H1" "$RED_BUILD"
out=$(reconcile --apply)
assert_absent "$out" "[R12" "control: master green, a labeled own failure keeps its label"
assert_eq "$(writes)" 0 "control: nothing is written"
assert_eq "$(grep -c '^pr view 5101' "$CALLS" || true)" 0 "control: master green costs no live read"
printf 'master CI red: engine-tests.yml\n' > "$FLEET_ALERTS_DIR/scout-master-ci-engine-tests.yml"
: > "$CALLS"; out=$(reconcile --apply)
assert_contains "$out" "$R12_REMOVE: head 111111111 has no failed check of its own" "a failure master now shares is reported for removal"
assert_eq "$(removed)" 1 "the label is removed"
assert_eq "$(writes)" 1 "exactly one write"
rm -f "$STUB_DATA"/live-5101.*; live "fleet:approved,fleet:checks-red" "$H1" "$RED_BUILD,perf-gate:Perf Gate:COMPLETED:FAILURE:$T_FAIL"
: > "$CALLS"; out=$(reconcile --apply)
assert_absent "$out" "[R12" "control: an own failure beside the inherited one keeps the label"
assert_eq "$(writes)" 0 "control: nothing is written beside an own failure"

echo "=== 5. unfinished is not failed ==="
for cached in unread red; do
    arm "fleet:approved" "$cached" "$H1"
    live "fleet:approved" "$H1" "$GREEN_SET,linux-build:Engine Tests:IN_PROGRESS::"
    out=$(reconcile --apply)
    assert_absent "$out" "[R12" "a running check is no finding (cached reading: $cached)"
    assert_eq "$(writes)" 0 "nothing is written (cached reading: $cached)"
done

echo "=== 6. live confirmation ==="
arm "fleet:approved" red "$H1"
live "fleet:approved" "$H1" "$GREEN_SET,linux-build:Engine Tests:COMPLETED:SUCCESS:$T_FAIL"
out=$(reconcile --apply)
assert_absent "$out" "[R12" "a live green blocks a cached red"
assert_eq "$(writes)" 0 "nothing is written on a live green"
arm "fleet:approved" red "$H1"
live "fleet:approved" "$H2" "$GREEN_SET,linux-build:Engine Tests:COMPLETED:FAILURE:2026-11-10T15:00:00Z"
out=$(reconcile --apply)
assert_absent "$out" "[R12" "a live head that differs from the cache blocks the add"
assert_eq "$(writes)" 0 "nothing is written on a moved head"
arm "fleet:approved" red "$H1"
live "fleet:approved" "$H1" "$RED_BUILD"
live "fleet:approved" "$H1" "$GREEN_SET,linux-build:Engine Tests:COMPLETED:SUCCESS:$T_FAIL"
out=$(reconcile --apply)
assert_contains "$out" "$R12_ADD" "the classifier's live read finds the failure"
assert_contains "$out" "R12 left $SLUG PR#5101 alone" "the apply arm's re-read sees the head go green"
assert_eq "$(writes)" 0 "nothing is written when the re-read disagrees"

echo "=== 7. a missing label is reported once, not retried ==="
arm "fleet:approved" red "$H1"
live "fleet:approved" "$H1" "$RED_BUILD"
: > "$STUB_DATA/label-missing"
out=$(reconcile --apply)
assert_contains "$out" "R12 could not add fleet:checks-red on $SLUG PR#5101 (not retried" "the failed add is reported"
assert_contains "$out" "not found" "the report carries gh's error"
assert_eq "$(added)" 1 "one attempt, no retry"

echo "=== 8. a host with no fresh master-CI watch reports and writes nothing ==="
arm "fleet:approved" red "$H1"
live "fleet:approved" "$H1" "$RED_BUILD"
rm -f "$FLEET_STATE_DIR/seen-hashes/master-ci.last-run"
out=$(reconcile --apply)
assert_absent "$out" "[R12" "no marker: no finding"
assert_eq "$(writes)" 0 "no marker: nothing written"
echo $(( $(date +%s) - 3 * 900 - 60 )) > "$FLEET_STATE_DIR/seen-hashes/master-ci.last-run"
out=$(reconcile --apply)
assert_absent "$out" "[R12" "a stale marker: no finding"
assert_eq "$(writes)" 0 "a stale marker: nothing written"

summarize "reconcile R12 fleet:checks-red"
