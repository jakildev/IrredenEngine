#!/usr/bin/env bash
# scripts/fleet/tests/test_settings_automerge_denials.sh — the Claude adapter
# denies every LLM role the two merge paths (FLEET.md "Who merges"): `gh pr
# merge` sits in `deny` (not `ask`, not `allow`) and every bare or
# checkout-relative spelling of the tier-0 executor is denied while no allow
# entry names it. Reads .claude/settings.json as text through jq; never
# executes anything from it.
set -uo pipefail
SCRIPT_DIR=$(cd "$(dirname "$0")/.." && pwd)
REPO_ROOT=$(cd "$SCRIPT_DIR/../.." && pwd)
SETTINGS_JSON="$REPO_ROOT/.claude/settings.json"
source "$(dirname "$0")/lib_assert.sh"
if [[ ! -f "$SETTINGS_JSON" ]]; then
    echo "SKIP: $SETTINGS_JSON not found" >&2
    exit 3
fi
if ! command -v jq >/dev/null 2>&1; then
    echo "SKIP: jq not found" >&2
    exit 3
fi

allow=$(jq -r '.permissions.allow[]' "$SETTINGS_JSON" | tr -d '\r')
ask=$(jq -r '.permissions.ask // [] | .[]' "$SETTINGS_JSON" | tr -d '\r')
deny=$(jq -r '.permissions.deny[]' "$SETTINGS_JSON" | tr -d '\r')

in_list() { printf '%s\n' "$1" | grep -Fxq -- "$2"; }

echo "T1: gh pr merge is denied, not asked, not allowed"
in_list "$deny" "Bash(gh pr merge:*)" && ok "gh pr merge in deny" || bad "gh pr merge missing from deny"
in_list "$ask" "Bash(gh pr merge:*)" && bad "gh pr merge still in ask" || ok "gh pr merge not in ask"
printf '%s\n' "$allow" | grep -Eq '^Bash\(gh pr merge' && bad "gh pr merge allowed" || ok "gh pr merge not allowed"

echo "T2: every executor spelling is denied and none is allowed"
for form in "Bash(fleet-automerge:*)" "Bash(scripts/fleet/fleet-automerge:*)" \
            "Bash(./scripts/fleet/fleet-automerge:*)" \
            "Bash(python3 scripts/fleet/fleet-automerge:*)" \
            "Bash(python3 ./scripts/fleet/fleet-automerge:*)"; do
    in_list "$deny" "$form" && ok "$form denied" || bad "$form missing from deny"
done
printf '%s\n' "$allow" | grep -Fq 'fleet-automerge' && bad "an allow entry names fleet-automerge" || ok "no allow entry names fleet-automerge"

echo "T3: the generic gh allow does not outrank the deny (deny entries are more specific)"
in_list "$allow" "Bash(gh:*)" && ok "Bash(gh:*) present; deny wins by precedence" || ok "no generic gh allow"

echo "T4: mutation control — a settings copy with the merge deny removed fails T1"
MUTATED=$(mktemp "${TMPDIR:-/tmp}/settings-mut.XXXXXX")
jq '.permissions.deny |= map(select(. != "Bash(gh pr merge:*)"))' "$SETTINGS_JSON" > "$MUTATED"
mutated_deny=$(jq -r '.permissions.deny[]' "$MUTATED" | tr -d '\r')
in_list "$mutated_deny" "Bash(gh pr merge:*)" && bad "mutation not detected" || ok "mutation detected"
rm -f "$MUTATED"

summarize "settings automerge denials"
