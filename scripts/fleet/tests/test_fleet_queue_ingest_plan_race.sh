#!/usr/bin/env bash
# Test the fleet-queue-ingest raced-planning-gate reconcile.
#
# TASK-FILING.md §"Agent-approved follow-up lane" plan-shape 2 files in three
# non-atomic steps (create → post `## Plan` comment → add fleet:plan-review).
# The planning gate keys on the comment, so an ingest tick landing between
# steps 1 and 2 stamps fleet:needs-plan and step 3 lands fleet:plan-review on
# top. The already-labeled skip guard returns early on either label, so no
# later tick re-examines the issue on its own — ingest strips the stale
# needs-plan and leaves the issue held by the plan-review arm of the guard.
#
# The discriminator is the label PAIR, not plan presence: fleet:needs-plan
# alone after a reviewer bounce (plan-review swapped out, a stale ## Plan
# comment still present) is a LEGITIMATE state and must be left alone.
#
# HOME is redirected to a temp sandbox; gh is stubbed to canned surfaces and
# every `gh issue edit` is logged for assertions.

set -euo pipefail

source "$(dirname "$0")/lib_assert.sh"

SCRIPT_DIR=$(cd "$(dirname "$0")/.." && pwd)
INGEST="$SCRIPT_DIR/fleet-queue-ingest"
if [[ ! -x "$INGEST" ]]; then
    echo "test setup: fleet-queue-ingest not found at $INGEST" >&2
    exit 1
fi

TMPROOT=""
cleanup() { [[ -n "$TMPROOT" && -d "$TMPROOT" ]] && rm -rf "$TMPROOT"; }
trap cleanup EXIT

TMPROOT=$(mktemp -d)
source "$(dirname "$0")/lib_hermetic.sh"
hermetic_poison_gh_env "$TMPROOT"
export HOME="$TMPROOT/home"
mkdir -p "$HOME/.fleet/state/projections" "$HOME/.fleet/logs"

PROJ="$HOME/.fleet/state/projections/queue-manager-ingest.json"
# issue 770 = needs-plan + plan-review (the race)     → strip needs-plan, still held
# issue 771 = needs-plan after a reviewer bounce      → legitimate, untouched
# issue 772 = needs-plan alone                        → left for planning (untouched)
# issue 773 = plan-review alone                       → already correct, untouched
cat > "$PROJ" <<'JSON'
{"pending_issues":[
  {"number":770,"repo":"engine"},
  {"number":771,"repo":"engine"},
  {"number":772,"repo":"engine"},
  {"number":773,"repo":"engine"}
],"unblock_issues":[]}
JSON

STUB_DIR="$TMPROOT/bin"; mkdir -p "$STUB_DIR"
export EDIT_LOG="$TMPROOT/edit.log"; : > "$EDIT_LOG"
export COMMENT_LOG="$TMPROOT/comment.log"; : > "$COMMENT_LOG"
cat > "$STUB_DIR/gh" <<'GHSTUB'
#!/usr/bin/env python3
import os, sys
args = sys.argv[1:]
if args[:2] == ["issue", "view"]:
    n = args[2] if len(args) > 2 else ""
    bodies = {
        "770": '{"title":"fleet: raced filing","body":"**Model:** opus\\n**Blocked by:** (none)","labels":[{"name":"fleet:agent-approved"},{"name":"fleet:needs-plan"},{"name":"fleet:plan-review"}],"comments":[{"body":"## Plan\\n\\nstep one"}]}',
        "771": '{"title":"fleet: bounced plan","body":"**Model:** opus\\n**Blocked by:** (none)","labels":[{"name":"fleet:agent-approved"},{"name":"fleet:needs-plan"}],"comments":[{"body":"## Plan\\n\\nstep one"}]}',
        "772": '{"title":"fleet: genuinely needs a plan","body":"**Model:** opus\\n**Blocked by:** (none)","labels":[{"name":"human:approved"},{"name":"fleet:needs-plan"}],"comments":[]}',
        "773": '{"title":"fleet: plan awaiting vetting","body":"**Model:** opus\\n**Blocked by:** (none)","labels":[{"name":"human:approved"},{"name":"fleet:plan-review"}],"comments":[{"body":"## Plan\\n\\nstep one"}]}',
    }
    print(bodies.get(n, '{"title":"","body":"","labels":[],"comments":[]}'))
elif args[:2] == ["issue", "edit"]:
    with open(os.environ["EDIT_LOG"], "a") as f:
        f.write(" ".join(args) + "\n")
elif args[:2] == ["issue", "comment"]:
    with open(os.environ["COMMENT_LOG"], "a") as f:
        f.write(" ".join(args) + "\n")
elif args[:2] == ["pr", "list"]:
    print("[]")
elif args[:1] == ["api"]:
    sys.stderr.write("gh: Not Found (HTTP 404)\n")
    sys.exit(1)
sys.exit(0)
GHSTUB
chmod +x "$STUB_DIR/gh"
# Native-Windows twin: fleet-queue-ingest invokes `gh` from PYTHON
# (subprocess), which cannot exec an extensionless shebang script on
# native-Windows python3 (mingw64) — see scripts/fleet/CLAUDE.md's
# native-Windows PATHEXT rule. Inert on POSIX hosts.
cat > "$STUB_DIR/gh.bat" <<'BATEOF'
@echo off
python3 "%~dp0gh" %*
BATEOF
export PATH="$STUB_DIR:$PATH"

echo "=== run fleet-queue-ingest ==="
INGEST_LOG="$TMPROOT/ingest.log"
bash "$INGEST" > "$INGEST_LOG" 2>&1 || true

edits_for() { grep -E "(^| )edit $1( |$)" "$EDIT_LOG" | tr '\n' '|'; }

assert_contains "$(edits_for 770)" "--remove-label fleet:needs-plan" \
    "#770 stripped the raced fleet:needs-plan (needs-plan + plan-review)"
assert_absent "$(edits_for 770)" "fleet:plan-review" \
    "#770 left fleet:plan-review in place (reviewer still owes a vet)"
assert_absent "$(edits_for 770)" "fleet:queued" \
    "#770 not queued — still held by the plan-review guard"

assert_absent "$(edits_for 771)" "--remove-label fleet:needs-plan" \
    "#771 kept fleet:needs-plan (reviewer bounce, plan comment present)"
assert_absent "$(edits_for 771)" "fleet:queued" \
    "#771 not queued (correctly skipped at the needs-plan guard)"

assert_absent "$(edits_for 772)" "--remove-label fleet:needs-plan" \
    "#772 kept fleet:needs-plan (no plan-review present)"

assert_absent "$(edits_for 773)" "fleet:needs-plan" \
    "#773 untouched (plan-review alone is already the correct state)"
assert_absent "$(edits_for 773)" "fleet:queued" \
    "#773 not queued — held for plan review"

assert_contains "$(cat "$INGEST_LOG")" "raced needs-plan stripped 1" \
    "run summary counts the reconcile (1 issue)"

summarize "fleet-queue-ingest raced-planning-gate reconcile"
