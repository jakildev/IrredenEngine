#!/usr/bin/env bash
# Tests for the ir-build / ir-run build-dir resolution helpers
# (engine/tools/lib/concurrency_helpers.sh).
#
# #1669 made downstream-creation worktree builds first-class: a git repo
# rooted at creations/<name>/.claude/worktrees/<agent>/ has no CMake
# presets of its own, so its build routes to the enclosing engine root at
# build-<creation>-<agent>/ and ir-build auto-configures that dir with
# -DIRREDEN_USER_PROJECTS=<worktree>. These tests pin the pure path
# resolution (ir_enclosing_engine_root / ir_creation_worktree_engine_root /
# ir_default_build_dir) against a fake directory layout — no cmake. T4 does
# a local `git init` (no network) to reproduce #3046's spelling split; every
# other test is pure filesystem, no git.
#
# Covers:
#   - engine checkout (presets at root) → <root>/build (unchanged)
#   - engine nested worktree (own presets) → <worktree>/build (unchanged)
#   - creation worktree under <engine>/creations/<name>/.claude/worktrees/
#     <agent>/ → <engine>/build-<name>-<agent> (the #1669 behavior)
#   - repo outside any engine tree → <root>/build (unchanged)
#   - presets-less dir under the engine but NOT under creations/ →
#     <root>/build (no false-positive creation detection)
#   - ir_enclosing_engine_root walk-up and miss cases
#   - mixed Windows-drive vs POSIX-drive spelling of the same worktree root
#     does not break detection or build-dir derivation (#3046)
#   - the ancestor walk terminates on a root spelling that is neither "." nor
#     "/" (a bare Windows drive root, where MSYS2's `dirname` is idempotent)

set -euo pipefail

SCRIPT_DIR=$(cd "$(dirname "$0")/../../.." && pwd)
HELPERS="$SCRIPT_DIR/engine/tools/lib/concurrency_helpers.sh"

if [[ ! -f "$HELPERS" ]]; then
    echo "test setup: helpers not found at $HELPERS" >&2
    exit 1
fi
# Isolate the helpers' lock-dir side effects from the host's live locks.
IR_LOCK_ROOT="$(mktemp -d)/locks"
export IR_LOCK_ROOT
# shellcheck source=../../../engine/tools/lib/concurrency_helpers.sh
source "$HELPERS"

PASS=0
FAIL=0

assert_eq() {
    local actual="$1" expected="$2" msg="$3"
    if [[ "$actual" == "$expected" ]]; then
        PASS=$((PASS + 1))
        echo "  ok: $msg"
    else
        FAIL=$((FAIL + 1))
        echo "  FAIL: $msg"
        echo "        expected: $expected"
        echo "        actual:   $actual"
    fi
}

assert_rc() {
    local rc="$1" expected="$2" msg="$3"
    assert_eq "$rc" "$expected" "$msg"
}

# --- Fake layout -------------------------------------------------------------
FAKE="$(mktemp -d)"
trap 'rm -rf "$FAKE" "${IR_LOCK_ROOT%/locks}"' EXIT

ENG="$FAKE/eng"
mkdir -p "$ENG/engine"
touch "$ENG/CMakePresets.json"

ENG_WT="$ENG/.claude/worktrees/opus-worker-1"
mkdir -p "$ENG_WT/engine"
touch "$ENG_WT/CMakePresets.json"

CREATION_WT="$ENG/creations/game/.claude/worktrees/agent-1"
mkdir -p "$CREATION_WT"

NON_CREATION="$ENG/.fleet/scratch-repo"
mkdir -p "$NON_CREATION"

OUTSIDE="$FAKE/elsewhere/repo"
mkdir -p "$OUTSIDE"

# --- T1: ir_enclosing_engine_root --------------------------------------------
echo "T1: ir_enclosing_engine_root walk-up"
assert_eq "$(ir_enclosing_engine_root "$CREATION_WT")" "$ENG" \
    "creation worktree walks up to the engine root"
assert_eq "$(ir_enclosing_engine_root "$ENG")" "$ENG" \
    "engine root resolves to itself"
rc=0; ir_enclosing_engine_root "$OUTSIDE" >/dev/null || rc=$?
assert_rc "$rc" 1 "dir outside any engine tree returns 1"

# --- T2: ir_creation_worktree_engine_root ------------------------------------
echo "T2: ir_creation_worktree_engine_root detection"
assert_eq "$(ir_creation_worktree_engine_root "$CREATION_WT")" "$ENG" \
    "creation worktree detected, echoes enclosing engine root"
rc=0; ir_creation_worktree_engine_root "$ENG" >/dev/null || rc=$?
assert_rc "$rc" 1 "engine root (has presets) is not a creation worktree"
rc=0; ir_creation_worktree_engine_root "$ENG_WT" >/dev/null || rc=$?
assert_rc "$rc" 1 "engine nested worktree (own presets) is not a creation worktree"
rc=0; ir_creation_worktree_engine_root "$NON_CREATION" >/dev/null || rc=$?
assert_rc "$rc" 1 "presets-less repo under the engine but outside creations/ is not detected"
rc=0; ir_creation_worktree_engine_root "$OUTSIDE" >/dev/null || rc=$?
assert_rc "$rc" 1 "repo outside the engine tree is not detected"

# --- T3: ir_default_build_dir ------------------------------------------------
echo "T3: ir_default_build_dir routing"
assert_eq "$(ir_default_build_dir "$ENG")" "$ENG/build" \
    "engine checkout builds in-tree"
assert_eq "$(ir_default_build_dir "$ENG_WT")" "$ENG_WT/build" \
    "engine nested worktree builds in-tree"
assert_eq "$(ir_default_build_dir "$CREATION_WT")" "$ENG/build-game-agent-1" \
    "creation worktree routes to <engine>/build-<creation>-<agent>"
assert_eq "$(ir_default_build_dir "$NON_CREATION")" "$NON_CREATION/build" \
    "non-creation repo under the engine builds in-tree"
assert_eq "$(ir_default_build_dir "$OUTSIDE")" "$OUTSIDE/build" \
    "repo outside the engine tree builds in-tree"

# --- T4: mixed-spelling regression (#3046) -----------------------------------
# On native Windows, `git rev-parse --show-toplevel` (the real source of
# ir_worktree_root's input) yields Windows-drive form (C:/Users/x), while
# `mktemp -d` above and ir_enclosing_engine_root's `cd ... && pwd` walk both
# yield the POSIX-drive form (/c/Users/x) — the exact spelling split #3046
# hit. Reproduce it by asking git for ITS OWN spelling of the same real $ENG
# directory: on MSYS2/Git-Bash that comes back Windows-drive form (a
# genuinely different string from $ENG), on Linux/macOS it normally comes
# back byte-identical to $ENG, so this degrades to a harmless repeat of T2
# there rather than a skip — the assertion is meaningful wherever the split
# actually exists and inert everywhere else.
echo "T4: ir_creation_worktree_engine_root under a mixed path spelling (#3046)"
git -C "$ENG" init -q
GIT_SPELLED_ENG="$(git -C "$ENG" rev-parse --show-toplevel)"
CREATION_WT_GITSPELL="$GIT_SPELLED_ENG/creations/game/.claude/worktrees/agent-1"
assert_eq "$(ir_creation_worktree_engine_root "$CREATION_WT_GITSPELL")" "$GIT_SPELLED_ENG" \
    "creation worktree detected under git's own toplevel spelling, whether or not it matches \$ENG's"
assert_eq "$(ir_default_build_dir "$CREATION_WT_GITSPELL")" "$GIT_SPELLED_ENG/build-game-agent-1" \
    "build dir derived in the same spelling as the input, not \$ENG's mktemp spelling"

# --- T5: the no-ancestor walk terminates on any root spelling (#3046) ---------
# ir_creation_worktree_engine_root's ancestor walk used to stop only at "."
# or "/". On MSYS2/Git-Bash `dirname` is idempotent at a bare Windows drive
# root ("C:" -> "C:", "C:/" -> "C:/"), so neither sentinel ever fired and the
# walk spun forever — hanging ir-build/ir-run on every invocation from a repo
# with no engine-root ancestor, the OUTSIDE case T2/T3 document as supported.
# The fix terminates on a `dirname` fixed point, which holds for every root
# spelling. Surfaced in review of PR #3050.
#
# T5a runs the host's real `dirname`: on native Windows it IS the hang
# reproduction, while on Linux/macOS `dirname C:` yields "." so the walk
# terminated even pre-fix — inert there, same honest degradation as T4. T5b
# supplies the host-independent lock by shadowing `dirname` with MSYS2's
# drive-root idempotence, so the pre-fix walk runs away on EVERY host.
echo "T5: no-ancestor ancestor walk terminates on a drive-root spelling (#3046)"

# Bound T5a so a runaway walk reports a FAIL instead of hanging a direct
# invocation of this suite (run_all.sh already caps each suite; same probe
# order it uses). Without a runner the call is made in-process, unbounded.
TIMEOUT_CMD=""
if command -v timeout >/dev/null 2>&1; then
    TIMEOUT_CMD="timeout"
elif command -v gtimeout >/dev/null 2>&1; then
    TIMEOUT_CMD="gtimeout"
fi

rc=0
if [[ -n "$TIMEOUT_CMD" ]]; then
    "$TIMEOUT_CMD" 10 bash -c '
        set -euo pipefail
        source "$1"
        ir_creation_worktree_engine_root "C:/some/unrelated/path" >/dev/null
    ' _ "$HELPERS" || rc=$?
else
    ir_creation_worktree_engine_root "C:/some/unrelated/path" >/dev/null || rc=$?
fi
assert_rc "$rc" 1 "drive-form path with no engine ancestor returns 1 promptly (host dirname)"

# T5b — same walk against an MSYS2-idempotent `dirname`. The helper calls
# `dirname` inside a command substitution, so a shell function shadows the
# binary there; the call counter lives in a FILE because each substitution
# runs in a subshell whose variable writes are discarded.
DIRNAME_CALLS="$FAKE/dirname-calls"
echo 0 > "$DIRNAME_CALLS"
DIRNAME_CAP=64
dirname() {
    local n
    n=$(( $(cat "$DIRNAME_CALLS") + 1 ))
    printf '%s\n' "$n" > "$DIRNAME_CALLS"
    if (( n > DIRNAME_CAP )); then
        # Runaway: hand back "/" so the caller unwinds and this test can
        # report the call count instead of spinning forever.
        printf '/\n'
        return 0
    fi
    case "$1" in
        [A-Za-z]:|[A-Za-z]:/) printf '%s\n' "$1" ;;   # the MSYS2 fixed point
        *) command dirname "$1" ;;
    esac
}
rc=0; ir_creation_worktree_engine_root "C:/some/unrelated/path" >/dev/null || rc=$?
walk_calls="$(cat "$DIRNAME_CALLS")"
unset -f dirname
assert_rc "$rc" 1 "drive-root walk returns 1 under an MSYS2-idempotent dirname"
terminated=no; (( walk_calls <= DIRNAME_CAP )) && terminated=yes
assert_eq "$terminated" "yes" \
    "walk terminated without hitting the $DIRNAME_CAP-call cap (used $walk_calls calls)"

# --- Summary ------------------------------------------------------------------
echo
echo "pass: $PASS  fail: $FAIL"
(( FAIL == 0 ))
