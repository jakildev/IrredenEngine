#!/usr/bin/env bash
# Tests for fleet-up's write_worktree_settings regeneration semantics on the
# "hooks" key. The invariant under test: regenerating a worktree's
# .claude/settings.local.json preserves hand-added hooks (a human's or the
# update-config skill's) exactly like user-granted permissions.allow entries,
# while the fleet's own fleet-session-track hook is replaced — never
# duplicated — even when a stale variant of it is already in the file.
#
# The function's JSON logic lives in a python3 heredoc inside fleet-up; the
# bash wrapper around it only mkdir-s and passes args. The test extracts that
# heredoc body (anchored to the function name, so a second heredoc elsewhere
# in fleet-up can't be picked up by mistake) and drives it directly against
# sandboxed settings files — no live ~/.fleet, no GitHub.
#
# Covers:
#   - fresh write installs the fleet SessionStart hook with the command -v guard
#   - hand-added hooks survive regeneration (other events AND extra
#     SessionStart groups), alongside preserved allow entries
#   - a stale fleet-session-track variant is replaced by the current command,
#     not duplicated
#   - malformed JSON falls back to a clean baseline write
#   - the FLEET_ASSIGNED_WORKTREE env key + the PreToolUse edit-guard hook are
#     emitted, the guard command references the engine clone by absolute path,
#     and both keys carry #2284 preservation: hand-added env entries + non-fleet
#     PreToolUse groups survive regeneration, a stale fleet guard-hook variant is
#     replaced not duplicated, and fleet wins only on its own env key (#2402)

set -euo pipefail

SCRIPT_DIR=$(cd "$(dirname "$0")/.." && pwd)
FLEET_UP="$SCRIPT_DIR/fleet-up"

if [[ ! -f "$FLEET_UP" ]]; then
    echo "test setup: fleet-up not found at $FLEET_UP" >&2
    exit 1
fi

PY_SRC=$(sed -n '/^write_worktree_settings() {$/,/^PYEOF$/p' "$FLEET_UP" \
    | sed -n "/<<'PYEOF'/,/^PYEOF\$/p" | sed '1d;$d')
if [[ -z "$PY_SRC" ]]; then
    echo "test setup: could not extract the settings heredoc from fleet-up" >&2
    exit 1
fi

PASS=0
FAIL=0
TMPROOT=""

cleanup() {
    [[ -n "$TMPROOT" && -d "$TMPROOT" ]] && rm -rf "$TMPROOT"
}
trap cleanup EXIT

assert_eq() {
    local actual="$1" expected="$2" msg="$3"
    if [[ "$actual" == "$expected" ]]; then
        PASS=$((PASS + 1)); echo "  ok: $msg"
    else
        FAIL=$((FAIL + 1)); echo "  FAIL: $msg"
        echo "        expected: $expected"
        echo "        actual:   $actual"
    fi
}

TMPROOT=$(mktemp -d -t fleet-settings-hooks)

# Regenerate <settings-file> the way fleet-up does. repo_root is inert here;
# the worktree (argv[3], → FLEET_ASSIGNED_WORKTREE) and engine root (argv[4], →
# guard hook path) default to sandbox stand-ins so existing single-arg callers
# keep working. Pass args 2/3 to override for the env/guard-path assertions.
#   regen <settings-file> [worktree] [engine-root]
DEFAULT_WT="$TMPROOT/eng/.claude/worktrees/worker-9"
DEFAULT_ENG="$TMPROOT/eng"
regen() {
    printf '%s\n' "$PY_SRC" | python3 - "$1" "$TMPROOT/repo" \
        "${2:-$DEFAULT_WT}" "${3:-$DEFAULT_ENG}"
}

# q <settings-file> <python expr over parsed dict d> — prints the expr value.
q() {
    python3 -c "import json,sys; d=json.load(open(sys.argv[1])); print(eval(sys.argv[2]))" "$1" "$2"
}

# --- T1: fresh write installs the guarded fleet hook -------------------------
echo "T1: fresh write installs the fleet SessionStart hook"
F1="$TMPROOT/t1/settings.local.json"; mkdir -p "$TMPROOT/t1"
regen "$F1"
assert_eq "$(q "$F1" "len(d['hooks']['SessionStart'])")" "1" \
    "fresh SessionStart has exactly the fleet group"
assert_eq "$(q "$F1" "'command -v fleet-session-track' in d['hooks']['SessionStart'][0]['hooks'][0]['command']")" \
    "True" "fleet hook command carries the command -v guard"

# --- T2: hand-added hooks + allow entries survive regeneration ---------------
echo "T2: hand-added hooks survive regeneration alongside extra allows"
F2="$TMPROOT/t2/settings.local.json"; mkdir -p "$TMPROOT/t2"
python3 - "$F2" <<'SEED'
import json, sys
json.dump({
    "permissions": {"allow": ["Bash(cargo:*)"]},
    "hooks": {
        # stale variant of the fleet's own hook: no command -v guard
        "SessionStart": [
            {"hooks": [{"type": "command", "command": "fleet-session-track"}]},
            {"hooks": [{"type": "command", "command": "echo hand-added-start"}]},
        ],
        "Stop": [
            {"matcher": "*", "hooks": [{"type": "command", "command": "echo hand-added-stop"}]}
        ],
    },
}, open(sys.argv[1], "w"))
SEED
regen "$F2"
assert_eq "$(q "$F2" "len([g for g in d['hooks']['SessionStart'] if 'fleet-session-track' in str(g)])")" \
    "1" "exactly one fleet-session-track group after regen (stale variant replaced)"
assert_eq "$(q "$F2" "'command -v fleet-session-track' in str(d['hooks']['SessionStart'])")" \
    "True" "surviving fleet group is the current guarded command"
assert_eq "$(q "$F2" "'echo hand-added-start' in str(d['hooks']['SessionStart'])")" \
    "True" "hand-added SessionStart group preserved"
assert_eq "$(q "$F2" "d['hooks']['Stop'][0]['hooks'][0]['command']")" \
    "echo hand-added-stop" "hand-added Stop event preserved wholesale"
assert_eq "$(q "$F2" "'Bash(cargo:*)' in d['permissions']['allow']")" \
    "True" "user-granted allow entry still preserved"

# --- T3: second regen is idempotent on the preserved hooks --------------------
echo "T3: regeneration is idempotent"
before=$(q "$F2" "sorted(d['hooks'])"); regen "$F2"
assert_eq "$(q "$F2" "sorted(d['hooks'])")" "$before" "hook event set unchanged on re-run"
assert_eq "$(q "$F2" "len(d['hooks']['SessionStart'])")" "2" \
    "SessionStart group count stable across regens"

# --- T4: malformed JSON falls back to a clean baseline ------------------------
echo "T4: malformed settings file regenerates baseline"
F4="$TMPROOT/t4/settings.local.json"; mkdir -p "$TMPROOT/t4"
echo '{not json' > "$F4"
regen "$F4"
assert_eq "$(q "$F4" "sorted(d['hooks'])")" "['PreToolUse', 'SessionStart']" \
    "malformed file yields fleet-only hooks (SessionStart + PreToolUse guard)"
assert_eq "$(q "$F4" "d['permissions']['defaultMode']")" "auto" \
    "baseline permissions written"
assert_eq "$(q "$F4" "'FLEET_ASSIGNED_WORKTREE' in d.get('env', {})")" "True" \
    "baseline write still emits the env assignment key"

# --- T5: fresh write emits the env assignment + PreToolUse edit guard ---------
echo "T5: fresh write emits FLEET_ASSIGNED_WORKTREE env + the PreToolUse guard"
F5="$TMPROOT/t5/settings.local.json"; mkdir -p "$TMPROOT/t5"
regen "$F5" "/eng/.claude/worktrees/worker-3" "/eng"
assert_eq "$(q "$F5" "d['env']['FLEET_ASSIGNED_WORKTREE']")" "/eng/.claude/worktrees/worker-3" \
    "env carries the assigned worktree path (argv[3])"
assert_eq "$(q "$F5" "d['hooks']['PreToolUse'][0]['matcher']")" "Edit|Write|MultiEdit" \
    "PreToolUse guard matches Edit|Write|MultiEdit"
assert_eq "$(q "$F5" "'/eng/scripts/fleet/fleet-guard-worktree-edit' in d['hooks']['PreToolUse'][0]['hooks'][0]['command']")" \
    "True" "guard command references the engine clone by absolute path (argv[4], not repo_root)"
assert_eq "$(q "$F5" "d['hooks']['PreToolUse'][0]['hooks'][0]['command'].endswith('|| true')")" \
    "True" "guard command is wrapped to fail OPEN on a missing script"

# --- T6: hand-added env survives; fleet wins only on its own env key ----------
echo "T6: hand-added env entries survive regeneration; fleet key wins"
F6="$TMPROOT/t6/settings.local.json"; mkdir -p "$TMPROOT/t6"
python3 - "$F6" <<'SEED'
import json, sys
json.dump({
    "env": {"MY_TOKEN": "keep-me", "FLEET_ASSIGNED_WORKTREE": "/stale/path"},
}, open(sys.argv[1], "w"))
SEED
regen "$F6" "/eng/.claude/worktrees/worker-7" "/eng"
assert_eq "$(q "$F6" "d['env']['MY_TOKEN']")" "keep-me" \
    "hand-added env entry preserved"
assert_eq "$(q "$F6" "d['env']['FLEET_ASSIGNED_WORKTREE']")" "/eng/.claude/worktrees/worker-7" \
    "fleet key overrides a stale hand value (fleet wins only on its own key)"

# --- T7: stale fleet guard-hook variant replaced; hand PreToolUse preserved ---
echo "T7: stale fleet guard-hook variant replaced, non-fleet PreToolUse preserved"
F7="$TMPROOT/t7/settings.local.json"; mkdir -p "$TMPROOT/t7"
python3 - "$F7" <<'SEED'
import json, sys
json.dump({
    "hooks": {
        "PreToolUse": [
            # stale variant of the fleet's own guard (bare, no [ -x ] wrapper)
            {"matcher": "Edit|Write", "hooks": [{"type": "command", "command": "fleet-guard-worktree-edit"}]},
            # a human's own PreToolUse hook — must survive
            {"matcher": "Bash", "hooks": [{"type": "command", "command": "echo hand-added-pre"}]},
        ],
    },
}, open(sys.argv[1], "w"))
SEED
regen "$F7" "/eng/.claude/worktrees/worker-3" "/eng"
assert_eq "$(q "$F7" "len([g for g in d['hooks']['PreToolUse'] if 'fleet-guard-worktree-edit' in str(g)])")" \
    "1" "exactly one fleet guard group after regen (stale variant replaced)"
assert_eq "$(q "$F7" "'echo hand-added-pre' in str(d['hooks']['PreToolUse'])")" \
    "True" "hand-added non-fleet PreToolUse group preserved"

echo
echo "passed: $PASS  failed: $FAIL"
[[ "$FAIL" -eq 0 ]]
