#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR=$(cd "$(dirname "$0")/.." && pwd)
source "$(dirname "$0")/lib_assert.sh"
SUBJECT="$SCRIPT_DIR/fleet-plans"

if [[ ! -f "$SUBJECT" ]]; then
    echo "SKIP: fleet-plans not found at $SUBJECT" >&2
    exit 3
fi

TMPROOT=$(mktemp -d)
trap 'rm -rf "$TMPROOT"' EXIT
export HOME="$TMPROOT/home"
mkdir -p "$HOME/.fleet/plans" "$TMPROOT/bin"

cat > "$TMPROOT/bin/gh" <<'STUB'
#!/usr/bin/env bash
if [[ $# -ne 10 || "$1" != "issue" || "$2" != "list" \
    || "$3" != "--repo" || "$5" != "--state" || "$6" != "all" \
    || "$7" != "--limit" || "$8" != "5000" \
    || "$9" != "--json" || "${10}" != "number,title" ]]; then
    echo "unsupported gh invocation: $*" >&2
    exit 2
fi
case "$4" in
    jakildev/IrredenEngine)
        echo '[{"number":2000,"title":"Engine-only task"},{"number":310,"title":"World Z-yaw rotation across the trixel pipeline"},{"number":137,"title":"Engine issue one thirty-seven"},{"number":42,"title":"Engine forty-two"}]'
        ;;
    jakildev/irreden)
        echo '[{"number":310,"title":"MIDI lattice visualizer"},{"number":137,"title":"Game-only task"},{"number":42,"title":"Game forty-two"}]'
        ;;
    *)
        echo "unknown repo: $4" >&2
        exit 2
        ;;
esac
STUB
chmod +x "$TMPROOT/bin/gh"
export PATH="$TMPROOT/bin:$PATH"

engine_path=$("$SUBJECT" path 310)
game_path=$("$SUBJECT" path 310 --repo game)
assert_eq "$engine_path" "$HOME/.fleet/plans/engine/issue-310.md" \
    "path defaults to engine staging"
assert_eq "$game_path" "$HOME/.fleet/plans/game/issue-310.md" \
    "path scopes game staging separately"

printf '# Plan: only engine\n' > "$HOME/.fleet/plans/issue-2000.md"
printf '# Plan: #310 — World Z-yaw rotation across the trixel pipeline\n' \
    > "$HOME/.fleet/plans/issue-310.md"
printf '# Plan: Game-only task\n' > "$HOME/.fleet/plans/issue-137.md"
printf '# Plan: unrelated\n' > "$HOME/.fleet/plans/issue-42.md"
printf '# Plan: old task\n' > "$HOME/.fleet/plans/T-054.md"

set +e
dry_output=$("$SUBJECT" migrate 2>&1)
dry_rc=$?
set -e
assert_eq "$dry_rc" "1" "dry run reports unresolved legacy plans"
assert_contains "$dry_output" "issue-2000.md -> engine" \
    "dry run resolves an engine-only issue"
assert_contains "$dry_output" "issue-310.md -> engine" \
    "dry run resolves an ambiguous number by engine title"
assert_contains "$dry_output" "issue-137.md -> game" \
    "dry run resolves an ambiguous number by game title"
assert_contains "$dry_output" "issue-42.md -> UNRESOLVED" \
    "dry run refuses an inconclusive title"
assert_contains "$dry_output" "T-054.md -> UNRESOLVED-legacy" \
    "dry run never classifies old task identifiers"
[[ -f "$HOME/.fleet/plans/issue-2000.md" ]] \
    && ok "dry run moves nothing" || bad "dry run moved a file"

set +e
apply_output=$("$SUBJECT" migrate --apply 2>&1)
apply_rc=$?
set -e
assert_eq "$apply_rc" "1" "apply leaves a nonzero unresolved verdict"
[[ -f "$HOME/.fleet/plans/engine/issue-2000.md" ]] \
    && ok "apply moves engine-only plan" || bad "engine-only plan not moved"
[[ -f "$HOME/.fleet/plans/engine/issue-310.md" ]] \
    && ok "apply moves engine title match" || bad "engine title match not moved"
[[ -f "$HOME/.fleet/plans/game/issue-137.md" ]] \
    && ok "apply moves game plan" || bad "game plan not moved"
[[ -f "$HOME/.fleet/plans/issue-42.md" ]] \
    && ok "apply leaves unresolved issue plan" || bad "unresolved issue moved"
[[ -f "$HOME/.fleet/plans/T-054.md" ]] \
    && ok "apply leaves old task plan" || bad "old task plan moved"
assert_contains "$apply_output" "issue-42.md -> UNRESOLVED" \
    "apply explains unresolved issue"

set +e
second_output=$("$SUBJECT" migrate --apply 2>&1)
second_rc=$?
set -e
assert_eq "$second_rc" "1" "second apply remains unresolved and idempotent"
assert_absent "$second_output" "issue-2000.md" \
    "second apply does not revisit moved plans"

rm -f "$HOME/.fleet/plans/issue-42.md" "$HOME/.fleet/plans/T-054.md"
check_output=$("$SUBJECT" check)
assert_eq "$check_output" "" "clean check is quiet"

summarize "fleet-plans CLI tests"
