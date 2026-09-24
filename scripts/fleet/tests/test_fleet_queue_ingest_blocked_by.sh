#!/usr/bin/env bash
# Test that fleet-queue-ingest queues blocked tasks with a fleet:blocked
# marker and removes the marker once the last blocker closes.
#
# Add path: every approved, non-skip task is stamped fleet:queued + model;
# a task whose **Blocked by:** predecessor is still open additionally gets
# fleet:blocked. Remove path: a queued task carrying fleet:blocked whose
# blocker has closed (surfaced in unblock_issues) has the marker stripped.
# The blocker issues are only READ (live state probe), never edited.
#
# HOME is redirected to a temp dir so the script's hardcoded projection/log/lock
# paths land in the sandbox, and `gh` is stubbed to canned issue/PR surfaces.

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
# Add path: a stacked epic. task 730 = head (no blocker), task 731 = blocked
# by an OPEN predecessor (task 719), task 732 = blocked by a CLOSED
# predecessor (task 718).
# Cross-repo: task 735 = engine task blocked by a CLOSED game ref
# (jakildev/irreden task 777) → routed to game, not blocked; task 736 =
# engine task blocked by an OPEN game ref (jakildev/irreden task 778) →
# blocked.
# Plain form: task 737 = engine task whose ONLY dep is a degraded plain
# mid-line "Blocked by: task 719" → blocked; the shared parser catches this
# form even though the dependency isn't stated in the canonical bold form.
# PR URL: task 738 is blocked by a pull request closed without merge, so it stays
# blocked until that pull request reaches MERGED.
# Remove path: task 733 = queued+fleet:blocked, blocker task 717 now CLOSED
# (unblock); task 734 = queued+fleet:blocked, blocker task 719 still OPEN
# (stay).
cat > "$PROJ" <<'JSON'
{"pending_issues":[
  {"number":730,"repo":"engine"},
  {"number":731,"repo":"engine"},
  {"number":732,"repo":"engine"},
  {"number":735,"repo":"engine"},
  {"number":736,"repo":"engine"},
  {"number":737,"repo":"engine"},
  {"number":738,"repo":"engine"}
],"unblock_issues":[
  {"number":733,"repo":"engine"},
  {"number":734,"repo":"engine"}
]}
JSON

# --- gh stub --------------------------------------------------------------
STUB_DIR="$TMPROOT/bin"
mkdir -p "$STUB_DIR"
export EDIT_LOG="$TMPROOT/edit.log"; : > "$EDIT_LOG"
cat > "$STUB_DIR/gh" <<'GHSTUB'
#!/usr/bin/env python3
import os, sys
args = sys.argv[1:]

def _repo_of(argv):
    for i, a in enumerate(argv):
        if a == "--repo" and i + 1 < len(argv):
            return argv[i + 1]
    return ""

if args[:2] == ["issue", "view"]:
    # Blocker-state probes pass `--jq .state`; the body/labels fetch asks
    # for `--json body,labels`. Dispatch on which one this is.
    if "--jq" in args:
        # Cross-repo refs resolve against the referenced repo, not the
        # issue's own — capture the --repo value passed to this call.
        bref_repo = _repo_of(args)
        n3 = args[2] if len(args) > 2 else ""
        if n3 in ("717", "718"):   # 733/732's predecessor — satisfied
            print("CLOSED")
        elif n3 == "719":          # 731/734's predecessor — open
            print("OPEN")
        elif n3 == "202":         # closed pull request, not merged
            print("CLOSED")
        elif n3 == "777":
            # CLOSED only when routed to game (the referenced repo);
            # OPEN if mis-routed to engine.
            print("CLOSED" if bref_repo == "jakildev/irreden" else "OPEN")
        elif n3 == "778":          # cross-repo game ref, still open
            print("OPEN")
        else:
            print("OPEN")
        sys.exit(0)
    n3 = args[2] if len(args) > 2 else ""
    bodies = {
        "730": '{"body":"**Model:** opus\\n**Blocked by:** (none)","labels":[{"name":"human:approved"}],"comments":[{"body":"## Plan: stub\\n\\nstep one"}]}',
        "731": '{"body":"**Model:** sonnet\\n**Blocked by:** #719","labels":[{"name":"human:approved"}],"comments":[{"body":"## Plan: stub\\n\\nstep one"}]}',
        "732": '{"body":"**Model:** opus\\n**Blocked by:** #718","labels":[{"name":"human:approved"}],"comments":[{"body":"## Plan: stub\\n\\nstep one"}]}',
        "733": '{"body":"**Blocked by:** #717","labels":[{"name":"fleet:queued"},{"name":"fleet:opus"},{"name":"fleet:blocked"}]}',
        "734": '{"body":"**Blocked by:** #719","labels":[{"name":"fleet:queued"},{"name":"fleet:opus"},{"name":"fleet:blocked"}]}',
        "735": '{"body":"**Model:** opus\\n**Blocked by:** jakildev/irreden#777","labels":[{"name":"human:approved"}],"comments":[{"body":"## Plan: stub\\n\\nstep one"}]}',
        "736": '{"body":"**Model:** opus\\n**Blocked by:** jakildev/irreden#778","labels":[{"name":"human:approved"}],"comments":[{"body":"## Plan: stub\\n\\nstep one"}]}',
        "737": '{"body":"**Model:** opus\\nPart of epic #174 (Phase D). [opus] Blocked by: #719.","labels":[{"name":"human:approved"}],"comments":[{"body":"## Plan: stub\\n\\nstep one"}]}',
        "738": '{"body":"**Model:** opus\\n**Blocked by:** https://github.com/jakildev/IrredenEngine/pull/202","labels":[{"name":"human:approved"}],"comments":[{"body":"## Plan: stub\\n\\nstep one"}]}',
    }
    print(bodies.get(n3, '{"body":"","labels":[]}'))
    sys.exit(0)
elif args[:2] == ["issue", "edit"]:
    with open(os.environ["EDIT_LOG"], "a") as f:
        f.write(" ".join(args) + "\n")
elif args[:2] == ["pr", "list"]:
    print("[]")  # scope-shipped: no merged coverage
elif args[:2] == ["pr", "view"]:
    print("CLOSED")
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

# Per-issue edit-log line (gh issue edit <N> ...) for assertions. Tolerates
# no-match (empty) without tripping `set -e` / pipefail.
edit_line() { grep -E "(^| )edit ${1}( |$)" "$EDIT_LOG" | head -1 || true; }

echo "=== run fleet-queue-ingest over a stacked epic + unblock candidates ==="
bash "$INGEST" >/dev/null 2>&1 || true

# --- Add path -------------------------------------------------------------
# task 730 (head, no blocker) → fleet:queued, NO fleet:blocked.
l730=$(edit_line 730)
if [[ -n "$l730" && "$l730" == *"fleet:queued"* && "$l730" != *"fleet:blocked"* ]]; then
    ok "head #730 stamped fleet:queued without fleet:blocked"
else
    bad "head #730 mis-stamped: '$l730'"
fi

# task 732 (blocker CLOSED) → fleet:queued, NO fleet:blocked.
l732=$(edit_line 732)
if [[ -n "$l732" && "$l732" == *"fleet:queued"* && "$l732" != *"fleet:blocked"* ]]; then
    ok "#732 (predecessor #718 CLOSED) stamped without fleet:blocked"
else
    bad "#732 mis-stamped: '$l732'"
fi

# task 731 (blocker OPEN) → fleet:queued + fleet:blocked (queued, marked).
l731=$(edit_line 731)
if [[ -n "$l731" && "$l731" == *"fleet:queued"* && "$l731" == *"fleet:blocked"* ]]; then
    ok "#731 (predecessor #719 OPEN) stamped fleet:queued + fleet:blocked"
else
    bad "#731 not queued-with-marker as expected: '$l731'"
fi

# --- Cross-repo routing -------------------------------------------
# task 735 (cross-repo blocker game task 777 CLOSED) → routed to game → NO fleet:blocked.
# If the gate mis-routed to engine, that ref would read OPEN and stamp fleet:blocked.
l735=$(edit_line 735)
if [[ -n "$l735" && "$l735" == *"fleet:queued"* && "$l735" != *"fleet:blocked"* ]]; then
    ok "#735 cross-repo blocker (game#777 CLOSED) routed to game → no fleet:blocked"
else
    bad "#735 cross-repo routing wrong (expected queued, no marker): '$l735'"
fi

# task 736 (cross-repo blocker game task 778 OPEN) → routed to game → fleet:blocked.
l736=$(edit_line 736)
if [[ -n "$l736" && "$l736" == *"fleet:queued"* && "$l736" == *"fleet:blocked"* ]]; then
    ok "#736 cross-repo blocker (game#778 OPEN) → fleet:queued + fleet:blocked"
else
    bad "#736 cross-repo open blocker not marked: '$l736'"
fi

# --- Plain mid-line form -------------------------------------------------
# task 737 declares its only dep as a degraded plain "Blocked by: task 719"
# outside the canonical bold form. The shared parser still catches it →
# fleet:queued + fleet:blocked.
l737=$(edit_line 737)
if [[ -n "$l737" && "$l737" == *"fleet:queued"* && "$l737" == *"fleet:blocked"* ]]; then
    ok "#737 plain 'Blocked by: #719' (epic #174 prose) → fleet:queued + fleet:blocked"
else
    bad "#737 plain-form blocker not marked: '$l737'"
fi

# An explicit PR URL requires MERGED; CLOSED without merge remains blocked.
l738=$(edit_line 738)
if [[ -n "$l738" && "$l738" == *"fleet:queued"* && "$l738" == *"fleet:blocked"* ]]; then
    ok "#738 closed-unmerged PR URL stays fleet:blocked"
else
    bad "#738 PR-URL blocker was treated as satisfied without merge: '$l738'"
fi

# --- Remove path ----------------------------------------------------------
# task 733 (blocker task 717 now CLOSED) → fleet:blocked removed.
l733=$(edit_line 733)
if [[ -n "$l733" && "$l733" == *"--remove-label fleet:blocked"* ]]; then
    ok "#733 (predecessor #717 CLOSED) had fleet:blocked removed"
else
    bad "#733 fleet:blocked NOT removed despite closed blocker: '$l733'"
fi

# task 734 (blocker task 719 still OPEN) → fleet:blocked NOT removed (no edit).
l734=$(edit_line 734)
if [[ -z "$l734" ]]; then
    ok "#734 (predecessor #719 OPEN) left fleet:blocked in place (no edit)"
else
    bad "#734 fleet:blocked wrongly removed while blocker open: '$l734'"
fi

# --- Read-only blocker invariant -----------------------------------------
# The blocker issues themselves must never be edited (we only read their state),
# including the cross-repo blockers (tasks 777/778).
if grep -qE '(^| )edit (717|718|719|777|778)( |$)' "$EDIT_LOG"; then
    bad "a blocker issue (#717/#718/#719/#777/#778) was edited — must only READ blocker state"
else
    ok "blocker issues #717/#718/#719/#777/#778 were never edited (read-only state probe)"
fi

echo
echo "================================"
echo "  PASS: $PASS    FAIL: $FAIL"
echo "================================"
[[ "$FAIL" -eq 0 ]]
