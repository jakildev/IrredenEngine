#!/usr/bin/env bash
# Tests for fleet-triage-sweep (the architect-managed triage sweep).
#
# Hermetic: `gh` is a PATH stub serving fixtures, exiting 99 on any
# unexpected invocation (fails closed — no live GitHub). FLEET_HOME points
# at a temp dir so the prior-verdict annotation and the audit log never
# touch the real ~/.fleet.
#
# Covers:
#   - list: untriaged predicate picks exactly the label-less issues; an
#     issue with any fleet:/human: label never appears; oldest-first order
#   - list: a re-surfaced issue is annotated with its prior verdict
#   - apply: refuses (non-zero, no gh write) without human_confirmed
#   - apply: refuses when no entry is confirmed
#   - apply: refuses a confirmed entry whose labels are outside the allowlist
#   - apply --dry-run: emits the expected `gh issue edit --add-label` set,
#     no `gh issue close`, and reports the race-guard issue as skipped
#   - apply: recommend-close is reported as a ready-to-run line, never run
#   - --repo is required; dual-spelling (--repo= empty) rejected

set -uo pipefail

SCRIPT_DIR=$(cd "$(dirname "$0")/.." && pwd)
SWEEP="$SCRIPT_DIR/fleet-triage-sweep"
source "$(dirname "$0")/lib_assert.sh"

if [[ ! -x "$SWEEP" ]]; then
    echo "test setup: fleet-triage-sweep not found at $SWEEP" >&2
    exit 1
fi

TMP=$(mktemp -d "${TMPDIR:-/tmp}/test-triage-sweep.XXXXXX")
trap 'rm -rf "$TMP"' EXIT

# --- fixtures ---------------------------------------------------------------
#
# #301 untriaged, oldest. #302 untriaged, newer, and carries a prior verdict.
# #303 has a human: label, #304 a fleet: label — both must stay invisible.
# #305 models the race: the staging file has it as untriaged, but live it now
# carries human:owned, so `list` correctly omits it and `apply` skips it.
# Untriaged set is therefore {301, 302}.

cat > "$TMP/issues.json" << 'EOF'
[
  {"number": 302, "title": "idea: newer untriaged", "url": "u2",
   "createdAt": "2026-02-01T00:00:00Z", "labels": []},
  {"number": 301, "title": "idea: oldest untriaged", "url": "u1",
   "createdAt": "2026-01-01T00:00:00Z", "labels": []},
  {"number": 303, "title": "task: already approved", "url": "u3",
   "createdAt": "2026-01-15T00:00:00Z", "labels": [{"name": "human:approved"}]},
  {"number": 304, "title": "task: already queued", "url": "u4",
   "createdAt": "2026-01-20T00:00:00Z", "labels": [{"name": "fleet:queued"}]},
  {"number": 305, "title": "idea: raced", "url": "u5",
   "createdAt": "2026-03-01T00:00:00Z", "labels": [{"name": "human:owned"}]}
]
EOF

mkdir -p "$TMP/bin"
cat > "$TMP/bin/gh" << EOF
#!/usr/bin/env bash
fixtures="$TMP"
EOF
cat >> "$TMP/bin/gh" << 'EOF'
repo=""
prev=""
for arg in "$@"; do
    [[ "$prev" == "--repo" ]] && repo="$arg"
    prev="$arg"
done
case "$1 $2 $repo" in
    "issue list jakildev/IrredenEngine") cat "$fixtures/issues.json" ;;
    "issue edit jakildev/IrredenEngine") echo "$*" >> "$fixtures/gh-writes.log" ;;
    "issue close jakildev/IrredenEngine")
        echo "$*" >> "$fixtures/gh-closes.log" ;;
    *) echo "gh stub: unexpected invocation: $*" >&2; exit 99 ;;
esac
EOF
chmod +x "$TMP/bin/gh"

mkdir -p "$TMP/fleet-home/triage"

run_sweep() {
    PATH="$TMP/bin:$PATH" FLEET_HOME="$TMP/fleet-home" \
        "$SWEEP" "$@" > "$TMP/out.txt" 2> "$TMP/err.txt"
    echo $?
}

# --- list -------------------------------------------------------------------

status=$(run_sweep list --repo engine)
out=$(cat "$TMP/out.txt")

assert_eq "$status" "0" "list exits 0"
assert_contains "$out" "2 untriaged issue(s)" "untriaged count excludes every labeled issue"
assert_contains "$out" "#301" "label-less issue listed"
assert_contains "$out" "#302" "second label-less issue listed"
assert_absent  "$out" "#303" "human:-labeled issue never appears"
assert_absent  "$out" "#304" "fleet:-labeled issue never appears"

# oldest-first: #301 (2026-01-01) must precede #302 (2026-02-01)
order=$(grep -o '#30[0-9]' "$TMP/out.txt" | head -2 | tr '\n' ' ')
assert_eq "$order" "#301 #302 " "list is oldest-first"

# --- list: prior-verdict annotation -----------------------------------------

cat > "$TMP/fleet-home/triage/engine-sweep-2026-01-05.json" << 'EOF'
{
  "repo": "jakildev/IrredenEngine",
  "generated_at": "2026-01-05T00:00:00Z",
  "human_confirmed": true,
  "entries": [
    {"number": 302, "title": "idea: newer untriaged", "verdict": "park",
     "labels": ["human:owned"], "basis": "b", "confirmed": false}
  ]
}
EOF

status=$(run_sweep list --repo engine)
out=$(cat "$TMP/out.txt")
assert_eq "$status" "0" "list with prior staging exits 0"
assert_contains "$out" "prior verdict: park" "re-surfaced issue annotated with prior verdict"
assert_contains "$out" "re-surfaced" "re-surfaced marker rendered"

# --- apply: unconfirmed file refused ----------------------------------------

: > "$TMP/gh-writes.log"
cat > "$TMP/unconfirmed.json" << 'EOF'
{
  "repo": "jakildev/IrredenEngine",
  "generated_at": "2026-07-30T00:00:00Z",
  "human_confirmed": false,
  "entries": [
    {"number": 301, "verdict": "recommend-approve",
     "labels": ["human:approved", "fleet:sonnet"], "confirmed": true}
  ]
}
EOF

status=$(run_sweep apply --repo engine "$TMP/unconfirmed.json")
err=$(cat "$TMP/err.txt")
assert_eq "$status" "1" "apply without human_confirmed exits non-zero"
assert_contains "$err" "human_confirmed" "refusal names the missing marker"
assert_eq "$(wc -l < "$TMP/gh-writes.log" | tr -d ' ')" "0" "unconfirmed apply issues no gh write"

# --- apply: confirmed at top level but no entry confirmed -------------------

cat > "$TMP/none-confirmed.json" << 'EOF'
{
  "repo": "jakildev/IrredenEngine",
  "human_confirmed": true,
  "entries": [
    {"number": 301, "verdict": "park", "labels": ["human:owned"], "confirmed": false}
  ]
}
EOF

status=$(run_sweep apply --repo engine "$TMP/none-confirmed.json")
err=$(cat "$TMP/err.txt")
assert_eq "$status" "1" "apply with no confirmed entry exits non-zero"
assert_contains "$err" "no entry is marked confirmed" "refusal names the empty confirmed set"
assert_eq "$(wc -l < "$TMP/gh-writes.log" | tr -d ' ')" "0" "still no gh write"

# --- apply: label outside the allowlist refused -----------------------------

cat > "$TMP/bad-label.json" << 'EOF'
{
  "repo": "jakildev/IrredenEngine",
  "human_confirmed": true,
  "entries": [
    {"number": 301, "verdict": "recommend-approve",
     "labels": ["human:approved", "fleet:queued"], "confirmed": true}
  ]
}
EOF

status=$(run_sweep apply --repo engine "$TMP/bad-label.json")
out=$(cat "$TMP/out.txt")
err=$(cat "$TMP/err.txt")
assert_eq "$status" "1" "apply refuses a scout-owned label"
assert_contains "$out" "outside the apply allowlist" "rejection names the allowlist"
assert_contains "$out" "fleet:queued" "rejection names the offending label"
assert_contains "$err" "refusing" "refusal reaches stderr"
assert_eq "$(wc -l < "$TMP/gh-writes.log" | tr -d ' ')" "0" "allowlist violation issues no gh write"

# --- apply --dry-run: expected command set, race guard, no close ------------

cat > "$TMP/confirmed.json" << 'EOF'
{
  "repo": "jakildev/IrredenEngine",
  "human_confirmed": true,
  "entries": [
    {"number": 301, "verdict": "recommend-approve",
     "labels": ["human:approved", "fleet:sonnet"], "confirmed": true},
    {"number": 305, "verdict": "recommend-approve",
     "labels": ["human:approved", "fleet:opus"], "confirmed": true},
    {"number": 302, "verdict": "recommend-close",
     "labels": ["human:owned"], "confirmed": true}
  ]
}
EOF

: > "$TMP/gh-closes.log"
status=$(run_sweep apply --repo engine "$TMP/confirmed.json" --dry-run)
out=$(cat "$TMP/out.txt")

assert_eq "$status" "0" "confirmed dry-run exits 0"
assert_contains "$out" "would run: gh issue edit 301 --repo jakildev/IrredenEngine --add-label human:approved --add-label fleet:sonnet" \
    "dry-run emits the exact add-label command for the clean entry"
assert_contains "$out" "skipped   #305" "race-guard issue reported skipped"
assert_contains "$out" "gained human:owned since staging" "skip reason names the label gained since staging"
assert_absent  "$out" "would run: gh issue edit 305" "raced issue is not in the command set"
assert_contains "$out" "2 issue(s) would be labeled, 0 closed" "dry-run summary counts only appliable entries"
assert_absent  "$out" "gh issue close 305" "no close line for a non-close verdict"
assert_contains "$out" "gh issue close 302" "recommend-close reported as a ready-to-run line"
assert_eq "$(wc -l < "$TMP/gh-writes.log" | tr -d ' ')" "0" "dry-run issues no gh write"
assert_eq "$(wc -l < "$TMP/gh-closes.log" | tr -d ' ')" "0" "sweep never calls gh issue close"

# --- apply --dry-run touches no filesystem state ----------------------------
# A read-only path must not create ~/.fleet/triage/ as a side effect.

mkdir -p "$TMP/pristine-home"
PATH="$TMP/bin:$PATH" FLEET_HOME="$TMP/pristine-home" \
    "$SWEEP" apply --repo engine "$TMP/confirmed.json" --dry-run > /dev/null 2>&1
if [[ -e "$TMP/pristine-home/triage" ]]; then
    bad "dry-run creates no ~/.fleet/triage state dir"
else
    ok "dry-run creates no ~/.fleet/triage state dir"
fi

# --- apply (live): writes labels, logs audit, still never closes ------------

status=$(run_sweep apply --repo engine "$TMP/confirmed.json")
out=$(cat "$TMP/out.txt")
writes=$(cat "$TMP/gh-writes.log")

assert_eq "$status" "0" "confirmed apply exits 0"
assert_contains "$writes" "issue edit 301 --repo jakildev/IrredenEngine --add-label human:approved --add-label fleet:sonnet" \
    "apply writes the staged label set"
assert_absent  "$writes" "issue edit 305" "race-guarded issue never written"
assert_contains "$out" "2 issue(s) labeled, 0 closed" "apply summary"
assert_eq "$(wc -l < "$TMP/gh-closes.log" | tr -d ' ')" "0" "apply never calls gh issue close"
assert_contains "$(cat "$TMP/fleet-home/triage/log.jsonl")" '"issue":301' "audit log records the applied issue"

# --- arg handling -----------------------------------------------------------

status=$(run_sweep list)
err=$(cat "$TMP/err.txt")
assert_eq "$status" "1" "--repo is required"
assert_contains "$err" "--repo is required" "missing --repo explained"

status=$(run_sweep list --repo=)
assert_eq "$status" "1" "empty --repo= rejected (dual-spelling rule)"

status=$(run_sweep list --repo=engine)
assert_eq "$status" "0" "--repo=engine equals-form accepted"

status=$(run_sweep bogus --repo engine)
assert_eq "$status" "1" "unknown subcommand rejected"

status=$(run_sweep apply --repo engine)
err=$(cat "$TMP/err.txt")
assert_eq "$status" "1" "apply without a staging file rejected"
assert_contains "$err" "staging file argument is required" "missing staging file explained"

status=$(run_sweep apply --repo engine "$TMP/nonexistent.json")
assert_eq "$status" "1" "apply with a missing staging file rejected"

summarize "fleet-triage-sweep tests"
