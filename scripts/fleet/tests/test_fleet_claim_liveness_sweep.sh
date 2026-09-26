#!/usr/bin/env bash
# Sweep-level tests for claim liveness: `fleet-claim cleanup --gh` keeps a
# fleet:claim-* / fleet:amending-* label whose owner is live on this host,
# judges only its own host's labels (--own-host, and the cross-host backstop
# on the full run), stamps fleet:sweep-cooldown after an age-only removal,
# and a won amending/resolving claim clears it. The predicate itself is
# table-tested in test_fleet_claim_liveness.py; this suite pins its wiring.
#
# Every label's age comes from the events stub (AGES), every mutation lands in
# the stub's call log (CALLS), and liveness signals are real files under a
# sandboxed HOME: heartbeats, dispatch records, dispatch-current, reservations,
# FS claim locks and amend snapshots.

set -euo pipefail
SCRIPT_DIR=$(cd "$(dirname "$0")/.." && pwd)
source "$(dirname "$0")/lib_preflight.sh"
source "$(dirname "$0")/lib_assert.sh"
FLEET_CLAIM="$SCRIPT_DIR/fleet-claim"
[[ -x "$FLEET_CLAIM" ]] || { echo "SKIP: fleet-claim not found at $FLEET_CLAIM" >&2; exit 3; }

TMPROOT=""; cleanup(){ [[ -n "$TMPROOT" && -d "$TMPROOT" ]] && rm -rf "$TMPROOT"; }
trap cleanup EXIT
TMPROOT=$(mktemp -d)
source "$(dirname "$0")/lib_hermetic.sh"
hermetic_poison_gh_env "$TMPROOT"

# The pane running this suite may itself be a dispatched fleet iteration.
unset FLEET_DISPATCH_ID FLEET_ROLE_MODEL FLEET_DISPATCH_TARGET FLEET_DISPATCH_KIND \
    FLEET_CLAIM_STALE_SECS FLEET_CLAIM_STALE_SECS_ISSUES FLEET_CLAIM_CROSSHOST_STALE_SECS \
    FLEET_CLAIM_SWEPT_COOLDOWN_SECS FLEET_AMEND_SNAPSHOTS_DIR
export HOME="$TMPROOT/home"
export FLEET_CLAIMS_DIR="$TMPROOT/claims"
export FLEET_RESERVATIONS_DIR="$TMPROOT/reservations"
export FLEET_HEARTBEATS_DIR="$TMPROOT/heartbeats"
export FLEET_STATE_DIR="$TMPROOT/state"
export FLEET_ORPHANS_DIR="$TMPROOT/orphans"
export FLEET_TEST_HOST="mac"
export FLEET_CLAIM_NO_SLEEP=1
export FLEET_SKIP_CLONE_FRESHNESS=1
SNAPS="$HOME/.fleet/amend-snapshots"
mkdir -p "$HOME/.fleet" "$FLEET_CLAIMS_DIR" "$FLEET_RESERVATIONS_DIR" "$FLEET_HEARTBEATS_DIR" \
    "$FLEET_STATE_DIR/dispatch" "$FLEET_STATE_DIR/dispatch-current" "$FLEET_ORPHANS_DIR" "$SNAPS"

export STUB_DATA="$TMPROOT/data"
mkdir -p "$STUB_DATA"
CALLS="$STUB_DATA/calls.log"

# gh stub: models the calls cleanup --gh and the claim paths make. Listings
# filtered by --label return nothing (no steward/planning/plan-review work in
# these fixtures); every other call is answered from prs.json / issues.json /
# ages.json and logged one argv per line.
STUB_BIN="$TMPROOT/bin"; mkdir -p "$STUB_BIN"
cat > "$STUB_BIN/gh_stub.py" <<'PY'
import json, os, sys, time
data = os.environ["STUB_DATA"]
argv = sys.argv[1:]
def load(name, default):
    try:
        with open(os.path.join(data, name)) as fh:
            return json.load(fh)
    except (OSError, ValueError):
        return default
with open(os.path.join(data, "calls.log"), "a") as fh:
    fh.write(" ".join(argv) + "\n")
prs, issues, ages = load("prs.json", []), load("issues.json", []), load("ages.json", {})
def record(n):
    for item in issues + prs:
        if str(item.get("number")) == str(n):
            return item
    return {"number": int(n), "labels": []}
def names(item):
    return [l["name"] for l in item.get("labels", [])]
jq = argv[argv.index("--jq") + 1] if "--jq" in argv else ""
if argv[:2] == ["pr", "list"]:
    print(json.dumps(prs))
elif argv[:2] == ["issue", "list"]:
    print("[]" if "--label" in argv else json.dumps(issues))
elif argv[:2] == ["issue", "view"]:
    item = record(argv[2])
    if jq == ".labels[].name":
        print("\n".join(names(item)))
    elif jq in (".state", "'.state'"):
        print("OPEN")
    else:
        print(json.dumps({"state": "OPEN", "labels": item.get("labels", []), "body": ""}))
elif argv[:1] == ["api"]:
    path = argv[1]
    if "/events" in path:
        n = path.split("/issues/")[1].split("/")[0]
        age = ages.get(f"{n}|{os.environ.get('FLEET_LABEL_NAME', '')}")
        if age is not None:
            print(time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime(time.time() - int(age))))
    else:
        posted = [a[len("labels[]="):] for a in argv if a.startswith("labels[]=")]
        if posted:
            print(json.dumps([{"name": posted[0]}]))
        else:
            print(json.dumps([[{"name": os.environ.get("FLEET_CLAIM_CANDIDATE", "")}]]))
elif argv[:1] == ["repo"]:
    sys.exit(1)
PY
cat > "$STUB_BIN/gh" <<'GH'
#!/usr/bin/env bash
exec python3 "$(dirname "$0")/gh_stub.py" "$@"
GH
chmod +x "$STUB_BIN/gh"
export PATH="$STUB_BIN:$PATH"

NOW=$(date +%s)
fixture() {  # <prs-json> <issues-json> <ages-json>
    printf '%s\n' "$1" > "$STUB_DATA/prs.json"
    printf '%s\n' "$2" > "$STUB_DATA/issues.json"
    printf '%s\n' "$3" > "$STUB_DATA/ages.json"
    : > "$CALLS"
}
beat() {  # <agent> <age-secs>
    touch -d "@$(( NOW - $2 ))" "$FLEET_HEARTBEATS_DIR/$1"
}
lock() {  # <slug> <owner> [<dispatch-id>]
    mkdir -p "$FLEET_CLAIMS_DIR/$1"
    printf '%s\n' "$2" > "$FLEET_CLAIMS_DIR/$1/owner"
    printf '%s\n' "$NOW" > "$FLEET_CLAIMS_DIR/$1/created"
    [[ -z "${3:-}" ]] || printf '%s\n' "$3" > "$FLEET_CLAIMS_DIR/$1/dispatch_id"
}
current() { printf '%s\n' "$2" > "$FLEET_STATE_DIR/dispatch-current/$1"; }
dispatch() {  # <pane> <agent> <target>
    printf '{"role":"worker","pane":"%%%s","target":"%s","agent":"%s"}\n' "$1" "$3" "$2" \
        > "$FLEET_STATE_DIR/dispatch/pane-$1.json"
}
snap() {  # <pr> <agent> <dispatch-id>
    printf '{"pr":%s,"agent":"%s","acquired_epoch":%s,"dispatch_id":"%s"}\n' \
        "$1" "$2" "$(( NOW - 5000 ))" "$3" > "$SNAPS/$1.json"
}
removed() { grep -cxF -- "issue edit $1 --repo jakildev/IrredenEngine --remove-label $2" "$CALLS" || true; }
added()   { grep -cxF -- "issue edit $1 --repo jakildev/IrredenEngine --add-label $2" "$CALLS" || true; }
cleanup_gh() { "$FLEET_CLAIM" cleanup --gh "$@" --repo jakildev/IrredenEngine 2>&1 || true; }

echo "=== 1. pass 2 keeps a past-TTL issue claim whose owner's heartbeat is live ==="
fixture '[]' '[{"number":700,"labels":[{"name":"fleet:claim-mac-pool-9"},{"name":"fleet:in-progress"}]}]' \
    '{"700|fleet:claim-mac-pool-9":10800}'
lock 700 pool-9 D1; current pool-9 D1; beat pool-9 60
out=$(cleanup_gh)
assert_eq "$(removed 700 fleet:claim-mac-pool-9)" 0 "3h-old claim with a fresh heartbeat and matching dispatch id is kept"
assert_contains "$out" "owner live: heartbeat" "the keep names the live owner"
beat pool-9 9000
: > "$CALLS"; out=$(cleanup_gh)
assert_eq "$(removed 700 fleet:claim-mac-pool-9)" 1 "control: the same claim with a heartbeat past the 7200s TTL is removed"
assert_eq "$(removed 700 fleet:in-progress)" 1 "control: fleet:in-progress goes with the last live claim"

echo "=== 2. pass 2 keeps a claim whose owner is live-dispatched on it (stale heartbeat) ==="
fixture '[]' '[{"number":701,"labels":[{"name":"fleet:claim-mac-pool-8"}]}]' \
    '{"701|fleet:claim-mac-pool-8":10800}'
lock 701 pool-8 preclaim; current pool-8 D4; beat pool-8 20000
dispatch 3 pool-8 "task:engine:701"
out=$(cleanup_gh)
assert_eq "$(removed 701 fleet:claim-mac-pool-8)" 0 "a live task:engine:701 record keeps the claim past the TTL"
assert_contains "$out" "owner live: dispatch task:engine:701" "the keep names the dispatch"
rm -f "$FLEET_STATE_DIR/dispatch/pane-3.json"
: > "$CALLS"; cleanup_gh >/dev/null
assert_eq "$(removed 701 fleet:claim-mac-pool-8)" 1 "control: with the record gone the claim is removed"

echo "=== 3. a detached-HEAD pane is live while its heartbeat is fresh ==="
# pool-7 resumed its reserved task under a new dispatch (D2 vs the claim's D1)
# and is doing evidence work on a detached HEAD with no PR open yet.
WT="$TMPROOT/worktrees/pool-7"
git init -q "$WT"
git -C "$WT" -c user.email=t@t -c user.name=t commit -q --allow-empty -m base
git -C "$WT" checkout -q --detach
assert_eq "$(git -C "$WT" rev-parse --abbrev-ref HEAD)" "HEAD" "fixture pane is on a detached HEAD"
fixture '[]' '[{"number":702,"labels":[{"name":"fleet:claim-mac-pool-7"}]}]' \
    '{"702|fleet:claim-mac-pool-7":21600}'
lock 702 pool-7 D1; current pool-7 D2; beat pool-7 30
"$FLEET_CLAIM" reserve 702 pool-7 >/dev/null
cleanup_gh >/dev/null
assert_eq "$(removed 702 fleet:claim-mac-pool-7)" 0 "detached pane with a fresh heartbeat and a reservation naming the item is kept"
beat pool-7 9000
: > "$CALLS"; cleanup_gh >/dev/null
assert_eq "$(removed 702 fleet:claim-mac-pool-7)" 1 "control: the same pane with a stale heartbeat is removed"
"$FLEET_CLAIM" release-worktree pool-7 >/dev/null 2>&1 || true

echo "=== 4. a fresh heartbeat under a superseded id does not keep a task claim ==="
# The pane-renewal guard: a later role in the same pane refreshes the
# heartbeat, and nothing ties that dispatch to this claim.
fixture '[]' '[{"number":703,"labels":[{"name":"fleet:claim-mac-pool-6"}]}]' \
    '{"703|fleet:claim-mac-pool-6":10800}'
lock 703 pool-6 D1; current pool-6 D2; beat pool-6 5
cleanup_gh >/dev/null
assert_eq "$(removed 703 fleet:claim-mac-pool-6)" 1 "id mismatch with no reservation is not live"

echo "=== 5. pass 1: amending keep, cooldown marking, and its exemptions ==="
fixture '[
  {"number":810,"labels":[{"name":"fleet:amending-mac-pool-2"}]},
  {"number":811,"labels":[{"name":"fleet:amending-mac-pool-3"}]},
  {"number":812,"labels":[{"name":"fleet:resolving-mac-pool-4"}]},
  {"number":813,"labels":[{"name":"fleet:resolving-mac-pool-5"}]}
]' '[]' '{"810|fleet:amending-mac-pool-2":2700,"811|fleet:amending-mac-pool-3":2700,
          "812|fleet:resolving-mac-pool-4":2700,"813|fleet:resolving-mac-pool-5":600}'
snap 810 pool-2 D1; current pool-2 D1; beat pool-2 20000
dispatch 4 pool-2 "feedback:engine:810"
snap 811 pool-3 D1; current pool-3 D2
printf '812\n' > "$FLEET_CLAIMS_DIR/_prlabel-resolving-pool-4"
cleanup_gh >/dev/null
assert_eq "$(removed 810 fleet:amending-mac-pool-2)" 0 "45-min amend with a live feedback dispatch record is kept despite a stale heartbeat"
assert_eq "$(added 810 fleet:sweep-cooldown)" 0 "a kept claim stamps no cooldown"
assert_eq "$(removed 811 fleet:amending-mac-pool-3)" 1 "a superseded amend is removed"
assert_eq "$(added 811 fleet:sweep-cooldown)" 0 "control: a confirmed-dead amend owner earns no cooldown"
assert_eq "$(removed 812 fleet:resolving-mac-pool-4)" 1 "a resolving claim past the 30-min TTL is removed"
assert_eq "$(added 812 fleet:sweep-cooldown)" 1 "an age-only resolving removal stamps fleet:sweep-cooldown"
assert_eq "$(removed 813 fleet:resolving-mac-pool-5)" 1 "a resolving label with no local marker is a confirmed orphan"
assert_eq "$(added 813 fleet:sweep-cooldown)" 0 "control: a confirmed-orphan resolving removal earns no cooldown"
rm -f "$FLEET_STATE_DIR/dispatch/pane-4.json"
: > "$CALLS"; cleanup_gh >/dev/null
assert_eq "$(removed 810 fleet:amending-mac-pool-2)" 1 "control: without the record the stale-heartbeat amend is removed"
assert_eq "$(added 810 fleet:sweep-cooldown)" 1 "and the age-only removal stamps fleet:sweep-cooldown"

echo "=== 6. host scoping: cross-host backstop on the full run, --own-host ==="
fixture '[{"number":820,"labels":[{"name":"fleet:amending-windows-pool-2"}]},
          {"number":822,"labels":[{"name":"fleet:amending-mac-pool-11"}]}]' \
    '[{"number":720,"labels":[{"name":"fleet:claim-windows-pool-3"}]}]' \
    '{"820|fleet:amending-windows-pool-2":3600,"720|fleet:claim-windows-pool-3":10800,
      "822|fleet:amending-mac-pool-11":7200}'
beat pool-2 5; beat pool-3 5    # same-basename local panes: bait only
cleanup_gh >/dev/null
assert_eq "$(removed 820 fleet:amending-windows-pool-2)" 0 "full run leaves a 1h-old cross-host amending label (under the backstop)"
assert_eq "$(removed 720 fleet:claim-windows-pool-3)" 0 "full run leaves a 3h-old cross-host issue claim"
fixture '[{"number":820,"labels":[{"name":"fleet:amending-windows-pool-2"}]}]' \
    '[{"number":720,"labels":[{"name":"fleet:claim-windows-pool-3"}]}]' \
    '{"820|fleet:amending-windows-pool-2":46800,"720|fleet:claim-windows-pool-3":46800}'
cleanup_gh >/dev/null
assert_eq "$(removed 820 fleet:amending-windows-pool-2)" 1 "full run removes a cross-host amending label past the 12h backstop"
assert_eq "$(removed 720 fleet:claim-windows-pool-3)" 1 "full run removes a cross-host issue claim past the 12h backstop"
fixture '[{"number":821,"labels":[{"name":"fleet:amending-windows-pool-2"}]},
          {"number":822,"labels":[{"name":"fleet:amending-mac-pool-11"}]}]' \
    '[{"number":721,"labels":[{"name":"fleet:claim-windows-pool-3"}]}]' \
    '{"821|fleet:amending-windows-pool-2":46800,"721|fleet:claim-windows-pool-3":46800,
      "822|fleet:amending-mac-pool-11":7200}'
out=$(cleanup_gh --own-host)
assert_absent "$(cat "$CALLS")" "remove-label fleet:amending-windows" "--own-host issues no remove for another host's amending label"
assert_absent "$(cat "$CALLS")" "remove-label fleet:claim-windows" "--own-host issues no remove for another host's issue claim"
assert_eq "$(removed 822 fleet:amending-mac-pool-11)" 1 "--own-host still sweeps this host's dead amending label"
assert_absent "$(cat "$CALLS")" "label list" "--own-host skips pass 5"
assert_absent "$(cat "$CALLS")" "--label fleet:epic" "--own-host skips pass 3"
assert_absent "$(cat "$CALLS")" "--label fleet:needs-plan" "--own-host skips pass 4"
assert_absent "$(cat "$CALLS")" "issues/821/events" "--own-host takes no label age for another host's PR label"
assert_absent "$(cat "$CALLS")" "issues/721/events" "--own-host takes no label age for another host's issue claim"
: > "$CALLS"; cleanup_gh >/dev/null
assert_contains "$(cat "$CALLS")" "label list" "control: the full run does reach pass 5"

echo "=== 7. fleet:in-progress follows the same cross-host classification ==="
# The dead same-host claim goes; the 3h-old windows claim is under the
# backstop, so it is live and fleet:in-progress must stay.
fixture '[]' '[{"number":740,"labels":[{"name":"fleet:claim-mac-pool-12"},{"name":"fleet:claim-windows-pool-1"},{"name":"fleet:in-progress"}]}]' \
    '{"740|fleet:claim-mac-pool-12":10800,"740|fleet:claim-windows-pool-1":10800}'
out=$(cleanup_gh)
assert_eq "$(removed 740 fleet:claim-mac-pool-12)" 1 "the orphaned same-host claim is removed"
assert_eq "$(removed 740 fleet:in-progress)" 0 "fleet:in-progress stays under a cross-host claim inside the backstop"

echo "=== 8. an expired fleet:sweep-cooldown is cleared on the full run only ==="
old=$(python3 -c "import time;print(time.strftime('%Y-%m-%dT%H:%M:%SZ',time.gmtime(time.time()-2400)))")
new=$(python3 -c "import time;print(time.strftime('%Y-%m-%dT%H:%M:%SZ',time.gmtime(time.time()-300)))")
fixture "[{\"number\":830,\"updatedAt\":\"$old\",\"labels\":[{\"name\":\"fleet:sweep-cooldown\"}]},
          {\"number\":831,\"updatedAt\":\"$new\",\"labels\":[{\"name\":\"fleet:sweep-cooldown\"}]}]" '[]' '{}'
cleanup_gh --own-host >/dev/null
assert_eq "$(removed 830 fleet:sweep-cooldown)" 0 "--own-host leaves the global cooldown label alone"
cleanup_gh >/dev/null
assert_eq "$(removed 830 fleet:sweep-cooldown)" 1 "a cooldown quiet for 40 min is cleared"
assert_eq "$(removed 831 fleet:sweep-cooldown)" 0 "a cooldown 5 min old stays"

echo "=== 9. a won amending/resolving claim clears fleet:sweep-cooldown ==="
fixture '[{"number":850,"labels":[{"name":"fleet:sweep-cooldown"}]},
          {"number":851,"labels":[{"name":"fleet:sweep-cooldown"}]},
          {"number":852,"labels":[]}]' '[]' '{}'
FLEET_DISPATCH_ID=D5 "$FLEET_CLAIM" amending-claim 850 pool-2 >/dev/null 2>&1 || bad "amending-claim 850 failed"
assert_eq "$(removed 850 fleet:sweep-cooldown)" 1 "amending-claim removes the cooldown"
"$FLEET_CLAIM" resolving-claim 851 pool-4 >/dev/null 2>&1 || bad "resolving-claim 851 failed"
assert_eq "$(removed 851 fleet:sweep-cooldown)" 1 "resolving-claim removes the cooldown"
"$FLEET_CLAIM" resolving-claim 852 pool-5 >/dev/null 2>&1 || bad "resolving-claim 852 failed"
assert_absent "$(cat "$CALLS")" "852 --repo jakildev/IrredenEngine --remove-label fleet:sweep-cooldown" \
    "control: a PR without the label costs no removal call"

echo "=== 10. claim stamps its dispatch id into the FS lock ==="
fixture '[]' '[{"number":760,"labels":[]},{"number":761,"labels":[]}]' '{}'
FLEET_DISPATCH_ID=D8 "$FLEET_CLAIM" claim 760 pool-13 >/dev/null 2>&1 || bad "claim 760 failed"
assert_eq "$(cat "$FLEET_CLAIMS_DIR/760/dispatch_id" 2>/dev/null)" "D8" "the FS lock records the claiming dispatch"
FLEET_DISPATCH_ID=D9 "$FLEET_CLAIM" claim 760 pool-13 >/dev/null 2>&1 || true
assert_eq "$(cat "$FLEET_CLAIMS_DIR/760/dispatch_id" 2>/dev/null)" "D9" "the incumbent's re-acquire moves the claim to its new dispatch"
FLEET_DISPATCH_ID=preclaim "$FLEET_CLAIM" claim 760 pool-13 >/dev/null 2>&1 || true
assert_eq "$(cat "$FLEET_CLAIMS_DIR/760/dispatch_id" 2>/dev/null)" "D9" "the pre-claim sentinel never overwrites a real id"
env -u FLEET_DISPATCH_ID "$FLEET_CLAIM" claim 761 pool-14 >/dev/null 2>&1 || bad "claim 761 failed"
[[ ! -e "$FLEET_CLAIMS_DIR/761/dispatch_id" ]] && ok "no dispatch id stamps nothing" \
    || bad "an id-less claim wrote a dispatch_id file"

summarize "fleet-claim liveness sweep"
