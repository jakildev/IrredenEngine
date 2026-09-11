#!/usr/bin/env bash
# Exercises both model-label minters over the shared parser corpus (#2833).

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
#!/usr/bin/env bash
body_for() {
    case "$1" in
        2101) printf '%s' '**Model:** sonnet' ;;
        2102) printf '%s' '**Model:** `sonnet`' ;;
        2103) printf '%s' '**Model:** [sonnet]' ;;
        2104) printf '%s' '- **Model:** sonnet' ;;
        2105) printf '%s' '**Suggested Model:** sonnet' ;;
        2106) printf '%s' '**Model:** sonnet (escalate to opus if needed)' ;;
        2107) printf '%s' '**Model:** fable' ;;
        2108) printf '%s' 'No model field.' ;;
        2109) printf '%s' '**Model:** either opus or sonnet' ;;
        2110) printf '%s' '**Model:** TBD' ;;
        2111) printf '%s' '**Area:** tooling · **Owner:** fleet · **Model:** sonnet' ;;
    esac
}

case "$1 $2" in
    "pr list")
        echo '[]'
        ;;
    "issue list")
        python3 - <<'PY'
import json

bodies = {
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
print(json.dumps([
    {"number": number, "labels": [{"name": "fleet:queued"}], "body": body}
    for number, body in bodies.items()
]))
PY
        ;;
    "issue view")
        body=$(body_for "$3")
        BODY="$body" python3 - <<'PY'
import json
import os

print(json.dumps({
    "body": os.environ["BODY"] + "\n**Blocked by:** (none)",
    "labels": [{"name": "human:approved"}, {"name": "fleet:no-plan"}],
}))
PY
        ;;
    "issue edit")
        printf '%s|%s\n' "${MINTER:?}" "$*" >>"$EDIT_LOG"
        ;;
    "issue comment")
        ;;
    *)
        echo "unexpected gh invocation: $*" >&2
        exit 1
        ;;
esac
GHSTUB
chmod +x "$STUB_DIR/gh"
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
