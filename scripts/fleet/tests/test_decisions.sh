#!/usr/bin/env bash
# Tests for fleet-decisions (the human decision digest).
#
# Hermetic: `gh` is a PATH stub that serves fixtures for the engine repo,
# fails for the game repo by default (exercising the skip-with-warning path,
# overridable per-test via GH_STUB_GAME_PRS/GH_STUB_GAME_ISSUES), and exits
# 99 on any unexpected invocation (fails closed — no live GitHub).
# FLEET_HOME points at a temp dir for the feedback-channel check.
#
# Covers:
#   - merge queue: approved PR listed; +nits annotation; smoke-hold sub-line
#   - merge queue: an approved PR whose title still carries [WIP] with no
#     fleet:wip label gets a warn sub-line; clean approved PRs get none
#   - decisions: gated / design-blocked PRs and fleet:needs-human issues
#   - a wip-only PR appears in no decision bucket
#   - cues: coding-improvement count, untriaged count, unread feedback roles
#     (file newer than .last-reviewed counts, older does not)
#   - drain thresholds: coding-improvement cue flips to OVERDUE at >= 12
#     open (informational below); untriaged cue flips to OVERDUE at >= 12
#     awaiting (informational below); feedback cue flips to OVERDUE when the
#     .last-reviewed marker is >= 14 days old or absent (informational when
#     fresh) — both arms of each threshold exercised
#   - headline decision count = merge queue + decisions
#   - unreachable repo is skipped with a warning, not fatal
#   - both repos reachable at once: each repo's manifest-indexed artifacts
#     resolve independently, not cross-contaminated
#   - --repo=engine equals-form works; empty --repo= rejected (dual-spelling)
#   - CI gate holds per approved PR, keyed on (head sha, workflow path): a
#     replay of a recorded head whose runs predate one gate (a
#     no-completed-run hold) and failed another (a failed-on-head hold naming
#     master's conclusion), path-filtered workflows never held for a missing
#     run, the latest completed run winning over an older failure, a queued
#     or cancelled run not counting, an API failure printing an unreadable
#     hold, and the gate derivation over every `on:` spelling

set -uo pipefail

SCRIPT_DIR=$(cd "$(dirname "$0")/.." && pwd)
source "$(dirname "$0")/lib_preflight.sh"
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
   "headRefOid": "aaaaaaaaa1010000000000000000000000000000",
   "labels": [{"name": "fleet:approved"}]},
  {"number": 102, "title": "engine: approved with nits", "url": "u",
   "headRefOid": "aaaaaaaaa1020000000000000000000000000000",
   "labels": [{"name": "fleet:approved"}, {"name": "fleet:has-nits"},
              {"name": "fleet:needs-linux-smoke"}]},
  {"number": 103, "title": "fleet: parked gated edit", "url": "u",
   "labels": [{"name": "fleet:gated"}]},
  {"number": 104, "title": "render: needs design answer", "url": "u",
   "labels": [{"name": "fleet:design-blocked"}, {"name": "fleet:wip"}]},
  {"number": 105, "title": "engine: plain wip", "url": "u",
   "labels": [{"name": "fleet:wip"}]},
  {"number": 106, "title": "engine: approved but title still says [WIP]", "url": "u",
   "headRefOid": "aaaaaaaaa1060000000000000000000000000000",
   "labels": [{"name": "fleet:approved"}]},
  {"number": 107, "title": "fleet: feedback waits for owner", "url": "u",
   "labels": [{"name": "fleet:claim-mac-interactive"}, {"name": "human:needs-fix"}]}
]
EOF

cat > "$TMP/engine-issues.json" << 'EOF'
[
  {"number": 201, "title": "task: parked for a human decision", "url": "u",
   "labels": [{"name": "fleet:needs-human"}, {"name": "human:approved"}]},
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

# --- REST fixtures for the CI gate reading -----------------------------------
#
# Each `gh api <path>` response is a file named after the path with `/`, `?`,
# `&` and `=` mapped to `_`, under $GH_STUB_API (a missing file is GitHub's
# 404). `write_api.py <dir> <spec.json>` renders a spec — the default
# branch's workflow files, the runs per head sha, and master's latest
# conclusion per workflow — into that layout. A list response is paged 100
# per file, page 1 at the bare path and page N at `&page=N`, as GitHub serves it.

cat > "$TMP/write_api.py" << 'PYEOF'
import base64
import json
import re
import sys
from pathlib import Path

out, spec = Path(sys.argv[1]), json.loads(Path(sys.argv[2]).read_text())
slug = "jakildev/IrredenEngine"
out.mkdir(parents=True, exist_ok=True)


def put(path, payload):
    (out / re.sub(r"[/?&=]", "_", path)).write_text(json.dumps(payload))


def put_list(path, items, key=None):
    for start in range(0, max(len(items), 1), 100):
        page = items[start:start + 100]
        suffix = f"&page={start // 100 + 1}" if start else ""
        put(path + suffix, {"total_count": len(items), key: page} if key else page)


put(f"repos/{slug}", {"default_branch": "master"})
put(f"repos/{slug}/contents/.github/workflows?ref=master", [
    {"name": name, "path": f".github/workflows/{name}", "type": "file"}
    for name in spec["workflows"]])
for name, text in spec["workflows"].items():
    put(f"repos/{slug}/contents/.github/workflows/{name}?ref=master",
        {"content": base64.b64encode(text.encode()).decode()})
for sha, runs in spec["runs"].items():
    put_list(f"repos/{slug}/actions/runs?head_sha={sha}&per_page=100", runs, "workflow_runs")
for name, value in spec.get("master", {}).items():
    run = value if isinstance(value, dict) else {"conclusion": value}
    put(f"repos/{slug}/actions/workflows/{name}/runs?branch=master&status=completed&per_page=1",
        {"total_count": 1, "workflow_runs": [run]})
for run_id, job_ids in spec.get("jobs", {}).items():
    put_list(f"repos/{slug}/actions/runs/{run_id}/jobs?per_page=100",
             [{"id": j} for j in job_ids], "jobs")
for job_id, annotations in spec.get("annotations", {}).items():
    put_list(f"repos/{slug}/check-runs/{job_id}/annotations?per_page=100", annotations)
PYEOF

# The default fixture: three gates and two path-filtered workflows; every
# approved PR's head carries a green completed run of all three gates. PR
# 102's head also carries an older failed comment-refs run that a later
# success supersedes.
python3 - "$TMP/api-default.json" << 'PYEOF'
import json
import sys

unfiltered = "on:\n  push:\n    branches: [master]\n  pull_request:\n  workflow_dispatch:\n"
filtered = "on:\n  pull_request:\n    paths:\n      - 'engine/**'\n"
gates = ["comment-refs.yml", "instruction-size.yml", "no-plan-files.yml"]


def run(i, name, conclusion, created="2026-01-02T00:00:00Z"):
    return {"id": i, "path": f".github/workflows/{name}", "status": "completed",
            "conclusion": conclusion, "created_at": created}


runs = {sha: [run(i, name, "success") for i, name in enumerate(gates)]
        for sha in ("aaaaaaaaa1010000000000000000000000000000",
                    "aaaaaaaaa1060000000000000000000000000000")}
runs["aaaaaaaaa1020000000000000000000000000000"] = [
    run(1, "comment-refs.yml", "failure", "2026-01-01T00:00:00Z"),
    run(2, "comment-refs.yml", "success", "2026-01-02T00:00:00Z"),
    run(3, "instruction-size.yml", "success"),
    run(4, "no-plan-files.yml", "success")]
spec = {"workflows": {**{name: unfiltered for name in gates},
                      "format-check.yml": filtered, "header-checks.yml": filtered},
        "runs": runs}
with open(sys.argv[1], "w", encoding="utf-8") as f:
    json.dump(spec, f)
PYEOF
python3 "$TMP/write_api.py" "$TMP/api-default" "$TMP/api-default.json"

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
if [[ "$1" == "api" ]]; then
    # `gh api <path>`: a GET with no flags. Anything else is a call this stub
    # does not model, so it fails closed.
    if [[ $# -ne 2 || "$2" == -* ]]; then
        echo "gh stub: unexpected api invocation: $*" >&2
        exit 99
    fi
    if [[ -n "${GH_STUB_API_FAIL:-}" ]]; then
        echo "gh: API rate limit exceeded (HTTP 403)" >&2
        exit 1
    fi
    f="${GH_STUB_API:-$fixtures/api-default}/$(printf '%s' "$2" | tr '/?&=' '____')"
    if [[ -f "$f" ]]; then
        cat "$f"
        exit 0
    fi
    echo "gh: Not Found (HTTP 404)" >&2
    exit 1
fi
case "$1 $2 $repo" in
    "pr list jakildev/IrredenEngine")    cat "${GH_STUB_ENGINE_PRS:-$fixtures/engine-prs.json}" ;;
    "issue list jakildev/IrredenEngine") cat "${GH_STUB_ENGINE_ISSUES:-$fixtures/engine-issues.json}" ;;
    "pr list jakildev/irreden")
        [[ -n "${GH_STUB_GAME_PRS:-}" ]] && cat "$GH_STUB_GAME_PRS" || exit 1 ;;
    "issue list jakildev/irreden")
        [[ -n "${GH_STUB_GAME_ISSUES:-}" ]] && cat "$GH_STUB_GAME_ISSUES" || exit 1 ;;
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

assert_contains "$out" "8 decision(s) waiting" "headline counts merge queue + decisions"
assert_contains "$out" "Merge queue (3)" "merge queue counts all three approved PRs"
assert_contains "$out" "engine PR #101" "clean approved PR listed"
assert_contains "$out" "#102" "approved-with-nits PR listed"
assert_contains "$out" "[approved+nits]" "has-nits annotated"
assert_contains "$out" "hold: fleet:needs-linux-smoke outstanding" "smoke hold sub-line"
assert_contains "$out" "engine PR #106" "approved PR with a stale [WIP] title is listed"
assert_contains "$out" "warn: title still carries [WIP] with no fleet:wip label" "wip-title/label mismatch is flagged"
warn_lines=$(grep -c "warn: title still carries \[WIP\]" "$TMP/out.txt")
assert_eq "$warn_lines" "1" "only the mismatched PR gets the warn line, not #101/#102"
assert_contains "$out" "Decisions (5)" "decision bucket includes a stranded persistent owner"
assert_contains "$out" "engine PR #103" "gated PR in decisions"
assert_contains "$out" "gated self-config edit" "gated tag rendered"
assert_contains "$out" "engine PR #104" "design-blocked PR in decisions"
assert_contains "$out" "engine issue #201" "needs-human issue in decisions"
assert_contains "$out" "engine issue #206" "triage-recommend issue in decisions"
assert_contains "$out" "triage verdict to review" "triage tag rendered"
assert_contains "$out" "engine PR #107" "owned PR with feedback appears in decisions"
assert_contains "$out" "persistent owner fleet:claim-mac-interactive with outstanding human:needs-fix" "owned feedback names the handback surface"
assert_absent  "$out" "#105" "wip-only PR appears in no bucket"
assert_contains "$out" "fleet:coding-improvement: 1 open — cue" "coding-improvement cue informational below drain threshold"
assert_absent  "$out" "fleet:coding-improvement: 1 open — OVERDUE" "1 open never reads as overdue"
assert_contains "$out" "stale threshold 14d" "ancient .last-reviewed marker flips feedback cue to OVERDUE"
assert_contains "$out" "untriaged (no state labels): 1" "untriaged cue counts label-less issue"
assert_contains "$out" "engine #203" "untriaged cue names the issue"
assert_absent  "$out" "untriaged (no state labels): 1 awaiting triage — OVERDUE" "1 untriaged never reads as overdue"
assert_contains "$out" "merger" "feedback role newer than marker is unread"
assert_absent  "$out" "role-worker" "feedback role older than marker is not unread"
assert_contains "$out" "engine: 7 open PR(s) · 1 queued · 1 needs-plan" "status footer"
assert_absent  "$out" "has no completed run" "every gate ran on every approved head: no coverage hold"
assert_absent  "$out" "failed on head" \
    "a failed run superseded by a later success on the same head is no hold"
assert_absent  "$out" "gate coverage unreadable" "a readable coverage answer prints no unreadable hold"

# --- drain thresholds: both arms of each cue --------------------------------

# Fresh marker (now), one feedback file made newer a second later: unread
# but not stale — the feedback cue must render informational, not OVERDUE.
touch "$TMP/fleet-home/feedback/.last-reviewed"
sleep 1
touch "$TMP/fleet-home/feedback/merger.md"

status=$(run_decisions --repo=engine)
out=$(cat "$TMP/out.txt")
assert_eq "$status" "0" "fresh-marker run exits 0"
assert_contains "$out" "merger" "feedback file newer than fresh marker is still unread"
assert_contains "$out" "cue \`review-fleet-feedback\`" "fresh-marker feedback cue still points at the skill"
assert_absent  "$out" "stale threshold" "fresh marker keeps the feedback cue informational"

# 12 open coding-improvement tickets: the cue flips to OVERDUE.
python3 - "$TMP/engine-issues-many.json" << 'PYEOF'
import json
import sys

issues = [
    {
        "number": 300 + i,
        "title": f"improvement: rule {i}",
        "url": "u",
        "labels": [{"name": "fleet:coding-improvement"}],
    }
    for i in range(12)
]
with open(sys.argv[1], "w", encoding="utf-8") as f:
    json.dump(issues, f)
PYEOF

status=$(GH_STUB_ENGINE_ISSUES="$TMP/engine-issues-many.json" run_decisions --repo=engine)
out=$(cat "$TMP/out.txt")
assert_eq "$status" "0" "threshold run exits 0"
assert_contains "$out" "fleet:coding-improvement: 12 open — OVERDUE" "12 open flips the cue to OVERDUE"
assert_contains "$out" "drain threshold 12" "overdue cue names the threshold"

# 11 untriaged (label-less) issues: the cue stays informational, one under
# the drain threshold.
python3 - "$TMP/engine-issues-untriaged-11.json" << 'PYEOF'
import json
import sys

issues = [
    {"number": 400 + i, "title": f"idea: untriaged {i}", "url": "u", "labels": []}
    for i in range(11)
]
with open(sys.argv[1], "w", encoding="utf-8") as f:
    json.dump(issues, f)
PYEOF

status=$(GH_STUB_ENGINE_ISSUES="$TMP/engine-issues-untriaged-11.json" run_decisions --repo=engine)
out=$(cat "$TMP/out.txt")
assert_eq "$status" "0" "11-untriaged run exits 0"
assert_contains "$out" "untriaged (no state labels): 11 awaiting triage — engine #400" \
    "11 open renders informational, no OVERDUE marker"
assert_absent  "$out" "OVERDUE" "11 untriaged never reads as overdue"

# 12 untriaged issues: the cue flips to OVERDUE.
python3 - "$TMP/engine-issues-untriaged-12.json" << 'PYEOF'
import json
import sys

issues = [
    {"number": 400 + i, "title": f"idea: untriaged {i}", "url": "u", "labels": []}
    for i in range(12)
]
with open(sys.argv[1], "w", encoding="utf-8") as f:
    json.dump(issues, f)
PYEOF

status=$(GH_STUB_ENGINE_ISSUES="$TMP/engine-issues-untriaged-12.json" run_decisions --repo=engine)
out=$(cat "$TMP/out.txt")
assert_eq "$status" "0" "12-untriaged run exits 0"
assert_contains "$out" "untriaged (no state labels): 12 awaiting triage — OVERDUE" \
    "12 open flips the untriaged cue to OVERDUE"
assert_contains "$out" "drain threshold 12" "overdue untriaged cue names the threshold"
assert_contains "$out" "triage-protocol.md" "overdue untriaged cue names the drain"

# Restore the ancient marker layout for any later cases.
touch -t 202401010000 "$TMP/fleet-home/feedback/role-worker.md"
touch -t 202401020000 "$TMP/fleet-home/feedback/.last-reviewed"
touch -t 202401030000 "$TMP/fleet-home/feedback/merger.md"

# --- marker age under GNU stat ----------------------------------------------
#
# The marker-age read must try GNU's `-c %Y` before BSD's `-f %m`: GNU reads
# `-f`'s argument as a path, so it emits a filesystem block on stdout that
# poisons the captured mtime and aborts the digest under `set -e`. This stub
# emulates GNU stat so a macOS host still exercises the Linux path.
cat > "$TMP/bin/stat" << 'EOF'
#!/usr/bin/env bash
# Delegate to the host's real stat, trying both spellings (this stub runs on
# macOS and Linux alike); the shim's job is only to mimic GNU's *interface*.
real_mtime() {
    /usr/bin/stat -c %Y "$1" 2>/dev/null || /usr/bin/stat -f %m "$1" 2>/dev/null
}
if [[ "$1" == "-c" && "$2" == "%Y" ]]; then
    real_mtime "$3"
    exit $?
fi
if [[ "$1" == "-f" ]]; then
    # GNU: %m is not a format here, it's a path that does not exist.
    echo "  File: \"$3\""
    echo "Blocks: Total: 1  Free: 1  Available: 1"
    echo "stat: cannot read file system information for '$2'" >&2
    exit 1
fi
exit 1
EOF
chmod +x "$TMP/bin/stat"

status=$(run_decisions --repo=engine)
out=$(cat "$TMP/out.txt")
assert_eq "$status" "0" "GNU-stat run exits 0"
assert_contains "$out" "stale threshold 14d" \
    "ancient marker still flips the feedback cue to OVERDUE under GNU stat"
assert_absent "$out" "Blocks: Total" \
    "GNU stat's filesystem block never leaks into the report"

rm -f "$TMP/bin/stat"

# --- CI gate holds: a recorded head replayed ---------------------------------
#
# The runs payload below is recorded from `actions/runs?head_sha=` for a PR
# that merged on checks completed about 16 hours before the instruction-size
# gate landed, with a failed comment-refs run among them.

cat > "$TMP/engine-prs-replay.json" << 'EOF'
[
  {"number": 3333, "title": "engine: approved on checks that predate a gate", "url": "u",
   "headRefOid": "2f0ebed838762bc4ff38bb2a27be70ae85d2c1fb",
   "labels": [{"name": "fleet:approved"}]}
]
EOF

python3 - "$TMP/api-default.json" "$TMP/api-replay.json" << 'PYEOF'
import json
import sys

HEAD = "2f0ebed838762bc4ff38bb2a27be70ae85d2c1fb"
with open(sys.argv[1], encoding="utf-8") as f:
    spec = json.load(f)
spec["runs"] = {HEAD: [
    {"conclusion": "success", "created_at": "2026-09-12T02:24:01Z", "event": "pull_request",
     "head_sha": HEAD, "id": 34667552839, "name": "Format Check",
     "path": ".github/workflows/format-check.yml", "status": "completed"},
    {"conclusion": "failure", "created_at": "2026-09-12T02:24:01Z", "event": "pull_request",
     "head_sha": HEAD, "id": 34667552882, "name": "Comment Refs",
     "path": ".github/workflows/comment-refs.yml", "status": "completed"},
    {"conclusion": "success", "created_at": "2026-09-12T02:24:01Z", "event": "pull_request",
     "head_sha": HEAD, "id": 34667552846, "name": "Header Checks",
     "path": ".github/workflows/header-checks.yml", "status": "completed"},
    {"conclusion": "success", "created_at": "2026-09-12T02:24:01Z", "event": "pull_request",
     "head_sha": HEAD, "id": 34667552864, "name": "No Plan Files",
     "path": ".github/workflows/no-plan-files.yml", "status": "completed"}]}
spec["master"] = {"comment-refs.yml": "success"}
with open(sys.argv[2], "w", encoding="utf-8") as f:
    json.dump(spec, f)
PYEOF
python3 "$TMP/write_api.py" "$TMP/api-replay" "$TMP/api-replay.json"

status=$(GH_STUB_ENGINE_PRS="$TMP/engine-prs-replay.json" GH_STUB_API="$TMP/api-replay" \
    run_decisions --repo=engine)
out=$(cat "$TMP/out.txt")
assert_eq "$status" "0" "replay run exits 0"
assert_contains "$out" \
    "hold: .github/workflows/instruction-size.yml has no completed run on head 2f0ebed83 — Update branch to trigger it" \
    "a gate with no run on the head is held"
assert_contains "$out" \
    "hold: .github/workflows/comment-refs.yml failed on head 2f0ebed83 (master: success)" \
    "a failed run on the head is held, naming master's conclusion"
hold_lines=$(grep -c "      hold: " "$TMP/out.txt")
assert_eq "$hold_lines" "2" "exactly the two gate holds"
assert_absent "$out" "format-check.yml" "a path-filtered workflow that passed is never held"
assert_absent "$out" "header-checks.yml" "a second path-filtered workflow is never held"

status=$(GH_STUB_ENGINE_PRS="$TMP/engine-prs-replay.json" GH_STUB_API_FAIL=1 \
    run_decisions --repo=engine)
out=$(cat "$TMP/out.txt")
assert_eq "$status" "0" "an API failure is not fatal to the digest"
assert_contains "$out" \
    "hold: gate coverage unreadable (gh api repos/jakildev/IrredenEngine: gh: API rate limit exceeded (HTTP 403))" \
    "an API failure prints an unreadable hold, never a clean reading"

# --- CI gate derivation over every `on:` spelling ---------------------------
#
# The head has no usable run, so every derived gate prints a no-completed-run
# hold and the hold set is the derived set. One filtered workflow did run and
# fail on the head, with no run on master: it is held as failed, master none.

python3 - "$TMP/api-spellings.json" << 'PYEOF'
import json
import sys

workflows = {
    "map-filtered.yml": "on:\n  push:\n  pull_request:\n    paths: ['a/**']\n",
    "map-branches.yml": "on:\n  pull_request:\n    branches:\n      - main\n",
    "map-bare.yml": "# a comment\n'on':\n  push:\n    branches: [master]\n  pull_request:\n",
    "scalar.yml": "name: x\non: pull_request\njobs: {}\n",
    "flow-list.yml": "on: [push, pull_request]\n",
    "block-list.yml": "on:\n  - push\n  - pull_request\n",
    "push-only.yml": "on:\n  push:\n    branches: [master]\n",
    "target-only.yml": "on:\n  pull_request_target:\n",
    "types-superset.yml": "on:\n  pull_request:\n"
                          "    types: [opened, reopened, synchronize, edited]\n",
    "types-block.yml": "on:\n  pull_request:\n    types:\n      - reopened\n"
                       "      - synchronize\n      - opened\n",
    "types-narrow.yml": "on:\n  pull_request:\n    types: [synchronize]\n",
    "notes.txt": "on: pull_request\n",
}
runs = {"bbbbbbbbb2000000000000000000000000000000": [
    {"id": 1, "path": ".github/workflows/map-filtered.yml", "status": "completed",
     "conclusion": "failure", "created_at": "2026-01-01T00:00:00Z"},
    {"id": 2, "path": ".github/workflows/scalar.yml", "status": "in_progress",
     "conclusion": None, "created_at": "2026-01-01T00:00:00Z"},
    {"id": 3, "path": ".github/workflows/flow-list.yml", "status": "completed",
     "conclusion": "cancelled", "created_at": "2026-01-01T00:00:00Z"}]}
with open(sys.argv[1], "w", encoding="utf-8") as f:
    json.dump({"workflows": workflows, "runs": runs}, f)
PYEOF
python3 "$TMP/write_api.py" "$TMP/api-spellings" "$TMP/api-spellings.json"

cat > "$TMP/engine-prs-spellings.json" << 'EOF'
[
  {"number": 900, "title": "engine: approved, no run on the head", "url": "u",
   "headRefOid": "bbbbbbbbb2000000000000000000000000000000",
   "labels": [{"name": "fleet:approved"}]}
]
EOF

status=$(GH_STUB_ENGINE_PRS="$TMP/engine-prs-spellings.json" GH_STUB_API="$TMP/api-spellings" \
    run_decisions --repo=engine)
out=$(cat "$TMP/out.txt")
assert_eq "$status" "0" "spellings run exits 0"
derived=$(sed -n 's|.*hold: \.github/workflows/\([^ ]*\) has no completed run.*|\1|p' "$TMP/out.txt" \
    | sort | tr '\n' ' ')
assert_eq "$derived" \
    "block-list.yml flow-list.yml map-bare.yml scalar.yml types-block.yml types-superset.yml " \
    "only pull_request triggers that fire on every head push are gates; a queued or cancelled run is no reading"
assert_contains "$out" \
    "hold: .github/workflows/map-filtered.yml failed on head bbbbbbbbb (master: none)" \
    "a failed filtered workflow is held; no master run reads as none"

# --- own red vs inherited red, by failed-suite identity ----------------------
#
# fleet-tests failed on master (alpha with identity 111, and gamma
# unitemized) and on six heads. Head 700's run annotated alpha@111 and
# beta@222: beta is its own and is held. Head 701 failed exactly as master
# does on alpha: inherited, a note. Head 702 passed fleet-tests before
# master's later run failed: its green predates the break and is held to
# re-run. Head 703 is BEHIND; head 704 is UNSTABLE with every run green, so
# the merge-box reading is the only signal and is held as unaccounted. Head
# 707 fails alpha with a different identity: a second failure in a suite
# master already fails is its own. Head 708 fails gamma unitemized, as master
# does: nothing proves the two failed alike, so it is held.

python3 - "$TMP/api-suites.json" << 'PYEOF'
import json
import sys

unfiltered = "on:\n  push:\n    branches: [master]\n  pull_request:\n  workflow_dispatch:\n"
filtered = "on:\n  pull_request:\n    paths:\n      - 'scripts/**'\n"
OWN, SAME, STALE, BEHIND, UNSTABLE = ("c" * 40, "d" * 40, "e" * 40, "f" * 40, "1" * 40)
OTHER, UNITEMIZED = "4" * 40, "5" * 40


def run(i, name, conclusion, created):
    return {"id": i, "path": f".github/workflows/{name}", "status": "completed",
            "conclusion": conclusion, "created_at": created}


runs = {
    OWN: [run(11, "comment-refs.yml", "success", "2026-02-01T00:00:00Z"),
          run(12, "fleet-tests.yml", "failure", "2026-02-01T00:00:00Z")],
    SAME: [run(21, "comment-refs.yml", "success", "2026-02-01T00:00:00Z"),
           run(22, "fleet-tests.yml", "failure", "2026-02-01T00:00:00Z")],
    STALE: [run(31, "comment-refs.yml", "success", "2026-01-15T00:00:00Z"),
            run(32, "fleet-tests.yml", "success", "2026-01-15T00:00:00Z")],
    BEHIND: [run(41, "comment-refs.yml", "success", "2026-02-02T00:00:00Z"),
             run(42, "fleet-tests.yml", "success", "2026-02-02T00:00:00Z")],
    UNSTABLE: [run(51, "comment-refs.yml", "success", "2026-02-02T00:00:00Z"),
               run(52, "fleet-tests.yml", "success", "2026-02-02T00:00:00Z")],
    OTHER: [run(81, "comment-refs.yml", "success", "2026-02-01T00:00:00Z"),
            run(82, "fleet-tests.yml", "failure", "2026-02-01T00:00:00Z")],
    UNITEMIZED: [run(91, "comment-refs.yml", "success", "2026-02-01T00:00:00Z"),
                 run(92, "fleet-tests.yml", "failure", "2026-02-01T00:00:00Z")],
}
spec = {
    "workflows": {"comment-refs.yml": unfiltered, "fleet-tests.yml": filtered},
    "runs": runs,
    "master": {"comment-refs.yml": "success",
               "fleet-tests.yml": {"conclusion": "failure", "id": 900,
                                   "created_at": "2026-02-01T12:00:00Z"}},
    "jobs": {"12": [1201], "22": [2201], "82": [8201], "92": [9201], "900": [9001]},
    "annotations": {
        "1201": [{"title": "fleet-tests failed suites",
                  "message": "test_alpha.sh@111 test_beta.py@222"}],
        "2201": [{"title": "fleet-tests failed suites", "message": "test_alpha.sh@111"}],
        "8201": [{"title": "fleet-tests failed suites", "message": "test_alpha.sh@333"}],
        "9201": [{"title": "fleet-tests failed suites", "message": "test_gamma.sh@?"}],
        "9001": [{"title": "unrelated", "message": "x"},
                 {"title": "fleet-tests failed suites",
                  "message": "test_alpha.sh@111 test_gamma.sh@?"}],
    },
}
with open(sys.argv[1], "w", encoding="utf-8") as f:
    json.dump(spec, f)
PYEOF
python3 "$TMP/write_api.py" "$TMP/api-suites" "$TMP/api-suites.json"

python3 - "$TMP/engine-prs-suites.json" << 'PYEOF'
import json
import sys

approved = [{"name": "fleet:approved"}]
prs = [
    {"number": 700, "title": "engine: own red suite", "headRefOid": "c" * 40,
     "mergeStateStatus": "UNSTABLE"},
    {"number": 701, "title": "engine: inherited red", "headRefOid": "d" * 40,
     "mergeStateStatus": "UNSTABLE"},
    {"number": 702, "title": "engine: stale green", "headRefOid": "e" * 40,
     "mergeStateStatus": "CLEAN"},
    {"number": 703, "title": "engine: behind master", "headRefOid": "f" * 40,
     "mergeStateStatus": "BEHIND"},
    {"number": 704, "title": "engine: unstable, unaccounted", "headRefOid": "1" * 40,
     "mergeStateStatus": "UNSTABLE"},
    {"number": 707, "title": "engine: new failure in a red suite", "headRefOid": "4" * 40,
     "mergeStateStatus": "UNSTABLE"},
    {"number": 708, "title": "engine: unitemized red", "headRefOid": "5" * 40,
     "mergeStateStatus": "UNSTABLE"},
]
for pr in prs:
    pr.update({"url": "u", "labels": approved})
with open(sys.argv[1], "w", encoding="utf-8") as f:
    json.dump(prs, f)
PYEOF

status=$(GH_STUB_ENGINE_PRS="$TMP/engine-prs-suites.json" GH_STUB_API="$TMP/api-suites" \
    run_decisions --repo=engine)
out=$(cat "$TMP/out.txt")
assert_eq "$status" "0" "suite-set run exits 0"
assert_contains "$out" \
    "hold: .github/workflows/fleet-tests.yml failed on head ccccccccc (master: failure) — 1 suite(s) not failing this way on master: test_beta.py" \
    "a suite failing on the head but not on master is the PR's own red and is held by name"
assert_contains "$out" \
    "note: .github/workflows/fleet-tests.yml failed on head ddddddddd with the same failures as master — inherited, not this PR's to fix" \
    "a head failing exactly as master does is a note"
assert_absent "$out" "hold: .github/workflows/fleet-tests.yml failed on head ddddddddd" \
    "an inherited red is never a hold"
assert_contains "$out" \
    "hold: .github/workflows/fleet-tests.yml failed on head 444444444 (master: failure) — 1 suite(s) not failing this way on master: test_alpha.sh" \
    "a different failure in a suite master already fails is the PR's own red"
assert_absent "$out" "note: .github/workflows/fleet-tests.yml failed on head 444444444" \
    "a same-suite, different-failure red is never read as inherited"
assert_contains "$out" \
    "hold: .github/workflows/fleet-tests.yml failed on head 555555555 (master: failure) — 1 suite(s) not failing this way on master: test_gamma.sh" \
    "an unitemized failure never matches master's, even when master's is unitemized too"
assert_contains "$out" \
    "hold: .github/workflows/fleet-tests.yml passed on head eeeeeeeee at 2026-01-15T00:00:00Z, but master's later run (2026-02-01T12:00:00Z) failed — Update branch to re-run before merging" \
    "a green run that predates master's break is held to re-run"
assert_contains "$out" "hold: behind master — Update branch" "BEHIND is an Update-branch hold"
unstable_lines=$(grep -c "mergeStateStatus UNSTABLE" "$TMP/out.txt")
assert_eq "$unstable_lines" "1" \
    "the merge-box UNSTABLE hold prints only where no run or note accounts for it (#704), not on #700/#701"
assert_contains "$out" \
    "hold: GitHub reports a check on the head that has not passed (mergeStateStatus UNSTABLE: failed or still running) that no completed run above accounts for" \
    "UNSTABLE with every run green is held as unread"
stale_lines=$(grep -c "Update branch to re-run" "$TMP/out.txt")
assert_eq "$stale_lines" "1" "only the head whose green predates master's failure is held stale"

# --- every list read pages ---------------------------------------------------
#
# Head 705's runs, its failed run's jobs, and that job's annotations each put
# the item that matters on page 2: 100 cancelled runs precede its two real
# runs, 100 jobs precede the annotated one, and 100 unrelated annotations
# precede the suite annotation. Read to the end, its red is master's — a
# note. Head 706's failed run has 300 jobs, a list still full at the page
# cap: its tail was never read, so the reading is unreadable, not complete.

python3 - "$TMP/api-paged.json" << 'PYEOF'
import json
import sys

unfiltered = "on:\n  push:\n    branches: [master]\n  pull_request:\n  workflow_dispatch:\n"
filtered = "on:\n  pull_request:\n    paths:\n      - 'scripts/**'\n"
PAGED, CAPPED = "2" * 40, "3" * 40


def run(i, name, conclusion, created="2026-02-01T00:00:00Z"):
    return {"id": i, "path": f".github/workflows/{name}", "status": "completed",
            "conclusion": conclusion, "created_at": created}


suite = {"title": "fleet-tests failed suites", "message": "test_alpha.sh@111"}
spec = {
    "workflows": {"comment-refs.yml": unfiltered, "fleet-tests.yml": filtered},
    "runs": {
        PAGED: [run(1000 + i, "comment-refs.yml", "cancelled") for i in range(100)]
        + [run(61, "comment-refs.yml", "success"), run(62, "fleet-tests.yml", "failure")],
        CAPPED: [run(71, "comment-refs.yml", "success"), run(72, "fleet-tests.yml", "failure")],
    },
    "master": {"comment-refs.yml": "success",
               "fleet-tests.yml": {"conclusion": "failure", "id": 900,
                                   "created_at": "2026-02-01T12:00:00Z"}},
    "jobs": {"62": list(range(6200, 6301)), "72": list(range(7000, 7300)), "900": [9001]},
    "annotations": {
        "6300": [{"title": "unrelated", "message": str(i)} for i in range(100)] + [suite],
        "9001": [suite],
    },
}
with open(sys.argv[1], "w", encoding="utf-8") as f:
    json.dump(spec, f)
PYEOF
python3 "$TMP/write_api.py" "$TMP/api-paged" "$TMP/api-paged.json"

python3 - "$TMP/engine-prs-paged.json" << 'PYEOF'
import json
import sys

prs = [
    {"number": 705, "title": "engine: red read past page 1", "headRefOid": "2" * 40},
    {"number": 706, "title": "engine: jobs past the page cap", "headRefOid": "3" * 40},
]
for pr in prs:
    pr.update({"url": "u", "labels": [{"name": "fleet:approved"}],
               "mergeStateStatus": "UNSTABLE"})
with open(sys.argv[1], "w", encoding="utf-8") as f:
    json.dump(prs, f)
PYEOF

status=$(GH_STUB_ENGINE_PRS="$TMP/engine-prs-paged.json" GH_STUB_API="$TMP/api-paged" \
    run_decisions --repo=engine)
out=$(cat "$TMP/out.txt")
assert_eq "$status" "0" "paged run exits 0"
assert_contains "$out" \
    "note: .github/workflows/fleet-tests.yml failed on head 222222222 with the same failures as master — inherited, not this PR's to fix" \
    "a suite annotation on page 2 of a page-2 job of a page-2 run is read"
assert_absent "$out" "has no completed run on head 222222222" \
    "a head run past the first page of the runs list is read"
assert_absent "$out" "hold: .github/workflows/fleet-tests.yml failed on head 222222222" \
    "an inherited red read past page 1 is never a hold"
assert_contains "$out" \
    "hold: gate coverage unreadable (gh api repos/jakildev/IrredenEngine/actions/runs/72/jobs: more than 300 items)" \
    "a list still full at the page cap is unreadable, never read as complete"

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

# --- both repos reachable: manifest artifact index isn't cross-contaminated -
#
# The manifest carries only a repo slug and an artifact index (never a host
# path); each repo's `<idx>-prs.json`/`<idx>-issues.json` is derived in
# Python from that index. Two simultaneously-reachable repos is the only way
# to prove index 0 and index 1 each resolve to their own artifacts rather
# than the last repo queried overwriting the read.

cat > "$TMP/game-prs.json" << 'EOF'
[
  {"number": 301, "title": "game: gated edit", "url": "u",
   "labels": [{"name": "fleet:gated"}]}
]
EOF

cat > "$TMP/game-issues.json" << 'EOF'
[
  {"number": 401, "title": "game: parked for a human decision", "url": "u",
   "labels": [{"name": "fleet:needs-human"}]}
]
EOF

status=$(GH_STUB_GAME_PRS="$TMP/game-prs.json" GH_STUB_GAME_ISSUES="$TMP/game-issues.json" \
    run_decisions)
out=$(cat "$TMP/out.txt")
err=$(cat "$TMP/err.txt")
assert_eq "$status" "0" "both-repos-reachable run exits 0"
assert_absent "$err" "skipping" "neither repo is reported unreachable"
assert_contains "$out" "[engine+game]" "scope lists both repos"
assert_contains "$out" "engine PR #101" "engine's own PR survives alongside game's"
assert_contains "$out" "game PR #301  game: gated edit" "game's PR is read from its own artifact, not engine's"
assert_contains "$out" "game issue #401" "game's issue is read from its own artifact, not engine's"

summarize "fleet-decisions tests"
