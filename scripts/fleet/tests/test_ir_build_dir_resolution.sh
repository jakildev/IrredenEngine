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
#   - the ir-build wrapper itself routes a creation worktree to the enclosing
#     engine and passes -DIRREDEN_USER_PROJECTS (#3046 acceptance criterion 1),
#     with cmake stubbed so no configure or compile runs

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

# shellcheck source=lib_assert.sh
source "$(dirname "$0")/lib_assert.sh"

# Test-specific assert, built on the shared assert_eq. lib_assert.sh owns the
# PASS/FAIL counters, ok/bad, the assert_* family, and the summarize exit
# idiom (scripts/fleet/CLAUDE.md).
assert_rc() {
    local rc="$1" expected="$2" msg="$3"
    assert_eq "$rc" "$expected" "$msg"
}

# Bound the arms that drive a walk or a wrapper end-to-end (T5a, T6) so a
# runaway reports a FAIL instead of hanging a direct invocation of this suite.
# run_all.sh already caps each suite; this is the same probe order it uses, and
# without a runner the calls would otherwise be unbounded.
TIMEOUT_CMD=""
if command -v timeout >/dev/null 2>&1; then
    TIMEOUT_CMD="timeout"
elif command -v gtimeout >/dev/null 2>&1; then
    TIMEOUT_CMD="gtimeout"
fi

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

# --- T6: the ir-build WRAPPER takes the downstream-creation route (#3046) -----
# T1-T5 pin the helpers in isolation. #3046's first acceptance criterion is
# about the wrapper on top of them: `fleet-build --target <creation-target>`
# from a creation worktree must announce the downstream-creation route and
# configure the ENCLOSING engine with -DIRREDEN_USER_PROJECTS=<worktree>,
# instead of trying to read presets out of the presets-less worktree. Nothing
# below T5 exercised that composition, so a wrapper-side regression (a dropped
# -D flag, a swapped -S/-B) would pass the whole suite.
#
# Drive the real ir-build over the fake layout with `cmake` stubbed onto PATH:
# the assertion is on the configure command ir-build COMPOSES, so there is no
# compiler, no configure, and no network — it runs identically on every host.
# `git init` in the creation worktree makes `git rev-parse --show-toplevel`
# (ir_worktree_root's real input) resolve there, as it does in the live fleet
# layout where each creation worktree is its own checkout.
#
# Scope, stated so it is not over-read: this covers the wrapper's branch and
# argument composition, which are host-independent. It is NOT a second control
# for the Windows spelling split — that is T4's (input spelling preserved) and
# T5's (walk terminates at a drive root), both at helper level. Running the
# acceptance command live on a native-Windows host is #3046's own criterion and
# is not something any of these hermetic arms stands in for.
echo "T6: ir-build wrapper route for a downstream-creation worktree (#3046)"

IR_BUILD="$SCRIPT_DIR/engine/tools/bin/ir-build"
git -C "$CREATION_WT" init -q
# Derive the expectations from git's OWN spelling of the worktree root by
# string suffix-strip, never by re-running the helpers under test (that would
# make the check circular). Suffix-strip also survives macOS spelling the
# mktemp root /var/... vs /private/var/....
GIT_SPELLED_WT="$(git -C "$CREATION_WT" rev-parse --show-toplevel)"
EXPECT_ENG="${GIT_SPELLED_WT%/creations/game/.claude/worktrees/agent-1}"
EXPECT_BUILD="$EXPECT_ENG/build-game-agent-1"
assert_eq "$([[ "$EXPECT_ENG" != "$GIT_SPELLED_WT" ]] && echo stripped || echo intact)" \
    "stripped" "git's worktree spelling carries the creations/ suffix (setup check)"

STUB_BIN="$FAKE/stub-bin"
CMAKE_LOG="$FAKE/cmake-args.log"
mkdir -p "$STUB_BIN"
: > "$CMAKE_LOG"
cat > "$STUB_BIN/cmake" <<STUB
#!/usr/bin/env bash
printf '%s\n' "\$*" >> "$CMAKE_LOG"
exit 0
STUB
chmod +x "$STUB_BIN/cmake"

# ir-build ends by exec'ing ir-acquire, which takes cpu slots out of
# $IR_LOCK_ROOT — already redirected to a temp dir at the top of this file, so
# the budget it sees is empty and the acquire is immediate rather than queued
# behind the host's real builds. Bounded by the same timeout probe T5a uses so
# a regression reports a FAIL instead of hanging the suite.
IR_BUILD_LOG="$FAKE/ir-build.log"
rc=0
if [[ -n "$TIMEOUT_CMD" ]]; then
    ( cd "$CREATION_WT" && PATH="$STUB_BIN:$PATH" \
        "$TIMEOUT_CMD" 60 "$IR_BUILD" --target IRMidiMoire ) >"$IR_BUILD_LOG" 2>&1 || rc=$?
else
    ( cd "$CREATION_WT" && PATH="$STUB_BIN:$PATH" \
        "$IR_BUILD" --target IRMidiMoire ) >"$IR_BUILD_LOG" 2>&1 || rc=$?
fi
ir_build_out="$(cat "$IR_BUILD_LOG")"
# The configure invocation, isolated from the later `cmake --build` one. Every
# path below is compared as a whole TOKEN, not with assert_contains: these
# values nest ("<eng>" is a prefix of "<eng>/creations/..."), so a substring
# match would stay green on exactly the misroute #3046 was — the wrapper
# configuring the worktree instead of the engine.
configure_line="$(grep -F -- '--preset' "$CMAKE_LOG" || true)"
cfg_src="${configure_line#*-S }"; cfg_src="${cfg_src%% *}"
cfg_bin="${configure_line#*-B }"; cfg_bin="${cfg_bin%% *}"
cfg_user="${configure_line##*-DIRREDEN_USER_PROJECTS=}"
engine_src_line="$(grep -F -- 'engine source:' "$IR_BUILD_LOG" || true)"

assert_rc "$rc" 0 "ir-build runs to completion from a creation worktree"
assert_contains "$ir_build_out" "ir-build: downstream-creation worktree" \
    "wrapper announces the downstream-creation route"
assert_eq "${engine_src_line#*engine source: }" "$EXPECT_ENG" \
    "wrapper reports the enclosing engine as the configure source"
assert_absent "$ir_build_out" "Could not read presets" \
    "wrapper never tries to read presets out of the creation worktree (#3046 symptom)"
assert_eq "$cfg_user" "$GIT_SPELLED_WT" \
    "configure adds the worktree as a user project (#3046 acceptance 1)"
assert_eq "$cfg_src" "$EXPECT_ENG" \
    "configure sources the enclosing engine, not the worktree"
assert_eq "$cfg_bin" "$EXPECT_BUILD" \
    "configure targets <engine>/build-<creation>-<agent>"

summarize "ir-build build-dir resolution tests"
