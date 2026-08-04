#!/usr/bin/env bash
# Tests for fleet-rules-sweep — the #2739 false-clean guard.
#
# Hermetic: builds a throwaway git repo carrying the same ignore-then-negate
# .gitignore shape as the engine (`creations/*` + `!creations/demos/`), plants a
# known violation under it, and asserts the sweep finds it.
#
# The characterization case asserts that a *raw* `rg` rooted at `creations`
# under-walks that same tree — the false clean this tool exists to replace. It
# measures the environment rather than this tool, so an absent rg SKIPs it
# loudly instead of passing: a silently-skipped control is how #2739 survived
# a "clean" detector run in the first place. Note that on a host where rg is a
# Claude Code shell-snapshot function rather than a PATH binary, a
# non-interactive test shell will not see it — that is a real skip, not a
# missing dependency.

set -uo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$SCRIPT_DIR/lib_assert.sh"

SWEEP="$SCRIPT_DIR/../fleet-rules-sweep"

SKIP=0
skip() { SKIP=$((SKIP + 1)); echo "  SKIP: $1"; }

TMPROOT="$(mktemp -d "${TMPDIR:-/tmp}/fleet-rules-sweep-test.XXXXXX")"
cleanup() { rm -rf "$TMPROOT"; }
trap cleanup EXIT

REPO="$TMPROOT/repo"

# ----------------------------------------------------------------------
# Fixture: the engine's ignore-then-negate shape, with a planted violation
# ----------------------------------------------------------------------
build_fixture() {
    mkdir -p "$REPO/creations/demos/planted" \
             "$REPO/creations/editors/voxel_editor" \
             "$REPO/creations/private_tool" \
             "$REPO/engine/render"

    cat > "$REPO/.gitignore" <<'EOF'
build/
creations/*
!creations/CLAUDE.md
!creations/demos/
!creations/demos/**
!creations/editors/
!creations/editors/voxel_editor/
!creations/editors/voxel_editor/**
EOF

    # The planted match: the exact shape a creations-scoped detector hunts.
    cat > "$REPO/creations/demos/planted/main.cpp" <<'EOF'
void tick() {
    static int g_frameCounter = 0;
    g_frameCounter++;
}
EOF
    cat > "$REPO/creations/editors/voxel_editor/main.cpp" <<'EOF'
void draw() {
    static int g_editorCounter = 0;
}
EOF
    echo "// clean" > "$REPO/creations/demos/planted/clean.hpp"
    echo "// creations top-level" > "$REPO/creations/CLAUDE.md"
    # Genuinely ignored — must never be swept from the engine repo.
    echo "static int g_privateCounter = 0;" > "$REPO/creations/private_tool/secret.cpp"
    echo "// engine side" > "$REPO/engine/render/renderer.cpp"

    git -C "$REPO" init --quiet
    git -C "$REPO" config user.email "test@example.com"
    git -C "$REPO" config user.name "test"
    git -C "$REPO" add -A
    git -C "$REPO" commit --quiet -m "fixture"
}

build_fixture

PATTERN='static\s+int\s+g_'

# ----------------------------------------------------------------------
# 1. Characterization: raw rg rooted at creations reads a false clean
# ----------------------------------------------------------------------
echo "1. raw rg at the negated-dir root (the #2739 false clean)"
truth="$(git -C "$REPO" ls-files -- 'creations/*.cpp' 'creations/**/*.cpp' | grep -c . || true)"
assert_eq "$truth" "2" "fixture truth: git sees 2 .cpp files under creations/"

if command -v rg >/dev/null 2>&1; then
    rg_files="$(rg -c "$PATTERN" -g '*.cpp' "$REPO/creations" 2>/dev/null | grep -c . || true)"
    if [[ "$rg_files" -lt "$truth" ]]; then
        ok "rg rooted at creations under-walks ($rg_files of $truth) — #2739 reproduces here"
    else
        ok "rg rooted at creations walked $rg_files of $truth — this rg does not exhibit #2739"
    fi
else
    skip "no rg on PATH — walker characterization not measured this run"
fi

# ----------------------------------------------------------------------
# 2. The sweep finds what the walker missed
# ----------------------------------------------------------------------
echo "2. sweep over the same scope"
out="$("$SWEEP" --repo-root "$REPO" --pattern "$PATTERN" --glob '*.cpp' --files-only creations 2>/dev/null)"
rc=$?
assert_eq "$rc" "0" "exit 0 when matches are found"
assert_contains "$out" "creations/demos/planted/main.cpp" "finds the planted demo violation"
assert_contains "$out" "creations/editors/voxel_editor/main.cpp" "finds the negated-subtree violation"

# ----------------------------------------------------------------------
# 3. Ignored paths stay out (cross-repo isolation)
# ----------------------------------------------------------------------
echo "3. genuinely-ignored paths are not swept"
assert_absent "$out" "creations/private_tool/secret.cpp" "gitignored private tree excluded"

# ----------------------------------------------------------------------
# 4. The coverage guard — the acceptance-criterion case
# ----------------------------------------------------------------------
echo "4. coverage guard"
guard_out="$("$SWEEP" --repo-root "$REPO" --pattern "$PATTERN" creations/nonexistent 2>&1)"
guard_rc=$?
assert_eq "$guard_rc" "2" "scope resolving to 0 files exits 2, not 1"
assert_contains "$guard_out" "refusing to report a clean pass" "guard says why"

glob_out="$("$SWEEP" --repo-root "$REPO" --pattern "$PATTERN" --glob '*.rs' creations 2>&1)"
glob_rc=$?
assert_eq "$glob_rc" "2" "a --glob that filters out every file also exits 2"

# ----------------------------------------------------------------------
# 5. A real clean pass is distinguishable from a false one
# ----------------------------------------------------------------------
echo "5. real clean pass"
clean_out="$("$SWEEP" --repo-root "$REPO" --pattern 'zzz_absent_token_zzz' --glob '*.cpp' creations 2>&1)"
clean_rc=$?
assert_eq "$clean_rc" "1" "no matches with real coverage exits 1"
assert_contains "$clean_out" "swept 2 file(s)" "clean pass reports its coverage"

# ----------------------------------------------------------------------
# 6. Glob semantics: braces, negation, path-vs-basename
# ----------------------------------------------------------------------
echo "6. glob semantics"
brace_out="$("$SWEEP" --repo-root "$REPO" --pattern '.' --glob '*.{cpp,hpp}' --files-only creations 2>/dev/null)"
assert_contains "$brace_out" "creations/demos/planted/clean.hpp" "brace alternation expands"

neg_out="$("$SWEEP" --repo-root "$REPO" --pattern "$PATTERN" --glob '*.cpp' --glob '!creations/editors/**' --files-only creations 2>/dev/null)"
assert_contains "$neg_out" "creations/demos/planted/main.cpp" "negation keeps the non-excluded hit"
assert_absent "$neg_out" "creations/editors/voxel_editor/main.cpp" "leading-! glob excludes its subtree"

path_out="$("$SWEEP" --repo-root "$REPO" --pattern '.' --glob 'creations/demos/**' --files-only creations 2>/dev/null)"
assert_contains "$path_out" "creations/demos/planted/main.cpp" "slash-bearing glob matches the repo-relative path"
assert_absent "$path_out" "creations/editors/voxel_editor/main.cpp" "slash-bearing glob excludes outside its prefix"

# ----------------------------------------------------------------------
# 7. Whole-repo default scope still covers the negated subtree
# ----------------------------------------------------------------------
echo "7. default scope"
all_out="$("$SWEEP" --repo-root "$REPO" --pattern "$PATTERN" --glob '*.cpp' --files-only 2>/dev/null)"
assert_contains "$all_out" "creations/demos/planted/main.cpp" "no-scope sweep reaches creations"

# ----------------------------------------------------------------------
# 8. JSON output carries the coverage counts
# ----------------------------------------------------------------------
echo "8. json output"
json_out="$("$SWEEP" --repo-root "$REPO" --pattern "$PATTERN" --glob '*.cpp' --json creations 2>/dev/null)"
assert_contains "$json_out" '"files_walked"' "json reports files_walked"
assert_contains "$json_out" '"files_matched": 2' "json reports the matched-file count"

# --json must not route around the coverage guard: a no-coverage run is the
# false clean, and exiting 1 on it would hand a caller "clean" as machine data.
"$SWEEP" --repo-root "$REPO" --pattern "$PATTERN" --glob '*.rs' --json creations >/dev/null 2>&1
assert_eq "$?" "2" "json path honors the zero-coverage guard"

[[ "$SKIP" -gt 0 ]] && echo "" && echo "($SKIP characterization check(s) skipped — see notes above)"

summarize "fleet-rules-sweep tests"
