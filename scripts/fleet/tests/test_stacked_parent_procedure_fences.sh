#!/usr/bin/env bash
# Execute the merged-parent command fences in the author and reviewer procedures.

set -euo pipefail

SCRIPT_DIR=$(cd "$(dirname "$0")/.." && pwd)
source "$(dirname "$0")/lib_preflight.sh"
source "$(dirname "$0")/lib_assert.sh"
ROOT=$(cd "$SCRIPT_DIR/../.." && pwd)

TMPROOT=""
cleanup() { [[ -n "$TMPROOT" && -d "$TMPROOT" ]] && rm -rf "$TMPROOT"; }
trap cleanup EXIT
TMPROOT=$(mktemp -d)
source "$(dirname "$0")/lib_hermetic.sh"
hermetic_poison_gh_env "$TMPROOT"
mkdir -p "$TMPROOT/bin"
export PATH="$TMPROOT/bin:$PATH"
export EVENT_LOG="$TMPROOT/events.log"

extract_fence() {
    python3 - "$1" "$2" "$3" <<'PY'
import pathlib
import sys

source, heading, output = sys.argv[1:]
text = pathlib.Path(source).read_text().split(heading, 1)[1]
fence = text.split("```bash\n", 1)[1].split("```", 1)[0]
pathlib.Path(output).write_text(fence)
PY
}

STACKABLE="$ROOT/.claude/skills/commit-and-push/procedures/stackable-on.md"
NATIVE="$ROOT/.claude/skills/commit-and-push/procedures/native-stack-link.md"
REVIEW="$ROOT/.claude/skills/review-pr/procedures/stacked-pr-review.md"
extract_fence "$STACKABLE" "2. Base and claim→open repair:" "$TMPROOT/stackable.sh"
extract_fence "$STACKABLE" "## Open (or reconcile)" "$TMPROOT/open.sh"
extract_fence "$NATIVE" "Idempotent:" "$TMPROOT/native.sh"
extract_fence "$REVIEW" "## Merged-parent gate" "$TMPROOT/review.sh"
sed -i.bak 's/"<N>"/"42"/g' "$TMPROOT/stackable.sh"
sed -i.bak 's/^base=<the PR.*/base="parent"/; s/^child_pr=.*/child_pr=200/' "$TMPROOT/native.sh"
sed -i.bak 's/^base=<the child.*/base="parent"/; s/^child_pr=.*/child_pr=200/' "$TMPROOT/review.sh"

cat > "$TMPROOT/bin/fleet-claim" <<'STUB'
#!/usr/bin/env bash
if [[ "$MODE" == merged ]]; then
    echo "fleet-claim claim-base: blocker PR #90 merged after the claim; run: git rebase --onto origin/master abc123" >&2
    echo master
else
    echo parent
fi
STUB
chmod +x "$TMPROOT/bin/fleet-claim"

cat > "$TMPROOT/bin/git" <<'STUB'
#!/usr/bin/env bash
case "$1 $2" in
    "branch --show-current") echo child ;;
    "rev-parse refs/remotes/origin/child") echo oldsha ;;
    "rebase --onto") echo rebase >> "$EVENT_LOG" ;;
    "push origin") echo push >> "$EVENT_LOG" ;;
    *) exit 90 ;;
esac
STUB
chmod +x "$TMPROOT/bin/git"

cat > "$TMPROOT/bin/gh" <<'STUB'
#!/usr/bin/env python3
import json
import os
import sys

args = sys.argv[1:]
mode = os.environ["MODE"]
log = os.environ["EVENT_LOG"]

def event(value):
    with open(log, "a", encoding="utf-8") as handle:
        handle.write(value + "\n")

if args[:2] == ["pr", "list"]:
    if "--head" in args and args[args.index("--head") + 1] == "parent":
        merged = {"number": 90, "state": "MERGED", "headRefOid": "abc123"}
        opened = {"number": 91, "state": "OPEN", "headRefOid": "def456"}
        rows = []
        if mode in {"merged", "both"}:
            rows.append(merged)
        if mode in {"open", "both", "page2"}:
            rows.append(opened)
        print(json.dumps(rows))
    else:
        pass
    raise SystemExit(0)
if args[:1] == ["api"]:
    query = args[args.index("--jq") + 1]
    if query.startswith("any("):
        print("true" if mode == "linked" else "false")
    else:
        print("77" if mode == "page2" else "null")
    raise SystemExit(0)
if args[:2] == ["stack", "link"]:
    event("link:" + ":".join(args[2:]))
    raise SystemExit(0)
if args[:2] == ["pr", "create"]:
    event("create:" + args[args.index("--base") + 1])
    print("https://example.test/pr/200")
    raise SystemExit(0)
if args[:2] == ["pr", "edit"]:
    if "--base" in args:
        event("retarget:" + args[args.index("--base") + 1])
    else:
        event("edit")
    raise SystemExit(0)
raise SystemExit(91)
STUB
chmod +x "$TMPROOT/bin/gh"

echo "T1: stackable-on merged row replays and pushes before PR creation"
export MODE=merged
: > "$EVENT_LOG"
bash -c 'source "$1"; source "$2"' _ "$TMPROOT/stackable.sh" "$TMPROOT/open.sh" >/dev/null
assert_eq "$(cat "$EVENT_LOG")" $'rebase\npush\ncreate:master' \
    "claim→open merged repair is ordered"

echo "T2: stackable-on open row does not replay"
export MODE=open
: > "$EVENT_LOG"
bash -c 'source "$1"; source "$2"' _ "$TMPROOT/stackable.sh" "$TMPROOT/open.sh" >/dev/null
assert_eq "$(cat "$EVENT_LOG")" "create:parent" "open parent retains its feature base"

run_native() {
    export MODE="$1"
    : > "$EVENT_LOG"
    bash "$TMPROOT/native.sh" >"$TMPROOT/native.out" 2>"$TMPROOT/native.err"
}

echo "T3: native link state table"
run_native open
assert_eq "$(cat "$EVENT_LOG")" "link:91:200" "open parent creates a stack"
run_native merged
assert_eq "$(cat "$EVENT_LOG")" $'rebase\npush\nretarget:master' \
    "merged parent repairs in D2 order"
run_native both
assert_eq "$(cat "$EVENT_LOG")" "link:91:200" "open parent wins over merged history"
run_native none
assert_eq "$(cat "$EVENT_LOG")" "" "missing parent skips without mutation"
assert_contains "$(cat "$TMPROOT/native.err")" "skipping link" "missing parent reports the skip"
run_native page2
assert_eq "$(cat "$EVENT_LOG")" "link:77:200" "second-page parent stack is joined"

run_review() {
    export MODE="$1"
    : > "$EVENT_LOG"
    set +e
    bash "$TMPROOT/review.sh" >"$TMPROOT/review.out" 2>"$TMPROOT/review.err"
    REVIEW_RC=$?
    set -e
}

echo "T4: reviewer rejects only an unlinked merged-parent child"
run_review merged
assert_eq "$REVIEW_RC" 1 "orphan review fence fails"
assert_contains "$(cat "$TMPROOT/review.err")" "not approvable" "orphan verdict is explicit"
run_review open
assert_eq "$REVIEW_RC" 0 "open-parent child remains approvable"
run_review linked
assert_eq "$REVIEW_RC" 0 "native-stack member remains approvable"

summarize
