#!/usr/bin/env bash
# Tests for fleet-decisions (the human decision digest).
#
# Hermetic: `gh` is a PATH stub that serves fixtures for the engine repo,
# fails for the game repo (exercising the skip-with-warning path), and
# exits 99 on any unexpected invocation (fails closed — no live GitHub).
# FLEET_HOME points at a temp dir for the feedback-channel check.
#
# Covers:
#   - merge queue: approved PR listed; +nits annotation; smoke-hold sub-line
#   - decisions: gated / design-blocked PRs and human:review-plan issues
#   - a wip-only PR appears in no decision bucket
#   - cues: coding-improvement count, untriaged count, unread feedback roles
#     (file newer than .last-reviewed counts, older does not)
#   - headline decision count = merge queue + decisions
#   - unreachable repo is skipped with a warning, not fatal
#   - --repo=engine equals-form works; empty --repo= rejected (dual-spelling)

set -uo pipefail

SCRIPT_DIR=$(cd "$(dirname "$0")/.." && pwd)
FLEET_DECISIONS="$SCRIPT_DIR/fleet-decisions"
source "$(dirname "$0")/lib_assert.sh"

if [[ ! -x "$FLEET_DECISIONS" ]]; then
    echo "test setup: fleet-decisions not found at $FLEET_DECISIONS" >&2
    exit 1
fi

TMP=$(mktemp -d "${TMPDIR:-/tmp}/test-decisions.XXXXXX")
trap 'rm -rf "$TMP"' EXIT

# --- fixtures ---------------------------------------------------------------

cat > "$TMP/engine-prs.json" << 'EOF'
[
  {"number": 101, "title": "render: clean approved", "url": "u",
   "labels": [{"name": "fleet:approved"}]},
  {"number": 102, "title": "engine: approved with nits", "url": "u",
   "labels": [{"name": "fleet:approved"}, {"name": "fleet:has-nits"},
              {"name": "fleet:needs-linux-smoke"}]},
  {"number": 103, "title": "fleet: parked gated edit", "url": "u",
   "labels": [{"name": "fleet:gated"}]},
  {"number": 104, "title": "render: needs design answer", "url": "u",
   "labels": [{"name": "fleet:design-blocked"}, {"name": "fleet:wip"}]},
  {"number": 105, "title": "engine: plain wip", "url": "u",
   "labels": [{"name": "fleet:wip"}]}
]
EOF

cat > "$TMP/engine-issues.json" << 'EOF'
[
  {"number": 201, "title": "task: high-stakes plan hold", "url": "u",
   "labels": [{"name": "human:review-plan"}, {"name": "human:approved"}]},
  {"number": 202, "title": "improvement: rule tweak", "url": "u",
   "labels": [{"name": "fleet:coding-improvement"}]},
  {"number": 203, "title": "idea: untriaged thing", "url": "u", "labels": []},
  {"number": 204, "title": "task: queued", "url": "u",
   "labels": [{"name": "fleet:queued"}, {"name": "human:approved"}]},
  {"number": 205, "title": "task: needs plan", "url": "u",
   "labels": [{"name": "fleet:needs-plan"}, {"name": "human:approved"}]},
  {"number": 206, "title": "idea: triaged, verdict pending", "url": "u",
   "labels": [{"name": "fleet:triage-recommend"}]}
]
EOF

# --- gh stub (fails closed) -------------------------------------------------

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
    "pr list jakildev/IrredenEngine")    cat "$fixtures/engine-prs.json" ;;
    "issue list jakildev/IrredenEngine") cat "$fixtures/engine-issues.json" ;;
    "pr list jakildev/irreden")          exit 1 ;;
    "issue list jakildev/irreden")       exit 1 ;;
    *) echo "gh stub: unexpected invocation: $*" >&2; exit 99 ;;
esac
EOF
chmod +x "$TMP/bin/gh"

# --- feedback channel fixture ----------------------------------------------

mkdir -p "$TMP/fleet-home/feedback"
touch -t 202401010000 "$TMP/fleet-home/feedback/role-worker.md"
touch -t 202401020000 "$TMP/fleet-home/feedback/.last-reviewed"
touch -t 202401030000 "$TMP/fleet-home/feedback/merger.md"

run_decisions() {
    PATH="$TMP/bin:$PATH" FLEET_HOME="$TMP/fleet-home" \
        "$FLEET_DECISIONS" "$@" > "$TMP/out.txt" 2> "$TMP/err.txt"
    echo $?
}

# --- default run (engine + game; game unreachable) --------------------------

status=$(run_decisions)
out=$(cat "$TMP/out.txt")
err=$(cat "$TMP/err.txt")

assert_eq "$status" "0" "default run exits 0 despite unreachable game repo"
assert_contains "$err" "skipping jakildev/irreden" "unreachable repo warned on stderr"

assert_contains "$out" "6 decision(s) waiting" "headline counts merge queue + decisions"
assert_contains "$out" "Merge queue (2)" "merge queue counts both approved PRs"
assert_contains "$out" "engine PR #101" "clean approved PR listed"
assert_contains "$out" "#102" "approved-with-nits PR listed"
assert_contains "$out" "[approved+nits]" "has-nits annotated"
assert_contains "$out" "hold: fleet:needs-linux-smoke outstanding" "smoke hold sub-line"
assert_contains "$out" "Decisions (4)" "decision bucket counts gated + design-blocked + plan hold + triage verdict"
assert_contains "$out" "engine PR #103" "gated PR in decisions"
assert_contains "$out" "gated self-config edit" "gated tag rendered"
assert_contains "$out" "engine PR #104" "design-blocked PR in decisions"
assert_contains "$out" "engine issue #201" "plan sign-off issue in decisions"
assert_contains "$out" "engine issue #206" "triage-recommend issue in decisions"
assert_contains "$out" "triage verdict to review" "triage tag rendered"
assert_absent  "$out" "#105" "wip-only PR appears in no bucket"
assert_contains "$out" "fleet:coding-improvement: 1 open" "coding-improvement cue with count"
assert_contains "$out" "untriaged (no state labels): 1" "untriaged cue counts label-less issue"
assert_contains "$out" "engine #203" "untriaged cue names the issue"
assert_contains "$out" "merger" "feedback role newer than marker is unread"
assert_absent  "$out" "role-worker" "feedback role older than marker is not unread"
assert_contains "$out" "engine: 5 open PR(s) · 1 queued · 1 needs-plan" "status footer"

# --- --repo equals-form + dual-spelling validation --------------------------

status=$(run_decisions --repo=engine)
out=$(cat "$TMP/out.txt")
err=$(cat "$TMP/err.txt")
assert_eq "$status" "0" "--repo=engine equals-form accepted"
assert_contains "$out" "[engine]" "equals-form scopes to engine only"
assert_absent "$err" "skipping" "engine-only run never touches the game repo"

status=$(run_decisions --repo engine)
assert_eq "$status" "0" "--repo engine space-form accepted"

status=$(run_decisions --repo=)
assert_eq "$status" "1" "empty --repo= rejected (dual-spelling rule)"

status=$(run_decisions --bogus)
assert_eq "$status" "1" "unknown flag rejected with usage"

summarize "fleet-decisions tests"
