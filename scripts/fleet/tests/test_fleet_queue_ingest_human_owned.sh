#!/usr/bin/env bash
# Test that fleet-queue-ingest skips human:owned issues.
#
# A human can de-queue an issue by stamping human:owned (and removing
# fleet:queued). Because it keeps human:approved, it stays in the ingest
# pending set — so ingest must explicitly NOT re-stamp fleet:queued onto it.
# A normal human:approved issue in the same batch must still be stamped, which
# proves the harness can stamp and the skip is meaningful.
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
  {"number":710,"repo":"engine"},
  {"number":711,"repo":"engine"}
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
# issue 710 is human-owned (de-queued); issue 711 is a normal approved issue.
if args[:2] == ["issue", "view"]:
    n = args[2] if len(args) > 2 else ""
    if n == "710":
        print('{"body":"**Model:** opus\\n**Blocked by:** (none)","labels":[{"name":"human:approved"},{"name":"human:owned"}]}')
    elif n == "711":
        print('{"body":"**Model:** opus\\n**Blocked by:** (none)","labels":[{"name":"human:approved"}],"comments":[{"body":"## Plan: stub\\n\\nstep one"}]}')
    else:
        print('{"body":"","labels":[]}')
elif args[:2] == ["issue", "edit"]:
    with open(os.environ["EDIT_LOG"], "a") as f:
        f.write(" ".join(args) + "\n")
elif args[:2] == ["pr", "list"]:
    print("[]")  # scope-shipped: no merged coverage
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

echo "=== run fleet-queue-ingest over a batch with one human:owned issue ==="
bash "$INGEST" >/dev/null 2>&1 || true

# issue 711 (normal approved) must be stamped fleet:queued.
if grep -qE '(^| )711( |$)' "$EDIT_LOG"; then
    ok "normal approved #711 was stamped (harness can stamp)"
else
    bad "normal approved #711 was NOT stamped — harness broken, skip test would be vacuous"
fi
if grep -q 'fleet:queued' "$EDIT_LOG" && grep -qE '(^| )711( |$)' "$EDIT_LOG"; then
    ok "#711 stamp carried fleet:queued"
else
    bad "#711 stamp missing fleet:queued"
fi

# issue 710 (human:owned) must NOT be touched at all.
if grep -qE '(^| )710( |$)' "$EDIT_LOG"; then
    bad "human:owned #710 was edited (should have been skipped): $(grep 710 "$EDIT_LOG")"
else
    ok "human:owned #710 was skipped — never re-stamped fleet:queued"
fi

echo
echo "================================"
echo "  PASS: $PASS    FAIL: $FAIL"
echo "================================"
[[ "$FAIL" -eq 0 ]]
