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
#   - apply: a graduated-class entry (class + complete bar) stamps
#     fleet:agent-approved plus the model label; an incomplete bar, a
#     direction entry or a verdict other than exactly recommend-approve
#     refuses the whole apply with no gh write; a park entry
#     stages human:owned; the audit line carries the class and the mode
#   - apply: labels_at_staging narrows the race guard to labels gained beyond
#     the staged set
#   - stamp: a complete graduated comment posts and stamps; direction and an
#     incomplete bar recommend; an issue already carrying a verdict label is
#     skipped with no write; --dry-run writes nothing; --comment-file required
#   - --repo is required; dual-spelling (--repo= empty) rejected

set -uo pipefail

SCRIPT_DIR=$(cd "$(dirname "$0")/.." && pwd)
source "$(dirname "$0")/lib_preflight.sh"
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
# issue 301 untriaged, oldest. issue 302 untriaged, newer, and carries a prior verdict.
# issue 303 has a human: label, issue 304 a fleet: label — both must stay invisible.
# issue 305 models the race: the staging file has it as untriaged, but live it now
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
    "issue view jakildev/IrredenEngine") cat "$fixtures/view-$3.json" ;;
    "issue comment jakildev/IrredenEngine") echo "$*" >> "$fixtures/gh-comments.log" ;;
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

# oldest-first: issue 301 (2026-01-01) must precede issue 302 (2026-02-01)
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
assert_contains "$(cat "$TMP/fleet-home/triage/log.jsonl")" '"labels":["human:approved", "fleet:sonnet"]' \
    "audit log stores labels as a JSON array, not a CLI-flag string"
if python3 -c '
import json, sys
with open(sys.argv[1], encoding="utf-8") as f:
    for line in f:
        entry = json.loads(line)
        assert isinstance(entry["labels"], list), entry
' "$TMP/fleet-home/triage/log.jsonl" 2>/dev/null; then
    ok "every audit-log line parses as JSON with a list-valued labels field"
else
    bad "every audit-log line parses as JSON with a list-valued labels field"
fi

# --- apply: a graduated-class entry stamps fleet:agent-approved -------------

cat > "$TMP/graduated.json" << 'EOF'
{
  "repo": "jakildev/IrredenEngine",
  "human_confirmed": true,
  "entries": [
    {"number": 301, "verdict": "recommend-approve", "class": "rebless",
     "bar": {"measurement": "14/14 rows match, max delta 0 on macos-debug",
             "host": "macos", "confinement": "one framebuffer pixel (criterion 2)"},
     "labels": ["fleet:agent-approved", "fleet:sonnet"], "confirmed": true},
    {"number": 302, "verdict": "park", "basis": "owned by the open PR #999",
     "labels": ["human:owned"], "confirmed": true}
  ]
}
EOF
: > "$TMP/gh-writes.log"
status=$(run_sweep apply --repo engine "$TMP/graduated.json")
writes=$(cat "$TMP/gh-writes.log")
assert_eq "$status" "0" "graduated apply exits 0"
assert_contains "$writes" "issue edit 301 --repo jakildev/IrredenEngine --add-label fleet:agent-approved --add-label fleet:sonnet" \
    "a complete rebless entry stamps fleet:agent-approved plus the model label"
assert_contains "$writes" "issue edit 302 --repo jakildev/IrredenEngine --add-label human:owned" \
    "a park entry naming its owner stamps human:owned"
assert_contains "$(tail -2 "$TMP/fleet-home/triage/log.jsonl")" '"class":"rebless"' "audit log records the class"
assert_contains "$(tail -2 "$TMP/fleet-home/triage/log.jsonl")" '"mode":"apply"' "audit log records the mode"
assert_contains "$(tail -1 "$TMP/fleet-home/triage/log.jsonl")" '"ts":"20' "audit log records a timestamp"

cat > "$TMP/incomplete.json" << 'EOF'
{
  "repo": "jakildev/IrredenEngine",
  "human_confirmed": true,
  "entries": [
    {"number": 301, "verdict": "recommend-approve", "class": "docs-capture",
     "bar": {"ruling": "https://example.invalid/issues/1#issuecomment-2"},
     "labels": ["fleet:agent-approved", "fleet:sonnet"], "confirmed": true}
  ]
}
EOF
: > "$TMP/gh-writes.log"
status=$(run_sweep apply --repo engine "$TMP/incomplete.json")
out=$(cat "$TMP/out.txt")
err=$(cat "$TMP/err.txt")
assert_eq "$status" "1" "apply refuses a graduated entry with an incomplete bar"
assert_contains "$out" "missing grep" "refusal names the missing bar key"
assert_contains "$err" "refusing" "bar refusal reaches stderr"
assert_eq "$(wc -l < "$TMP/gh-writes.log" | tr -d ' ')" "0" "incomplete bar issues no gh write"

cat > "$TMP/direction.json" << 'EOF'
{
  "repo": "jakildev/IrredenEngine",
  "human_confirmed": true,
  "entries": [
    {"number": 301, "verdict": "recommend-approve", "class": "direction",
     "labels": ["fleet:agent-approved", "fleet:opus"], "confirmed": true}
  ]
}
EOF
status=$(run_sweep apply --repo engine "$TMP/direction.json")
out=$(cat "$TMP/out.txt")
assert_eq "$status" "1" "apply refuses fleet:agent-approved on a direction entry"
assert_contains "$out" "recommend-only" "refusal says direction stays recommend-only"
assert_eq "$(wc -l < "$TMP/gh-writes.log" | tr -d ' ')" "0" "direction entry issues no gh write"

cat > "$TMP/suffixed-verdict.json" << 'EOF'
{
  "repo": "jakildev/IrredenEngine",
  "human_confirmed": true,
  "entries": [
    {"number": 301, "verdict": "recommend-approve-extra", "class": "rebless",
     "bar": {"measurement": "14/14 rows match, max delta 0 on macos-debug",
             "host": "macos", "confinement": "one framebuffer pixel (criterion 2)"},
     "labels": ["fleet:agent-approved", "fleet:sonnet"], "confirmed": true}
  ]
}
EOF
status=$(run_sweep apply --repo engine "$TMP/suffixed-verdict.json")
out=$(cat "$TMP/out.txt")
assert_eq "$status" "1" "apply refuses fleet:agent-approved on a suffixed verdict"
assert_contains "$out" "needs verdict exactly recommend-approve" "refusal names the exact verdict"
assert_eq "$(wc -l < "$TMP/gh-writes.log" | tr -d ' ')" "0" "suffixed verdict issues no gh write"

# --- apply: labels_at_staging narrows the race guard -------------------------

cat > "$TMP/staged-with-labels.json" << 'EOF'
{
  "repo": "jakildev/IrredenEngine",
  "human_confirmed": true,
  "entries": [
    {"number": 304, "verdict": "recommend-approve", "labels_at_staging": ["fleet:queued"],
     "labels": ["human:approved", "fleet:sonnet"], "confirmed": true},
    {"number": 305, "verdict": "recommend-approve", "labels_at_staging": ["fleet:task"],
     "labels": ["human:approved", "fleet:sonnet"], "confirmed": true}
  ]
}
EOF
status=$(run_sweep apply --repo engine "$TMP/staged-with-labels.json" --dry-run)
out=$(cat "$TMP/out.txt")
assert_eq "$status" "0" "dry-run with labels_at_staging exits 0"
assert_contains "$out" "would run: gh issue edit 304" "an issue staged with its fleet:queued recorded is applied"
assert_contains "$out" "skipped   #305" "an issue that gained a label beyond its staged set is skipped"
assert_contains "$out" "gained human:owned since staging" "the skip names the label gained"

# --- stamp: the dispatched role's write path ---------------------------------

echo '{"labels": []}' > "$TMP/view-301.json"
echo '{"labels": []}' > "$TMP/view-306.json"
echo '{"labels": [{"name": "fleet:triage-recommend"}]}' > "$TMP/view-307.json"
cat > "$TMP/triage-rebless.md" << 'EOF'
## Triage

**Verdict:** recommend-approve
**Class:** rebless
**Objective:** (none — defect-shaped)
**Suggested model:** sonnet
**Bar:**
- measurement: 14/14 rows match, max delta 0, PSNR inf on macos-debug
- host: macos
- confinement: one framebuffer pixel (criterion 2)
**Basis:** the six rows moved by whole pixels after the scatter fix merged.
EOF
cat > "$TMP/triage-direction.md" << 'EOF'
## Triage

**Verdict:** recommend-approve
**Class:** direction
**Objective:** (none — see verdict)
**Suggested model:** opus
**Basis:** a public API addition; the owner decides.
EOF
cat > "$TMP/triage-short.md" << 'EOF'
## Triage

**Verdict:** recommend-approve
**Class:** plan-residual
**Suggested model:** sonnet
**Bar:**
- plan: https://example.invalid/issues/9#issuecomment-1
**Basis:** carved out of the plan.
EOF

: > "$TMP/gh-writes.log"; : > "$TMP/gh-comments.log"
status=$(run_sweep stamp --repo engine 306 --comment-file "$TMP/triage-rebless.md" --dry-run)
out=$(cat "$TMP/out.txt")
assert_eq "$status" "0" "stamp --dry-run exits 0"
assert_contains "$out" "would run: gh issue edit 306 --repo jakildev/IrredenEngine --add-label fleet:agent-approved --add-label fleet:sonnet" \
    "dry-run shows the graduated label set"
assert_eq "$(wc -l < "$TMP/gh-writes.log" | tr -d ' ')" "0" "stamp dry-run issues no gh write"
assert_eq "$(wc -l < "$TMP/gh-comments.log" | tr -d ' ')" "0" "stamp dry-run posts no comment"

status=$(run_sweep stamp --repo engine 306 --comment-file "$TMP/triage-rebless.md")
out=$(cat "$TMP/out.txt")
writes=$(cat "$TMP/gh-writes.log")
comments=$(cat "$TMP/gh-comments.log")
assert_eq "$status" "0" "graduated stamp exits 0"
assert_contains "$comments" "issue comment 306 --repo jakildev/IrredenEngine --body-file" "stamp posts the comment"
assert_contains "$writes" "issue edit 306 --repo jakildev/IrredenEngine --add-label fleet:agent-approved --add-label fleet:sonnet" \
    "a complete rebless comment stamps fleet:agent-approved plus the model label"
assert_contains "$out" "stamp     #306" "stamp reports the graduated verb"
assert_contains "$(tail -1 "$TMP/fleet-home/triage/log.jsonl")" '"mode":"stamp"' "audit log records the stamp"
assert_contains "$(tail -1 "$TMP/fleet-home/triage/log.jsonl")" '"class":"rebless"' "audit log records the stamped class"

: > "$TMP/gh-writes.log"
status=$(run_sweep stamp --repo engine 301 --comment-file "$TMP/triage-direction.md")
out=$(cat "$TMP/out.txt")
writes=$(cat "$TMP/gh-writes.log")
assert_eq "$status" "0" "direction stamp exits 0"
assert_contains "$writes" "issue edit 301 --repo jakildev/IrredenEngine --add-label fleet:triage-recommend" "a direction verdict recommends"
assert_absent  "$writes" "fleet:agent-approved" "a direction verdict never stamps approval"
assert_contains "$out" "recommend-only" "the report says why"

: > "$TMP/gh-writes.log"
status=$(run_sweep stamp --repo engine 301 --comment-file "$TMP/triage-short.md")
out=$(cat "$TMP/out.txt")
writes=$(cat "$TMP/gh-writes.log")
assert_eq "$status" "0" "incomplete-bar stamp exits 0"
assert_contains "$writes" "issue edit 301 --repo jakildev/IrredenEngine --add-label fleet:triage-recommend" "an incomplete bar recommends"
assert_contains "$out" "missing decision" "the report names the missing bar key"

: > "$TMP/gh-writes.log"; : > "$TMP/gh-comments.log"
status=$(run_sweep stamp --repo engine 307 --comment-file "$TMP/triage-rebless.md")
out=$(cat "$TMP/out.txt")
assert_eq "$status" "0" "stamp on an already-triaged issue exits 0"
assert_contains "$out" "skipped   #307: already carries fleet:triage-recommend" "the idempotency guard names the label held"
assert_eq "$(wc -l < "$TMP/gh-writes.log" | tr -d ' ')" "0" "no label write on a held issue"
assert_eq "$(wc -l < "$TMP/gh-comments.log" | tr -d ' ')" "0" "no comment on a held issue"

status=$(run_sweep stamp --repo engine 306)
err=$(cat "$TMP/err.txt")
assert_eq "$status" "1" "stamp without --comment-file is refused"
assert_contains "$err" "--comment-file" "refusal names the missing flag"

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
