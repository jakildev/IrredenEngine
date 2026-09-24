#!/usr/bin/env bash
# Test that fleet-queue-ingest skips fleet:gated issues.
#
# A worker parks an issue fleet:gated when the fix surface is gated
# self-config no class can push (.claude/commands/role-*.md, .claude/agents/*,
# .claude/skills/**/SKILL.md). Because it keeps human:approved, it stays in
# the ingest pending set — so ingest must explicitly NOT re-stamp
# fleet:queued onto it, or the issue re-enters autonomous pickup and every
# matching dispatch re-claims, re-hits the ungateable surface, and releases
# (an unbounded pane-burn loop). A normal human:approved issue in the same
# batch must still be stamped, which proves the harness can stamp and the
# skip is meaningful (same shape as the human:owned / fleet:needs-human
# regression tests).
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
cat > "$PROJ" <<'JSON'
{"pending_issues":[
  {"number":830,"repo":"engine"},
  {"number":831,"repo":"engine"}
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
# issue 830 carries fleet:gated (parked, gated fix surface);
# issue 831 is a normal approved issue.
if args[:2] == ["issue", "view"]:
    num = args[2] if len(args) > 2 else ""
    if num == "830":
        print('{"body":"**Model:** opus\\n**Blocked by:** (none)","labels":[{"name":"human:approved"},{"name":"fleet:gated"}],"comments":[{"body":"## Plan\\nstep 1"}]}')
    elif num == "831":
        print('{"body":"**Model:** opus\\n**Blocked by:** (none)","labels":[{"name":"human:approved"}],"comments":[{"body":"## Plan\\nstep 1"}]}')
    else:
        print('{"body":"","labels":[],"comments":[]}')
elif args[:2] == ["issue", "edit"]:
    with open(os.environ["EDIT_LOG"], "a") as f:
        f.write(" ".join(args) + "\n")
elif args[:2] == ["pr", "list"]:
    print("[]")  # scope-shipped: no merged coverage
sys.exit(0)
GHSTUB
chmod +x "$STUB_DIR/gh"
# Native-Windows twin: fleet-queue-ingest invokes `gh` from PYTHON
# (subprocess), and native-Windows python3 (the mingw64 build the Windows
# fleet host ships) cannot exec an extensionless shebang script — PATH
# lookup falls through to the REAL gh.exe (measured: this is exactly how
# this suite's fixture issues got mutated for real). A .bat twin invoking
# python3 is what Windows PATH resolution finds; MSYS converts $STUB_DIR to
# a Windows PATH entry when exec'ing native binaries. Inert on POSIX hosts
# (nothing resolves *.bat). See scripts/fleet/CLAUDE.md's native-Windows
# PATHEXT rule.
cat > "$STUB_DIR/gh.bat" <<'BATEOF'
@echo off
python3 "%~dp0gh" %*
BATEOF
export PATH="$STUB_DIR:$PATH"

echo "=== run fleet-queue-ingest over a batch with one fleet:gated issue ==="
bash "$INGEST" >/dev/null 2>&1 || true

# issue 831 (normal approved) must be stamped fleet:queued.
if grep -qE '(^| )831( |$)' "$EDIT_LOG"; then
    ok "normal approved #831 was stamped (harness can stamp)"
else
    bad "normal approved #831 was NOT stamped — harness broken, skip test would be vacuous"
fi
if grep -q 'fleet:queued' "$EDIT_LOG" && grep -qE '(^| )831( |$)' "$EDIT_LOG"; then
    ok "#831 stamp carried fleet:queued"
else
    bad "#831 stamp missing fleet:queued"
fi

# issue 830 (fleet:gated) must NOT be touched at all.
if grep -qE '(^| )830( |$)' "$EDIT_LOG"; then
    bad "fleet:gated #830 was edited (should have been skipped): $(grep 830 "$EDIT_LOG")"
else
    ok "fleet:gated #830 was skipped — never re-stamped fleet:queued"
fi

echo
echo "================================"
echo "  PASS: $PASS    FAIL: $FAIL"
echo "================================"
[[ "$FAIL" -eq 0 ]]
