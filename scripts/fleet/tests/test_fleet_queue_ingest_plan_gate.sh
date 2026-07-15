#!/usr/bin/env bash
# Test the fleet-queue-ingest planning gate (#1456).
#
# An approved issue with no `## Plan` issue comment (canonical since #1932) and
# no plan file (neither planner-host staging ~/.fleet/plans/issue-<N>.md nor the
# committed repo-side copy .fleet/plans/issue-<N>.md, probed via `gh api`) must
# be bounced to fleet:needs-plan (never stamped fleet:queued), with an explanatory
# comment. Escape hatches: a `## Plan` comment, a local plan file, a committed
# plan file, an explicit "investigation spike" in the title/body, the `[no-plan]`
# tag, the `human:no-plan` label, the `fleet:plan-review` early-skip, and a
# non-404 probe failure (fail open — a transient API error must never bounce a
# planned issue).
#
# HOME is redirected to a temp dir so the script's hardcoded projection/log/lock
# paths land in the sandbox, and `gh` is stubbed to canned issue/PR surfaces.

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

# Local staging plan for #741 only — the same-host fast path.
mkdir -p "$HOME/.fleet/plans"
echo "# Plan: stub" > "$HOME/.fleet/plans/issue-741.md"

PROJ="$HOME/.fleet/state/projections/queue-manager-ingest.json"
# #740 = no plan anywhere, not a spike            → bounce to fleet:needs-plan
# #741 = local ~/.fleet/plans/issue-741.md exists → stamp
# #742 = committed repo-side plan (gh api 200)    → stamp
# #743 = no plan, body declares investigation spike → stamp
# #744 = plan probe fails WITHOUT a 404 (network) → fail open, stamp
# #745 = `## Plan` issue comment, no file (#1932)  → stamp (the canonical path)
# #746 = `[no-plan]` opt-out tag in title (#1932)  → stamp
# #747 = human:no-plan opt-out label (#1932)       → stamp
# #748 = fleet:plan-review (plan not yet vetted)    → early-skip (no stamp/bounce)
# #749 = fleet:agent-approved + fleet:no-plan (follow-up lane) → stamp
# #750 = fleet:agent-approved, no plan, no opt-out  → bounce to fleet:needs-plan
# #751 = fleet:agent-approved + filed plan + fleet:plan-review → early-skip
cat > "$PROJ" <<'JSON'
{"pending_issues":[
  {"number":740,"repo":"engine"},
  {"number":741,"repo":"engine"},
  {"number":742,"repo":"engine"},
  {"number":743,"repo":"engine"},
  {"number":744,"repo":"engine"},
  {"number":745,"repo":"engine"},
  {"number":746,"repo":"engine"},
  {"number":747,"repo":"engine"},
  {"number":748,"repo":"engine"},
  {"number":749,"repo":"engine"},
  {"number":750,"repo":"engine"},
  {"number":751,"repo":"engine"}
],"unblock_issues":[]}
JSON

# --- gh stub --------------------------------------------------------------
STUB_DIR="$TMPROOT/bin"
mkdir -p "$STUB_DIR"
export EDIT_LOG="$TMPROOT/edit.log"; : > "$EDIT_LOG"
export COMMENT_LOG="$TMPROOT/comment.log"; : > "$COMMENT_LOG"
cat > "$STUB_DIR/gh" <<'GHSTUB'
#!/usr/bin/env bash
case "$1" in
    issue)
        case "$2" in
            view)
                case "$3" in
                    740) echo '{"title":"render: fix residual","body":"**Model:** opus\n**Blocked by:** (none)","labels":[{"name":"human:approved"}]}' ;;
                    741) echo '{"title":"render: planned task","body":"**Model:** opus\n**Blocked by:** (none)","labels":[{"name":"human:approved"}]}' ;;
                    742) echo '{"title":"render: epic child","body":"**Model:** sonnet\n**Blocked by:** (none)","labels":[{"name":"human:approved"}]}' ;;
                    743) echo '{"title":"render: probe the culling path","body":"**Model:** opus\n**Blocked by:** (none)\n\nExplicit investigation spike: report findings, no fix expected.","labels":[{"name":"human:approved"}]}' ;;
                    744) echo '{"title":"render: planned elsewhere","body":"**Model:** opus\n**Blocked by:** (none)","labels":[{"name":"human:approved"}]}' ;;
                    745) echo '{"title":"render: planned via comment","body":"**Model:** opus\n**Blocked by:** (none)","labels":[{"name":"human:approved"}],"comments":[{"body":"## Plan: do the thing\n\nstep one"}]}' ;;
                    746) echo '{"title":"render: tiny tweak [no-plan]","body":"**Model:** sonnet\n**Blocked by:** (none)","labels":[{"name":"human:approved"}]}' ;;
                    747) echo '{"title":"render: human said skip","body":"**Model:** opus\n**Blocked by:** (none)","labels":[{"name":"human:approved"},{"name":"human:no-plan"}]}' ;;
                    748) echo '{"title":"render: plan under review","body":"**Model:** opus\n**Blocked by:** (none)","labels":[{"name":"human:approved"},{"name":"fleet:plan-review"}]}' ;;
                    749) echo '{"title":"render: stale baseline follow-up","body":"**Model:** sonnet\n**Blocked by:** (none)","labels":[{"name":"fleet:agent-approved"},{"name":"fleet:no-plan"}]}' ;;
                    750) echo '{"title":"render: verified defect, fix unknown","body":"**Model:** opus\n**Blocked by:** (none)","labels":[{"name":"fleet:agent-approved"}]}' ;;
                    751) echo '{"title":"render: follow-up filed with plan","body":"**Model:** sonnet\n**Blocked by:** (none)","labels":[{"name":"fleet:agent-approved"},{"name":"fleet:plan-review"}],"comments":[{"body":"## Plan: filed by the finder\n\nstep one"}]}' ;;
                    *)   echo '{"title":"","body":"","labels":[]}' ;;
                esac
                exit 0 ;;
            edit)
                printf '%s\n' "$*" >> "$EDIT_LOG"
                exit 0 ;;
            comment)
                printf '%s\n' "$*" >> "$COMMENT_LOG"
                exit 0 ;;
            *) exit 0 ;;
        esac ;;
    pr)
        case "$2" in
            list) echo '[]'; exit 0 ;;   # scope-shipped: no merged coverage
            *) exit 0 ;;
        esac ;;
    api)
        # Repo-side plan probe: gh api repos/<slug>/contents/.fleet/plans/issue-<N>.md
        case "$2" in
            repos/jakildev/IrredenEngine/contents/.fleet/plans/issue-742.md)
                echo '.fleet/plans/issue-742.md'; exit 0 ;;
            repos/jakildev/IrredenEngine/contents/.fleet/plans/issue-744.md)
                echo "connect: network is unreachable" >&2; exit 1 ;;
            *)
                echo "gh: Not Found (HTTP 404)" >&2; exit 1 ;;
        esac ;;
    *) exit 0 ;;
esac
GHSTUB
chmod +x "$STUB_DIR/gh"
export PATH="$STUB_DIR:$PATH"

# Per-issue edit-log line (gh issue edit <N> ...) for assertions. Tolerates
# no-match (empty) without tripping `set -e` / pipefail.
edit_line() { grep -E "(^| )edit ${1}( |$)" "$EDIT_LOG" | head -1 || true; }

echo "=== run fleet-queue-ingest over planned/unplanned approved issues ==="
bash "$INGEST" >/dev/null 2>&1 || true

# --- Bounce path -----------------------------------------------------------
# #740 (no plan, not a spike) → fleet:needs-plan added, fleet:queued withheld.
l740=$(edit_line 740)
if [[ -n "$l740" && "$l740" == *"fleet:needs-plan"* && "$l740" != *"fleet:queued"* ]]; then
    ok "unplanned #740 bounced to fleet:needs-plan without fleet:queued"
else
    bad "unplanned #740 mis-handled: '$l740'"
fi
# The bounce must not stamp a model label either — labeling is planning output.
if [[ "$l740" != *"fleet:opus"* && "$l740" != *"fleet:sonnet"* && "$l740" != *"fleet:fable"* ]]; then
    ok "#740 bounce carried no model label"
else
    bad "#740 bounce wrongly stamped a model label: '$l740'"
fi
if grep -qE '(^| )comment 740( |$)' "$COMMENT_LOG"; then
    ok "#740 got an explanatory planning-gate comment"
else
    bad "#740 bounced silently — no comment posted"
fi

# --- Escape hatches --------------------------------------------------------
# #741 (local staging plan) → stamped fleet:queued, no bounce.
l741=$(edit_line 741)
if [[ -n "$l741" && "$l741" == *"fleet:queued"* && "$l741" != *"fleet:needs-plan"* ]]; then
    ok "#741 (local ~/.fleet/plans plan) stamped fleet:queued"
else
    bad "#741 mis-stamped despite local plan: '$l741'"
fi

# #742 (committed repo-side plan, api 200) → stamped.
l742=$(edit_line 742)
if [[ -n "$l742" && "$l742" == *"fleet:queued"* && "$l742" != *"fleet:needs-plan"* ]]; then
    ok "#742 (committed repo-side plan) stamped fleet:queued"
else
    bad "#742 mis-stamped despite committed plan: '$l742'"
fi

# #743 (explicit investigation spike, no plan) → stamped.
l743=$(edit_line 743)
if [[ -n "$l743" && "$l743" == *"fleet:queued"* && "$l743" != *"fleet:needs-plan"* ]]; then
    ok "#743 (explicit investigation spike) stamped without a plan"
else
    bad "#743 spike escape hatch failed: '$l743'"
fi

# #744 (probe failed without 404) → fail open, stamped.
l744=$(edit_line 744)
if [[ -n "$l744" && "$l744" == *"fleet:queued"* && "$l744" != *"fleet:needs-plan"* ]]; then
    ok "#744 (non-404 probe failure) failed open and stamped"
else
    bad "#744 transient probe failure wrongly bounced: '$l744'"
fi

# --- #1932: comment-based plan + opt-outs -----------------------------------
# #745 (`## Plan` issue comment, no file) → stamped (the canonical redesign path).
l745=$(edit_line 745)
if [[ -n "$l745" && "$l745" == *"fleet:queued"* && "$l745" != *"fleet:needs-plan"* ]]; then
    ok "#745 (## Plan comment, no file) stamped fleet:queued"
else
    bad "#745 comment-based plan mis-handled: '$l745'"
fi

# #746 ([no-plan] tag in title) → stamped (opt-out).
l746=$(edit_line 746)
if [[ -n "$l746" && "$l746" == *"fleet:queued"* && "$l746" != *"fleet:needs-plan"* ]]; then
    ok "#746 ([no-plan] opt-out tag) stamped without a plan"
else
    bad "#746 [no-plan] opt-out failed: '$l746'"
fi

# #747 (human:no-plan label) → stamped (opt-out).
l747=$(edit_line 747)
if [[ -n "$l747" && "$l747" == *"fleet:queued"* && "$l747" != *"fleet:needs-plan"* ]]; then
    ok "#747 (human:no-plan opt-out label) stamped without a plan"
else
    bad "#747 human:no-plan opt-out failed: '$l747'"
fi

# #748 (fleet:plan-review) → early-skipped: neither queued nor bounced.
l748=$(edit_line 748)
if [[ -z "$l748" ]]; then
    ok "#748 (fleet:plan-review) early-skipped — no stamp, no bounce"
else
    bad "#748 plan-review issue was not skipped: '$l748'"
fi

# --- Agent-approved follow-up lane -------------------------------------------
# #749 (fleet:agent-approved + fleet:no-plan, no human:approved) → stamped.
l749=$(edit_line 749)
if [[ -n "$l749" && "$l749" == *"fleet:queued"* && "$l749" != *"fleet:needs-plan"* ]]; then
    ok "#749 (agent-approved + fleet:no-plan) stamped without a plan"
else
    bad "#749 fleet:no-plan opt-out failed: '$l749'"
fi

# #750 (fleet:agent-approved, no plan, no opt-out) → planning gate applies
# identically to the agent lane: bounced to fleet:needs-plan, never queued.
l750=$(edit_line 750)
if [[ -n "$l750" && "$l750" == *"fleet:needs-plan"* && "$l750" != *"fleet:queued"* ]]; then
    ok "#750 (planless agent-approved) bounced to fleet:needs-plan"
else
    bad "#750 planless agent-approved mis-handled: '$l750'"
fi

# #751 (agent-approved, filer-authored plan awaiting vetting) → early-skip,
# same as the planner-swapped #748 — ingest waits for the plan reviewer.
l751=$(edit_line 751)
if [[ -z "$l751" ]]; then
    ok "#751 (agent-approved + filed plan + plan-review) early-skipped"
else
    bad "#751 filed-plan follow-up was not skipped: '$l751'"
fi

echo
echo "================================"
echo "  PASS: $PASS    FAIL: $FAIL"
echo "================================"
[[ "$FAIL" -eq 0 ]]
