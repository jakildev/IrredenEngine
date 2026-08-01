#!/usr/bin/env bash
# Tests for cmake/run_clang_format_changed.cmake line scoping (#2719).
#
# The script under test lives in cmake/, not scripts/fleet/ — but it is
# reached exclusively through `fleet-build --target format-changed`, and
# this directory holds the only shell-test harness in the repo, so its
# regression coverage lives here.
#
# The defect: the target was diff-scoped at FILE granularity, running
# `clang-format -i` over each changed file in full. ~20% of the engine's
# quality files carry pre-existing clang-format drift, so a one-line edit
# to any of them dragged the whole file's drift into the author's PR
# (measured on master d6a44197: a 1-line edit to trixel_font.hpp produced
# a 225-line diff). The fix scopes the rewrite to the changed lines via
# repeated `--lines=<start>:<end>`.
#
# Each case builds a throwaway git repo with a synthetic origin/master
# ref, so the merge-base path the real script takes is exercised rather
# than its fallback.

set -uo pipefail

SCRIPT_DIR=$(cd "$(dirname "$0")/../../.." && pwd)
CMAKE_SCRIPT="$SCRIPT_DIR/cmake/run_clang_format_changed.cmake"
STYLE_FILE="$SCRIPT_DIR/.clang-format"

if [[ ! -f "$CMAKE_SCRIPT" ]]; then
    echo "SKIP: script under test not found at $CMAKE_SCRIPT" >&2
    exit 0
fi
for tool in cmake clang-format git; do
    if ! command -v "$tool" >/dev/null 2>&1; then
        echo "SKIP: $tool not available" >&2
        exit 0
    fi
done

# shellcheck source=/dev/null
source "$(dirname "$0")/lib_assert.sh"

TMPROOT=""
cleanup() { [[ -n "$TMPROOT" && -d "$TMPROOT" ]] && rm -rf "$TMPROOT"; }
trap cleanup EXIT
TMPROOT=$(mktemp -d)

# A file carrying plenty of pre-existing clang-format drift, plus a
# comment line that is already format-clean and therefore safe to edit.
write_drift_file() {
    cat > "$1" <<'EOF'
#include <vector>
int  a( int x ){return x   +1;}
int  b( int x ){return x   +2;}
// MARKER
int  c( int x ){return x   +3;}
int  d( int x ){return x   +4;}
EOF
}

# Fresh repo with one committed drift-carrying file and an origin/master
# ref pointing at that commit. Echoes the repo path.
new_repo() {
    local repo="$TMPROOT/$1"
    mkdir -p "$repo"
    git -C "$repo" init -q .
    git -C "$repo" config user.email t@example.com
    git -C "$repo" config user.name test
    cp "$STYLE_FILE" "$repo/.clang-format"
    write_drift_file "$repo/drift.cpp"
    git -C "$repo" add -A
    git -C "$repo" commit -qm init
    git -C "$repo" update-ref refs/remotes/origin/master HEAD
    echo "$repo"
}

# Run the target the way ir_quality_tools.cmake does. Extra args are the
# repo-relative paths to advertise as QUALITY_FILES.
run_format_changed() {
    local repo="$1"; shift
    local list="$repo/quality_files.cmake"
    {
        echo "set(QUALITY_FILES"
        for rel in "$@"; do echo "    \"$repo/$rel\""; done
        echo ")"
    } > "$list"
    cmake -DCLANG_FORMAT_BIN="$(command -v clang-format)" \
          -DQUALITY_FILE_LIST="$list" \
          -DPROJECT_ROOT="$repo" \
          -P "$CMAKE_SCRIPT" 2>&1
}

changed_line_count() {
    git -C "$1" diff --numstat -- "$2" | awk '{print $1+$2}'
}

echo "T1: editing one line of a drift-carrying file formats only that line"
REPO=$(new_repo t1)
# Sanity: the fixture really does carry drift a whole-file run would rewrite.
DRIFT=$(clang-format --style=file "$REPO/drift.cpp" | diff - "$REPO/drift.cpp" | grep -cE '^[<>]')
if [[ "$DRIFT" -lt 4 ]]; then
    bad "fixture carries pre-existing drift (got $DRIFT rewritable lines)"
else
    ok "fixture carries pre-existing drift ($DRIFT rewritable lines)"
fi
sed -i.bak 's|// MARKER|// MARKER edited|' "$REPO/drift.cpp" && rm -f "$REPO/drift.cpp.bak"
OUT=$(run_format_changed "$REPO" drift.cpp)
assert_eq "$(changed_line_count "$REPO" drift.cpp)" "2" \
    "diff stays at the single edited line (1 added + 1 removed)"
assert_contains "$OUT" "scoped to changed lines" "reports line-scoped formatting"
DRIFT_AFTER=$(git -C "$REPO" diff -U0 -- drift.cpp | grep -cE '^[+-]int ' || true)
assert_eq "$DRIFT_AFTER" "0" "pre-existing drift lines are left untouched"

echo ""
echo "T2: a misformatted CHANGED line still gets fixed (scoping, not disabling)"
REPO=$(new_repo t2)
sed -i.bak 's|// MARKER|int  e( int x ){return x   +5;}|' "$REPO/drift.cpp" && rm -f "$REPO/drift.cpp.bak"
run_format_changed "$REPO" drift.cpp >/dev/null
BODY=$(cat "$REPO/drift.cpp")
# The style wraps the body, so the edited line becomes several lines —
# assert the misformatted spelling is gone and the formatted one present.
assert_absent "$BODY" "int  e( int x ){" "the edited line's bad spacing is gone"
assert_contains "$BODY" "int e(int x) {" "the edited line is reformatted"
# ...while its drifted neighbours, which this edit did not touch, survive.
assert_contains "$BODY" "int  a( int x ){return x   +1;}" \
    "neighbouring drift lines are still untouched"

echo ""
echo "T3: a brand-new untracked file is formatted in full"
REPO=$(new_repo t3)
write_drift_file "$REPO/fresh.cpp"
OUT=$(run_format_changed "$REPO" drift.cpp fresh.cpp)
REMAINING=$(clang-format --style=file "$REPO/fresh.cpp" | diff - "$REPO/fresh.cpp" | grep -cE '^[<>]' || true)
assert_eq "$REMAINING" "0" "untracked file is fully formatted (all of it is new)"
assert_eq "$(changed_line_count "$REPO" drift.cpp)" "" \
    "the untouched tracked file is not reformatted"

echo ""
echo "T4: a pure-deletion hunk produces no zero-length range and no failure"
REPO=$(new_repo t4)
sed -i.bak '/\/\/ MARKER/d' "$REPO/drift.cpp" && rm -f "$REPO/drift.cpp.bak"
OUT=$(run_format_changed "$REPO" drift.cpp)
RC=$?
assert_eq "$RC" "0" "deletion-only edit exits cleanly"
assert_absent "$OUT" "CMake Error" "no CMake error on a '+N,0' hunk"
DRIFT_AFTER=$(git -C "$REPO" diff -U0 -- drift.cpp | grep -cE '^\+int ' || true)
assert_eq "$DRIFT_AFTER" "0" "deletion-only edit pulls in no drift"

echo ""
echo "T5: falls back to whole-file formatting when no merge-base exists"
REPO="$TMPROOT/t5"
mkdir -p "$REPO"
git -C "$REPO" init -q .
git -C "$REPO" config user.email t@example.com
git -C "$REPO" config user.name test
cp "$STYLE_FILE" "$REPO/.clang-format"
write_drift_file "$REPO/drift.cpp"
git -C "$REPO" add -A
git -C "$REPO" commit -qm init
# No origin/master ref at all -> merge-base lookup fails.
sed -i.bak 's|// MARKER|// MARKER edited|' "$REPO/drift.cpp" && rm -f "$REPO/drift.cpp.bak"
OUT=$(run_format_changed "$REPO" drift.cpp)
assert_contains "$OUT" "falling back to whole-file formatting" \
    "announces the whole-file fallback instead of silently skipping"
REMAINING=$(clang-format --style=file "$REPO/drift.cpp" | diff - "$REPO/drift.cpp" | grep -cE '^[<>]' || true)
assert_eq "$REMAINING" "0" "fallback still formats the file (old behavior preserved)"

echo ""
echo "T6: a new file COMMITTED on the branch is formatted in full"
# T3's new file is untracked, so it takes the explicit whole-file branch.
# `git add` + commit makes it tracked, so it goes down the line-range path
# instead and its hunk header is `@@ -0,0 +1,N @@` — a zero-length
# PRE-image, the mirror of T4's zero-length post-image. The range must come
# out as 1:N (the whole file). Getting it wrong is not silent: a 0-start
# range makes clang-format reject the argument outright, which is what the
# error assertions below pin.
REPO=$(new_repo t6)
write_drift_file "$REPO/added.cpp"
git -C "$REPO" add added.cpp
git -C "$REPO" commit -qm "add a new source file on the branch"
OUT=$(run_format_changed "$REPO" drift.cpp added.cpp)
assert_absent "$OUT" "CMake Error" "no CMake error on a '-0,0' hunk"
assert_absent "$OUT" "start line should be at least 1" \
    "the range starts at line 1, not 0"
REMAINING=$(clang-format --style=file "$REPO/added.cpp" | diff - "$REPO/added.cpp" | grep -cE '^[<>]' || true)
assert_eq "$REMAINING" "0" "the whole added file is formatted (all of it is new)"
assert_eq "$(changed_line_count "$REPO" drift.cpp)" "" \
    "the untouched tracked file is not reformatted"

summarize "format-changed line scoping (#2719)"
