#!/usr/bin/env bash
# Test the fleet-queue-ingest late-opt-out reconcile.
#
# human:no-plan / [no-plan] / "investigation spike" mean "skip planning, queue
# directly". When that opt-out lands AFTER the issue already carries
# fleet:needs-plan, ingest strips the stale needs-plan label and queues the
# issue directly, rather than leaving it stranded between the needs-plan skip
# guard (which drops it from ingest) and the opt-out (which no planner would
# act on).
#
# A plain fleet:needs-plan issue with NO opt-out must still be left for planning
# (skip guard), never auto-stripped.
#
# HOME is redirected to a temp sandbox; gh is stubbed to canned surfaces and
# every `gh issue edit` is logged for assertions.

set -euo pipefail

SCRIPT_DIR=$(cd "$(dirname "$0")/.." && pwd)
source "$(dirname "$0")/lib_preflight.sh"
INGEST="$SCRIPT_DIR/fleet-queue-ingest"
if [[ ! -x "$INGEST" ]]; then
    echo "test setup: fleet-queue-ingest not found at $INGEST" >&2
    exit 1
fi

PASS=0
FAIL=0
TMPROOT=""
cleanup() { [[ -n "$TMPROOT" && -d "$TMPROOT" ]] && rm -rf "$TMPROOT"; }
trap cleanup EXIT
ok()  { PASS=$((PASS + 1)); echo "  ok: $1"; }
bad() { FAIL=$((FAIL + 1)); echo "  FAIL: $1"; }

TMPROOT=$(mktemp -d)
source "$(dirname "$0")/lib_hermetic.sh"
hermetic_poison_gh_env "$TMPROOT"
export HOME="$TMPROOT/home"
mkdir -p "$HOME/.fleet/state/projections" "$HOME/.fleet/logs"

PROJ="$HOME/.fleet/state/projections/queue-manager-ingest.json"
# issue 760 = human:no-plan label + fleet:needs-plan   → strip needs-plan, queue
# issue 761 = fleet:needs-plan, NO opt-out             → left for planning (skip)
# issue 762 = [no-plan] tag in title + fleet:needs-plan → strip needs-plan, queue
cat > "$PROJ" <<'JSON'
{"pending_issues":[
  {"number":760,"repo":"engine"},
  {"number":761,"repo":"engine"},
  {"number":762,"repo":"engine"}
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
        "760": '{"title":"render: fast-track","body":"**Model:** opus\\n**Blocked by:** (none)","labels":[{"name":"human:approved"},{"name":"fleet:needs-plan"},{"name":"human:no-plan"}]}',
        "761": '{"title":"render: genuinely needs a plan","body":"**Model:** opus\\n**Blocked by:** (none)","labels":[{"name":"human:approved"},{"name":"fleet:needs-plan"}]}',
        "762": '{"title":"render: tiny tweak [no-plan]","body":"**Model:** sonnet\\n**Blocked by:** (none)","labels":[{"name":"human:approved"},{"name":"fleet:needs-plan"}]}',
    }
    print(bodies.get(n, '{"title":"","body":"","labels":[]}'))
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
bash "$INGEST" >/dev/null 2>&1 || true

# --- issue 760: late human:no-plan on a needs-plan issue → unstuck -----------
if grep -qE "(^| )edit 760 .*--remove-label fleet:needs-plan" "$EDIT_LOG"; then
    ok "#760 stripped stale fleet:needs-plan (honoring late human:no-plan)"
else
    bad "#760 did NOT strip fleet:needs-plan: $(grep -E '(^| )edit 760' "$EDIT_LOG" | tr '\n' '|')"
fi
if grep -qE "(^| )edit 760 .*--add-label fleet:queued|(^| )edit 760 .*fleet:queued" "$EDIT_LOG"; then
    ok "#760 stamped fleet:queued (queued directly)"
else
    bad "#760 did NOT reach fleet:queued: $(grep -E '(^| )edit 760' "$EDIT_LOG" | tr '\n' '|')"
fi

# --- issue 761: plain needs-plan, no opt-out → left for planning -------------
if grep -qE "(^| )edit 761 .*--remove-label fleet:needs-plan" "$EDIT_LOG"; then
    bad "#761 wrongly stripped fleet:needs-plan (no opt-out present)"
else
    ok "#761 kept fleet:needs-plan (no opt-out — left for planning)"
fi
if grep -qE "(^| )edit 761 .*fleet:queued" "$EDIT_LOG"; then
    bad "#761 wrongly queued an unplanned issue"
else
    ok "#761 not queued (correctly skipped at the needs-plan guard)"
fi

# --- issue 762: [no-plan] tag on a needs-plan issue → unstuck too ------------
if grep -qE "(^| )edit 762 .*--remove-label fleet:needs-plan" "$EDIT_LOG"; then
    ok "#762 stripped fleet:needs-plan via the [no-plan] tag path"
else
    bad "#762 did NOT strip fleet:needs-plan: $(grep -E '(^| )edit 762' "$EDIT_LOG" | tr '\n' '|')"
fi
if grep -qE "(^| )edit 762 .*fleet:queued" "$EDIT_LOG"; then
    ok "#762 stamped fleet:queued"
else
    bad "#762 did NOT reach fleet:queued"
fi

echo
echo "================================"
echo "PASS: $PASS    FAIL: $FAIL"
[[ "$FAIL" -eq 0 ]]
