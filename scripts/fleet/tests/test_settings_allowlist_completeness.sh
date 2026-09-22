#!/usr/bin/env bash
# scripts/fleet/tests/test_settings_allowlist_completeness.sh — every wrapper
# install.sh symlinks into ~/bin that a fleet role is instructed to run has a
# `Bash(<name>:*)` entry in the checked-in .claude/settings.json allowlist.
#
# Prefix entries do not nest (`Bash(fleet-pr:*)` does not cover
# `fleet-pr-overlap`), so each wrapper needs its own line. A wrapper without
# one prompts in every headless pane, and the pane's interactively-accumulated
# settings.local.json is the only record — nothing in the repo guarantees or
# records per-pane capability. This suite is that record: the population is
# install.sh's `_SRC` registry (the tools that reach ~/bin), minus the exempt
# set below, and each member must carry an allow entry.
#
# Reads install.sh and settings.json as text; never executes either.

set -euo pipefail

SCRIPT_DIR=$(cd "$(dirname "$0")/.." && pwd)
REPO_ROOT=$(cd "$SCRIPT_DIR/../.." && pwd)
INSTALL_SH="$SCRIPT_DIR/install.sh"
SETTINGS_JSON="$REPO_ROOT/.claude/settings.json"

source "$(dirname "$0")/lib_assert.sh"

for subject in "$INSTALL_SH" "$SETTINGS_JSON"; do
    if [[ ! -f "$subject" ]]; then
        echo "SKIP: $subject not found" >&2
        exit 3
    fi
done

# Exempt: registered in install.sh but never a fleet role's Bash step. Each
# reason names the invoker; a wrapper that a role doc starts instructing
# leaves this list and gains an allow entry in the same change.
EXEMPT=(
    "fleet-common.sh"                    # sourced library
    "fleet-clone-freshness.sh"           # sourced library
    "fleet-net.sh"                       # sourced library
    "timeout-shim.py"                    # invoked by path from the timeout shim
    "fleet-codex"                        # Codex host setup; --check/--doctor are human steps
    "fleet-gh-poll"                      # the scout's internal conditional-GET seam
    "fleet-gh-token"                     # token plumbing, exported by wrappers
    "fleet-notify"                       # cron push half (FLEET.md "The push half")
    "fleet-digest-tick"                  # cron push half
    "fleet-session-track"                # hook wired by fleet-up / fleet-babysit
    "fleet-stalled-sweep"                # scout-invoked idle sweep
    "fleet-queue-backfill-model-labels"  # one-time human backfill
    "solo-architect"                     # human pane launcher
    "review-fleet-feedback"              # the cue-only skill of that name calls it by
                                         # relative path, which a bare-name entry never matches
)

is_exempt() {
    local name="$1" e
    for e in "${EXEMPT[@]}"; do
        [[ "$name" == "$e" ]] && return 0
    done
    return 1
}

# registered_wrappers <install_sh> — basename of every top-level
# `<NAME>_SRC="$SCRIPT_DIR/<file>"` declaration (completions/* excluded).
registered_wrappers() {
    grep -oE '_SRC="\$SCRIPT_DIR/[^"/]+"' "$1" | sed -E 's#.*SCRIPT_DIR/##; s#"$##'
}

# allowed_prefixes <settings_json> — the <name> of every `Bash(<name>:*)`
# entry under permissions.allow, one per line. JSON is parsed, not grepped,
# so a reformatted file reads the same.
allowed_prefixes() {
    python3 - "$1" <<'PY'
import json, re, sys
allow = json.load(open(sys.argv[1]))["permissions"]["allow"]
for entry in allow:
    m = re.fullmatch(r"Bash\(([^:)]+):\*\)", entry)
    if m:
        print(m.group(1))
PY
}

# missing_entries <install_sh> <settings_json> — registered, non-exempt
# wrappers with no allow entry, one per line.
missing_entries() {
    comm -23 \
        <(registered_wrappers "$1" | sort -u) \
        <(allowed_prefixes "$2" | sort -u) \
        | while IFS= read -r name; do
            is_exempt "$name" || echo "$name"
        done
}

echo "T1: every registered, non-exempt wrapper has a Bash allow entry"
missing=$(missing_entries "$INSTALL_SH" "$SETTINGS_JSON")
if [[ -z "$missing" ]]; then
    ok "no registered wrapper lacks a Bash(<name>:*) entry"
else
    bad "wrapper(s) with no allow entry in .claude/settings.json: $(echo "$missing" | tr '\n' ' ')"
fi

echo "T2: the exempt list names only registered wrappers"
registered=$(registered_wrappers "$INSTALL_SH")
for e in "${EXEMPT[@]}"; do
    if grep -qxF "$e" <<<"$registered"; then
        ok "exempt $e is still registered"
    else
        bad "exempt $e is not in install.sh's registry — drop the stale exemption"
    fi
done

echo "T3: the exempt list and the allowlist do not overlap"
allowed=$(allowed_prefixes "$SETTINGS_JSON")
for e in "${EXEMPT[@]}"; do
    if grep -qxF "$e" <<<"$allowed"; then
        bad "exempt $e also has an allow entry — it is agent-invoked, drop the exemption"
    else
        ok "exempt $e has no allow entry"
    fi
done

TMPROOT=$(mktemp -d)
trap 'rm -rf "$TMPROOT"' EXIT

echo "T4: negative control — dropping one allow entry is reported by name"
python3 - "$SETTINGS_JSON" "$TMPROOT/settings.json" <<'PY'
import json, sys
d = json.load(open(sys.argv[1]))
d["permissions"]["allow"] = [e for e in d["permissions"]["allow"] if e != "Bash(fleet-positive-control:*)"]
json.dump(d, open(sys.argv[2], "w"))
PY
missing=$(missing_entries "$INSTALL_SH" "$TMPROOT/settings.json")
assert_contains "$missing" "fleet-positive-control" "the dropped wrapper is reported"
assert_absent "$missing" "fleet-common.sh" "an exempt library is not reported"

echo "T5: negative control — a newly registered wrapper with no entry is reported"
cp "$INSTALL_SH" "$TMPROOT/install.sh"
echo 'FLEET_BRAND_NEW_SRC="$SCRIPT_DIR/fleet-brand-new-tool"' >> "$TMPROOT/install.sh"
missing=$(missing_entries "$TMPROOT/install.sh" "$SETTINGS_JSON")
assert_contains "$missing" "fleet-brand-new-tool" "the unregistered-in-allowlist wrapper is reported"

summarize "settings allowlist completeness tests"
