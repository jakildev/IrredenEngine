#!/usr/bin/env bash
# Tests for reconcile R10: this host's fleet:amending-* label whose owner is
# gone. The amend claim consumed the PR's feedback tier, so a dead owner's
# label is the only mark left on the PR and every lane reads it as live.
#
#   report-only  names the PR and label when the shared liveness verdict is
#                CONFIRMED DEAD, or CANNOT VOUCH with no ownership record
#                younger than the TTL; stays quiet for a live dispatch record,
#                an abandon pin, a fresh ownership record, and another host's
#                label
#   --apply      removes it through cleanup's locked sweep, so the label-age
#                rule and fleet:sweep-cooldown are the sweep's own: a label
#                younger than the TTL is kept even when reported
#
# gh is a PATH stub answering from canned JSON (label ages from the events
# endpoint), logging every call; liveness signals are real files under a
# sandboxed HOME.

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

unset FLEET_DISPATCH_ID FLEET_ROLE_MODEL FLEET_DISPATCH_TARGET FLEET_DISPATCH_KIND \
    FLEET_CLAIM_STALE_SECS FLEET_CLAIM_CROSSHOST_STALE_SECS FLEET_AMEND_SNAPSHOTS_DIR
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

export STUB_DATA="$TMPROOT/data"
mkdir -p "$STUB_DATA"
CALLS="$STUB_DATA/calls.log"
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
if argv[:2] == ["pr", "list"]:
    print(json.dumps(load("prs.json", [])))
elif argv[:2] == ["issue", "list"]:
    print("[]")
elif argv[:2] == ["issue", "edit"]:
    pass
elif argv[:1] == ["api"] and "/events" in argv[1]:
    n = argv[1].split("/issues/")[1].split("/")[0]
    age = load("ages.json", {}).get(f"{n}|{os.environ.get('FLEET_LABEL_NAME', '')}")
    if age is not None:
        print(time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime(time.time() - int(age))))
elif argv[:1] == ["repo"]:
    sys.exit(1)
else:
    print(f"gh stub: unmodelled call: {' '.join(argv)}", file=sys.stderr)
    sys.exit(64)
PY
cat > "$STUB_BIN/gh" <<'GH'
#!/usr/bin/env bash
exec python3 "$(dirname "$0")/gh_stub.py" "$@"
GH
chmod +x "$STUB_BIN/gh"
export PATH="$STUB_BIN:$PATH"

NOW=$(date +%s)
LABEL="fleet:amending-mac-pool-2"

# Fresh host-local surfaces; PR 3763 carries <label>, aged <label-age> seconds.
arm() {  # <label> <label-age>
    rm -rf "$FLEET_CLAIMS_DIR" "$FLEET_RESERVATIONS_DIR" "$FLEET_HEARTBEATS_DIR" \
        "$FLEET_STATE_DIR" "$FLEET_ORPHANS_DIR" "$SNAPS"
    mkdir -p "$FLEET_CLAIMS_DIR" "$FLEET_RESERVATIONS_DIR" "$FLEET_HEARTBEATS_DIR" \
        "$FLEET_STATE_DIR/dispatch" "$FLEET_STATE_DIR/dispatch-current" "$FLEET_ORPHANS_DIR" "$SNAPS"
    printf '[{"number":3763,"headRefName":"claude/3676-thing","body":"Closes #3676","labels":[{"name":"%s"}]}]\n' \
        "$1" > "$STUB_DATA/prs.json"
    printf '{"3763|%s":%s}\n' "$1" "$2" > "$STUB_DATA/ages.json"
    : > "$CALLS"
}
snap() {  # <agent> <dispatch-id> <acquired-age>
    printf '{"pr":3763,"agent":"%s","acquired_epoch":%s,"dispatch_id":"%s"}\n' \
        "$1" "$(( NOW - $3 ))" "$2" > "$SNAPS/3763.json"
}
current() { printf '%s\n' "$2" > "$FLEET_STATE_DIR/dispatch-current/$1"; }
reconcile() { "$FLEET_CLAIM" reconcile "$@" --repo jakildev/IrredenEngine 2>&1 || true; }
removed() { grep -cxF -- "issue edit 3763 --repo jakildev/IrredenEngine --remove-label $1" "$CALLS" || true; }
added()   { grep -cxF -- "issue edit 3763 --repo jakildev/IrredenEngine --add-label $1" "$CALLS" || true; }
R10_LINE="[R10 fix ] jakildev/IrredenEngine pr#3763: this host's '$LABEL' has no live owner"

echo "=== 1. a superseded owner's label is reported, then removed on --apply ==="
arm "$LABEL" 30000
snap pool-2 D1 30000; current pool-2 D2
out=$(reconcile)
assert_contains "$out" "$R10_LINE" "report-only names the PR and the label"
assert_eq "$(removed "$LABEL")" 0 "report-only removes nothing"
: > "$CALLS"; out=$(reconcile --apply)
assert_contains "$out" "R10 removed '$LABEL' on jakildev/IrredenEngine PR#3763" "--apply reports the removal"
assert_eq "$(removed "$LABEL")" 1 "--apply removes the label"
assert_eq "$(added fleet:sweep-cooldown)" 0 "a confirmed-dead owner earns no cooldown"

echo "=== 2. an owner nothing vouches for past the TTL is removed, with the sweep's cooldown ==="
# The stranding shape: the dead session's id is still current (no later
# dispatch superseded it), its heartbeat is stale, and the claim is hours old.
arm "$LABEL" 30000
snap pool-2 D1 30000; current pool-2 D1
out=$(reconcile)
assert_contains "$out" "$R10_LINE" "report-only names it"
: > "$CALLS"; reconcile --apply >/dev/null
assert_eq "$(removed "$LABEL")" 1 "--apply removes the label"
assert_eq "$(added fleet:sweep-cooldown)" 1 "an age-only removal stamps fleet:sweep-cooldown"

echo "=== 3. control: a live dispatch record for the PR keeps it ==="
arm "$LABEL" 30000
snap pool-2 D1 30000; current pool-2 D2
printf '{"role":"worker","pane":"%%2","target":"feedback:engine:3763","agent":"pool-2"}\n' \
    > "$FLEET_STATE_DIR/dispatch/pane-2.json"
out=$(reconcile --apply)
assert_absent "$out" "[R10" "no R10 finding"
assert_eq "$(removed "$LABEL")" 0 "nothing removed"

echo "=== 4. control: an abandon pin on the PR keeps it ==="
arm "$LABEL" 30000
snap pool-2 D1 30000; current pool-2 D1
mkdir -p "$FLEET_STATE_DIR/abandon-pin"
printf 'feedback:engine:3763\n%s\nworker\n' "$NOW" > "$FLEET_STATE_DIR/abandon-pin/pool-2"
out=$(reconcile --apply)
assert_absent "$out" "[R10" "no R10 finding while the pane is held for its resume"
assert_eq "$(removed "$LABEL")" 0 "nothing removed"

echo "=== 5. control: another host's label is never judged here ==="
arm "fleet:amending-linux-pool-2" 90000
out=$(reconcile --apply)
assert_absent "$out" "[R10" "no R10 finding for a foreign-host label"
assert_eq "$(removed fleet:amending-linux-pool-2)" 0 "nothing removed"

echo "=== 6. control: an unvouched claim younger than the TTL is not reported ==="
arm "$LABEL" 60
snap pool-2 D1 60; current pool-2 D1
out=$(reconcile)
assert_absent "$out" "[R10" "a 1-minute-old ownership record is still in its TTL"

echo "=== 7. the locked re-judge has the last word: a reported label younger than the TTL stays ==="
# No ownership record, so the classifier cannot age the claim and reports it;
# the sweep reads the label's real age and keeps it.
arm "$LABEL" 600
out=$(reconcile --apply)
assert_contains "$out" "$R10_LINE" "reported without an ownership record"
assert_contains "$out" "R10 kept '$LABEL'" "--apply defers to the sweep's age rule"
assert_eq "$(removed "$LABEL")" 0 "nothing removed"

summarize "reconcile R10 dead amending label"
