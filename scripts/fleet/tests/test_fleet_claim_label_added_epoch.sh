#!/usr/bin/env bash
# Tests for fleet-claim's label_added_epoch() — the timestamp lookup every
# claim-label TTL sweep ages its labels against.
#
# Regression coverage for #2781: the lookup passed jq's `--arg lname` to
# `gh api`, which does not accept it. Real gh exits 1 on the unknown flag
# BEFORE issuing the request; the call site's `2>/dev/null` swallowed the
# usage text, so the function fell through to its "unknown age" guard and
# returned 0 on every call, on every host, always. Each caller reads 0 as
# "can't tell how old this is" and skips, so all five cleanup --gh sweeps,
# the acquisition force-sweep, and reconcile R4a were silently inert.
#
# Why the sibling claim suites could not catch it: their gh stubs match the
# substring `events` anywhere in argv and ignore flags entirely (see
# test_fleet_claim_prlabel_orphan_sweep.sh), so a flag rejection is
# unrepresentable. The stub here therefore models `gh api`'s FLAG PARSING —
# it validates against gh api's real accepted flag set and fails the way gh
# fails — and evaluates the --jq program against real events JSON rather
# than pretending the filter already ran. T6 is the positive control on that
# fidelity: it asserts the stub still rejects the flag the bug used, so a
# future stub rewrite can't quietly turn this suite into a no-op.

set -uo pipefail

SCRIPT_DIR=$(cd "$(dirname "$0")/.." && pwd)
FLEET_CLAIM="$SCRIPT_DIR/fleet-claim"
[[ -x "$FLEET_CLAIM" ]] || { echo "test setup: fleet-claim not found at $FLEET_CLAIM" >&2; exit 1; }

source "$(dirname "$0")/lib_assert.sh"

TMPROOT=""; cleanup(){ [[ -n "$TMPROOT" && -d "$TMPROOT" ]] && rm -rf "$TMPROOT"; }
trap cleanup EXIT
TMPROOT=$(mktemp -d)

export FLEET_CLAIMS_DIR="$TMPROOT/claims"
export FLEET_STATE_DIR="$TMPROOT/state"
export FLEET_ORPHANS_DIR="$TMPROOT/orphans"
mkdir -p "$FLEET_CLAIMS_DIR" "$FLEET_STATE_DIR" "$FLEET_ORPHANS_DIR"

TARGET_LABEL="fleet:reviewing-mac-pool-8"
OTHER_LABEL="fleet:approved"
TS_FIRST="2026-07-20T01:57:21Z"
TS_LAST="2026-07-30T17:12:40Z"
TS_OTHER="2026-08-01T09:53:23Z"   # later than TS_LAST, but a DIFFERENT label

epoch_of() { ADDED_AT="$1" python3 -c '
import os, datetime
dt = datetime.datetime.strptime(os.environ["ADDED_AT"], "%Y-%m-%dT%H:%M:%SZ")
print(int(dt.replace(tzinfo=datetime.timezone.utc).timestamp()))
'; }

export EVENTS_JSON="$TMPROOT/events.json"
cat > "$EVENTS_JSON" <<JSON
[
  {"event":"labeled","label":{"name":"$TARGET_LABEL"},"created_at":"$TS_FIRST"},
  {"event":"unlabeled","label":{"name":"$TARGET_LABEL"},"created_at":"$TS_OTHER"},
  {"event":"labeled","label":{"name":"$TARGET_LABEL"},"created_at":"$TS_LAST"},
  {"event":"labeled","label":{"name":"$OTHER_LABEL"},"created_at":"$TS_OTHER"}
]
JSON

# --- gh stub: models `gh api` flag parsing + jq evaluation -----------------
# Accepted-flag set is transcribed from `gh api --help` (gh 2.89.0), NOT from
# what fleet-claim happens to pass — that is what keeps this a real check.
# Anything else exits 1 with gh's own "unknown flag" shape and no stdout.
# The --jq evaluator covers exactly the filter shapes this call site uses and
# fails closed (exit 3, diagnostic on stderr) on anything it does not model,
# so a rewritten filter can never silently read as a pass.
STUB_DIR="$TMPROOT/bin"; mkdir -p "$STUB_DIR"
cat > "$STUB_DIR/gh" <<'GHSTUB'
#!/usr/bin/env bash
[[ "${1:-}" == "api" ]] || exit 0
shift
endpoint=""; jqprog=""
while [[ $# -gt 0 ]]; do
    case "$1" in
        --paginate|--silent|--slurp|--verbose|-i|--include) shift ;;
        --jq|-q)   jqprog="$2"; shift 2 ;;
        --jq=*)    jqprog="${1#--jq=}"; shift ;;
        -q=*)      jqprog="${1#-q=}"; shift ;;
        --method|-X|--header|-H|--field|-F|--raw-field|-f|--input|\
        --hostname|--cache|--template|-t|--preview|-p) shift 2 ;;
        --method=*|--header=*|--field=*|--raw-field=*|--input=*|\
        --hostname=*|--cache=*|--template=*|--preview=*) shift ;;
        -*)
            printf 'unknown flag: %s\n\nUsage:  gh api <endpoint> [flags]\n' "$1" >&2
            exit 1 ;;
        *) endpoint="$1"; shift ;;
    esac
done
[[ -n "$endpoint" ]] || { echo 'gh: no endpoint' >&2; exit 1; }
JQPROG="$jqprog" python3 - "$EVENTS_JSON" <<'PY'
import json, os, re, sys

prog = os.environ.get("JQPROG", "")
events = json.load(open(sys.argv[1]))
if not prog:
    print(json.dumps(events)); raise SystemExit(0)

# Models: .[] | select(.event=="labeled" and .label.name==<EXPR>) | .created_at
m = re.fullmatch(
    r'\s*\.\[\]\s*\|\s*select\(\s*\.event\s*==\s*"(?P<ev>[^"]*)"\s+and\s+'
    r'\.label\.name\s*==\s*(?P<expr>\S+?)\s*\)\s*\|\s*\.(?P<field>\w+)\s*',
    prog)
if not m:
    print("stub jq: unmodelled program: %r" % prog, file=sys.stderr)
    raise SystemExit(3)

expr = m.group("expr")
if expr.startswith('"') and expr.endswith('"'):
    want = expr[1:-1]
elif expr.startswith("env."):
    want = os.environ.get(expr[4:], "")      # jq: unset env key -> null, never matches
    if expr[4:] not in os.environ:
        want = None
elif expr.startswith("$ENV."):
    want = os.environ.get(expr[5:])
elif expr.startswith("$"):
    # A jq variable with no --arg binding: real jq refuses to compile.
    print("jq: error: %s is not defined" % expr, file=sys.stderr)
    raise SystemExit(1)
else:
    print("stub jq: unmodelled name expression: %r" % expr, file=sys.stderr)
    raise SystemExit(3)

for e in events:
    if e.get("event") == m.group("ev") and want is not None \
       and e.get("label", {}).get("name") == want:
        print(e[m.group("field")])
PY
GHSTUB
chmod +x "$STUB_DIR/gh"
export PATH="$STUB_DIR:$PATH"

# A PATH with the coreutils label_added_epoch needs but no `gh` (T5). Emptying
# PATH outright would hide `tail`/`python3` too and test the wrong thing.
NOGH_DIR="$TMPROOT/nogh-bin"; mkdir -p "$NOGH_DIR"
for tool in tail python3; do ln -s "$(command -v "$tool")" "$NOGH_DIR/$tool"; done

# Source fleet-claim as a library (defines helpers, skips the dispatch). Clear
# positional args first so the script's top-level --repo parse is a no-op.
set --
FLEET_CLAIM_LIB=1 source "$FLEET_CLAIM"
# fleet-claim's header sets -e; this suite deliberately runs commands that
# exit non-zero (T5's missing gh, T6's rejected flag), so restore our own.
set +e
set -uo pipefail

REPO="jakildev/IrredenEngine"

echo "== label_added_epoch =="

# T1: the regression. A label that IS present must resolve to a real epoch.
# Pre-fix this is 0, because the stub rejects `--arg` exactly as gh does.
echo "T1: present label -> the epoch of its most recent 'labeled' event"
assert_eq "$(label_added_epoch "$REPO" 2393 "$TARGET_LABEL")" "$(epoch_of "$TS_LAST")" \
    "resolves a non-zero epoch for a label that is actually present"

# T2: the filter is genuinely applied — a LATER event for a different label
# must not leak through. Catches both "filter dropped" and "env name wrong".
echo "T2: a later event for a different label does not leak"
assert_absent "$(label_added_epoch "$REPO" 2393 "$TARGET_LABEL")" "$(epoch_of "$TS_OTHER")" \
    "does not return another label's (later) timestamp"

# T3: only 'labeled' events count — the unlabeled event at TS_OTHER is skipped
# above, which T1 already pins; here the second label is queried directly.
echo "T3: a different present label resolves to its own epoch"
assert_eq "$(label_added_epoch "$REPO" 2393 "$OTHER_LABEL")" "$(epoch_of "$TS_OTHER")" \
    "each label ages against its own 'labeled' event"

# T4: genuinely-unknown age still reports 0 (the documented contract the
# callers' `-eq 0 && continue` guards rely on).
echo "T4: absent label -> 0"
assert_eq "$(label_added_epoch "$REPO" 2393 "fleet:never-applied")" "0" \
    "a label with no 'labeled' event reports unknown age (0)"

# T5: no gh on PATH -> 0.
echo "T5: gh missing -> 0"
assert_eq "$(PATH="$NOGH_DIR" label_added_epoch "$REPO" 2393 "$TARGET_LABEL")" "0" \
    "no gh binary reports unknown age (0)"

# T6: positive control on the stub itself. If a future edit relaxes the stub's
# flag parsing, T1-T5 would pass no matter what fleet-claim sends — this keeps
# the suite from decaying into the same blind spot it exists to close.
echo "T6: stub fidelity — gh api rejects jq-only flags"
stub_rc=0
stub_out=$(gh api "repos/$REPO/issues/2393/events" --paginate \
    --arg lname "$TARGET_LABEL" \
    --jq '.[] | select(.event=="labeled" and .label.name==$lname) | .created_at' 2>&1) \
    || stub_rc=$?
assert_eq "$stub_rc" "1" "stub exits 1 on the unknown flag, as gh api does"
assert_contains "$stub_out" "unknown flag: --arg" "stub reports the unknown flag"

echo "T7: stub fidelity — an accepted-flag call still returns data"
stub_ok=$(FLEET_LABEL_NAME="$TARGET_LABEL" gh api "repos/$REPO/issues/2393/events" --paginate \
    --jq '.[] | select(.event=="labeled" and .label.name==env.FLEET_LABEL_NAME) | .created_at' 2>&1)
assert_contains "$stub_ok" "$TS_LAST" "stub serves events when every flag is accepted"

summarize "fleet-claim label_added_epoch tests"
