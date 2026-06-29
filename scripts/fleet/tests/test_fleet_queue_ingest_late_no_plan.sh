#!/usr/bin/env bash
# Test the fleet-queue-ingest late-opt-out reconcile.
#
# human:no-plan / [no-plan] / "investigation spike" mean "skip planning, queue
# directly". When that opt-out lands AFTER the issue already carries
# fleet:needs-plan (a human fast-tracking a backlog item that was earlier
# bounced to planning), the issue was stranded forever: the needs-plan skip
# guard dropped it from ingest, while the opt-out meant no planner would pick it
# up either — never reaching fleet:queued (the 9-issue limbo behind an idle
# "worker found no work" fleet). Ingest now strips the stale needs-plan and
# queues the issue directly.
#
# A plain fleet:needs-plan issue with NO opt-out must still be left for planning
# (skip guard), never auto-stripped.
#
# HOME is redirected to a temp sandbox; gh is stubbed to canned surfaces and
# every `gh issue edit` is logged for assertions.

set -euo pipefail

SCRIPT_DIR=$(cd "$(dirname "$0")/.." && pwd)
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
export HOME="$TMPROOT/home"
mkdir -p "$HOME/.fleet/state/projections" "$HOME/.fleet/logs"

PROJ="$HOME/.fleet/state/projections/queue-manager-ingest.json"
# #760 = human:no-plan label + fleet:needs-plan   → strip needs-plan, queue
# #761 = fleet:needs-plan, NO opt-out             → left for planning (skip)
# #762 = [no-plan] tag in title + fleet:needs-plan → strip needs-plan, queue
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
#!/usr/bin/env bash
case "$1" in
    issue)
        case "$2" in
            view)
                case "$3" in
                    760) echo '{"title":"render: fast-track","body":"**Model:** opus\n**Blocked by:** (none)","labels":[{"name":"human:approved"},{"name":"fleet:needs-plan"},{"name":"human:no-plan"}]}' ;;
                    761) echo '{"title":"render: genuinely needs a plan","body":"**Model:** opus\n**Blocked by:** (none)","labels":[{"name":"human:approved"},{"name":"fleet:needs-plan"}]}' ;;
                    762) echo '{"title":"render: tiny tweak [no-plan]","body":"**Model:** sonnet\n**Blocked by:** (none)","labels":[{"name":"human:approved"},{"name":"fleet:needs-plan"}]}' ;;
                    *)   echo '{"title":"","body":"","labels":[]}' ;;
                esac
                exit 0 ;;
            edit)    printf '%s\n' "$*" >> "$EDIT_LOG"; exit 0 ;;
            comment) printf '%s\n' "$*" >> "$COMMENT_LOG"; exit 0 ;;
            *) exit 0 ;;
        esac ;;
    pr)
        case "$2" in list) echo '[]'; exit 0 ;; *) exit 0 ;; esac ;;
    api) echo "gh: Not Found (HTTP 404)" >&2; exit 1 ;;
    *) exit 0 ;;
esac
GHSTUB
chmod +x "$STUB_DIR/gh"
export PATH="$STUB_DIR:$PATH"

echo "=== run fleet-queue-ingest ==="
bash "$INGEST" >/dev/null 2>&1 || true

# --- #760: late human:no-plan on a needs-plan issue → unstuck -----------------
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

# --- #761: plain needs-plan, no opt-out → left for planning -------------------
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

# --- #762: [no-plan] tag on a needs-plan issue → unstuck too ------------------
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
