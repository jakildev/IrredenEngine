#!/usr/bin/env bash
# Exercises both model-label minters over the shared parser corpus.

set -euo pipefail

SCRIPT_DIR=$(cd "$(dirname "$0")/.." && pwd)
source "$(dirname "$0")/lib_assert.sh"

INGEST="$SCRIPT_DIR/fleet-queue-ingest"
BACKFILL="$SCRIPT_DIR/fleet-queue-backfill-model-labels"

if [[ ! -x "$INGEST" || ! -x "$BACKFILL" ]]; then
    echo "SKIP: model label minter missing" >&2
    exit 3
fi

TMPROOT=$(mktemp -d)
source "$(dirname "$0")/lib_hermetic.sh"
hermetic_poison_gh_env "$TMPROOT"
trap 'rm -rf "$TMPROOT"' EXIT
export HOME="$TMPROOT/home"
mkdir -p "$HOME/.fleet/state/projections" "$HOME/.fleet/logs"
export EDIT_LOG="$TMPROOT/edit.log"
: >"$EDIT_LOG"

PROJ="$HOME/.fleet/state/projections/queue-manager-ingest.json"
cat >"$PROJ" <<'JSON'
{"pending_issues":[
  {"number":2101,"repo":"engine"},{"number":2102,"repo":"engine"},
  {"number":2103,"repo":"engine"},{"number":2104,"repo":"engine"},
  {"number":2105,"repo":"engine"},{"number":2106,"repo":"engine"},
  {"number":2107,"repo":"engine"},{"number":2108,"repo":"engine"},
  {"number":2109,"repo":"engine"},{"number":2110,"repo":"engine"},
  {"number":2111,"repo":"engine"}
],"unblock_issues":[]}
JSON

STUB_DIR="$TMPROOT/bin"
mkdir -p "$STUB_DIR"
cat >"$STUB_DIR/gh" <<'GHSTUB'
#!/usr/bin/env python3
import json, os, sys

BODIES = {
    2101: "**Model:** sonnet",
    2102: "**Model:** `sonnet`",
    2103: "**Model:** [sonnet]",
    2104: "- **Model:** sonnet",
    2105: "**Suggested Model:** sonnet",
    2106: "**Model:** sonnet (escalate to opus if needed)",
    2107: "**Model:** fable",
    2108: "No model field.",
    2109: "**Model:** either opus or sonnet",
    2110: "**Model:** TBD",
    2111: "**Area:** tooling · **Owner:** fleet · **Model:** sonnet",
}

args = sys.argv[1:]
if args[:2] == ["pr", "list"]:
    print("[]")
elif args[:2] == ["issue", "list"]:
    print(json.dumps([
        {"number": number, "labels": [{"name": "fleet:queued"}], "body": body}
        for number, body in BODIES.items()
    ]))
elif args[:2] == ["issue", "view"]:
    num = int(args[2]) if len(args) > 2 else 0
    body = BODIES.get(num, "")
    print(json.dumps({
        "body": body + "\n**Blocked by:** (none)",
        "labels": [{"name": "human:approved"}, {"name": "fleet:no-plan"}],
    }))
elif args[:2] == ["issue", "edit"]:
    with open(os.environ["EDIT_LOG"], "a", encoding="utf-8") as f:
        f.write(f'{os.environ["MINTER"]}|{" ".join(args)}\n')
elif args[:2] == ["issue", "comment"]:
    pass
else:
    sys.stderr.write(f"unexpected gh invocation: {' '.join(args)}\n")
    sys.exit(1)
GHSTUB
chmod +x "$STUB_DIR/gh"
# Native-Windows twin: fleet-queue-ingest / fleet-queue-backfill-model-labels
# invoke `gh` from PYTHON (subprocess), which cannot exec an extensionless
# shebang script on native-Windows python3 (mingw64) — see
# scripts/fleet/CLAUDE.md's native-Windows PATHEXT rule. Inert on POSIX hosts.
cat >"$STUB_DIR/gh.bat" <<'BATEOF'
@echo off
python3 "%~dp0gh" %*
BATEOF
export PATH="$STUB_DIR:$PATH"

MINTER=ingest bash "$INGEST" >/dev/null
MINTER=backfill bash "$BACKFILL" >/dev/null

expected_label() {
    case "$1" in
        2101|2102|2103|2104|2105|2106|2111) echo "fleet:sonnet" ;;
        2107) echo "fleet:fable" ;;
        2108|2109|2110) echo "fleet:opus" ;;
    esac
}

for issue in {2101..2111}; do
    expected=$(expected_label "$issue")
    ingest_line=$(grep "^ingest|issue edit $issue " "$EDIT_LOG" || true)
    backfill_line=$(grep "^backfill|issue edit $issue " "$EDIT_LOG" || true)
    assert_contains "$ingest_line" "$expected" "ingest #$issue stamps $expected"
    assert_contains "$backfill_line" "$expected" "backfill #$issue stamps $expected"
done

summarize "model field minters"
