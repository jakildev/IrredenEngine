#!/usr/bin/env bash
# Tests for fleet-run-targets' Windows /c/… vs C:/… path-scope match — issue #2036.
#
# On native Windows the engine root comes from `git rev-parse --show-toplevel`
# (drive spelling: C:/Users/x) while the scope is an MSYS2 shell's $PWD (POSIX
# drive spelling: /c/Users/x). resolve_scope compares them with a bash `case`
# string match, so the same directory in two spellings failed containment and
# fell back to a whole-tree scan that found no executables.
#
# canonicalize_path_spelling (fleet-common.sh) normalizes both spellings to one
# canonical form before the compare. These tests pin that helper against literal
# path strings — reproducing the exact Windows scenario deterministically on any
# host — plus a containment simulation mirroring resolve_scope's case block.

set -euo pipefail

SCRIPT_DIR=$(cd "$(dirname "$0")/.." && pwd)
# shellcheck source=../fleet-common.sh
source "$SCRIPT_DIR/fleet-common.sh"

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

# Mirror of resolve_scope's containment case block (fleet-run-targets), operating
# on already-canonicalized root/build/scope. Prints the derived scope-relative
# path, or the literal "OUTSIDE" sentinel when the scope escapes the tree.
scope_rel() {
    local root build scope
    root=$(canonicalize_path_spelling "$1")
    build=$(canonicalize_path_spelling "$2")
    scope=$(canonicalize_path_spelling "$3")
    case "$scope" in
        "$build"/*) printf '%s' "${scope#"$build"/}" ;;
        "$build")   printf '' ;;
        "$root"/*)  printf '%s' "${scope#"$root"/}" ;;
        "$root")    printf '' ;;
        *)          printf 'OUTSIDE' ;;
    esac
}

WT_WIN='C:/Users/evinj/src/IrredenEngine/.claude/worktrees/opus-architect'
WT_MSYS='/c/Users/evinj/src/IrredenEngine/.claude/worktrees/opus-architect'
CANON='/c/Users/evinj/src/IrredenEngine/.claude/worktrees/opus-architect'

# --- Test 1: the two Windows spellings collapse to one canonical form -------
echo "T1: MSYS2 /c/… and Windows C:/… of the same dir canonicalize equal"
assert_eq "$(canonicalize_path_spelling "$WT_WIN")"  "$CANON" "C:/…  -> /c/…"
assert_eq "$(canonicalize_path_spelling "$WT_MSYS")" "$CANON" "/c/… -> /c/… (unchanged)"
assert_eq "$(canonicalize_path_spelling "$WT_WIN")" \
          "$(canonicalize_path_spelling "$WT_MSYS")" "both spellings compare equal"

# --- Test 2: spelling variants ----------------------------------------------
echo "T2: backslashes, drive-letter case, and bare drive"
assert_eq "$(canonicalize_path_spelling 'c:\Users\x\wt')" "/c/Users/x/wt" "backslash + lowercase drive"
assert_eq "$(canonicalize_path_spelling 'C:\Users\x')"    "/c/Users/x"    "backslash + uppercase drive"
assert_eq "$(canonicalize_path_spelling 'C:')"            "/c"           "bare drive C: -> /c"
assert_eq "$(canonicalize_path_spelling '/C/Users/x')"    "/c/Users/x"   "uppercase POSIX drive -> lowercase"

# --- Test 3: POSIX paths pass through byte-unchanged (macOS/Linux no-op) -----
echo "T3: real POSIX paths are returned unchanged"
assert_eq "$(canonicalize_path_spelling '/Users/evinjkill/src/IrredenEngine')" \
          "/Users/evinjkill/src/IrredenEngine" "macOS home path unchanged"
assert_eq "$(canonicalize_path_spelling '/home/u/wt')" "/home/u/wt" "linux path unchanged"
assert_eq "$(canonicalize_path_spelling '/opt/homebrew/bin')" "/opt/homebrew/bin" "multi-char top dir unchanged"
assert_eq "$(canonicalize_path_spelling '/e/data')" "/e/data" "lowercase single-letter top dir unchanged (not a drive)"

# --- Test 4: containment survives mixed spellings (the #2036 regression) -----
echo "T4: resolve_scope containment matches across mixed root/scope spellings"
ROOT_WIN='C:/Users/evinj/src/IrredenEngine'
SCOPE_MSYS='/c/Users/evinj/src/IrredenEngine/creations/demos/shape_debug'
assert_eq "$(scope_rel "$ROOT_WIN" "$ROOT_WIN/build" "$SCOPE_MSYS")" \
          "creations/demos/shape_debug" "win root + msys scope -> relative subpath (was OUTSIDE)"
# reverse spelling (defensive symmetry): msys root + win scope
assert_eq "$(scope_rel '/c/Users/evinj/src/IrredenEngine' '/c/Users/evinj/src/IrredenEngine/build' \
                        'C:/Users/evinj/src/IrredenEngine/creations/demos/shape_debug')" \
          "creations/demos/shape_debug" "msys root + win scope -> relative subpath"
# scope inside the build tree wins over the root branch
assert_eq "$(scope_rel "$ROOT_WIN" "$ROOT_WIN/build" "$ROOT_WIN/build/creations")" \
          "creations" "scope under build tree -> build-relative subpath"
# a genuinely outside scope still reports OUTSIDE
assert_eq "$(scope_rel "$ROOT_WIN" "$ROOT_WIN/build" 'C:/somewhere/else')" \
          "OUTSIDE" "unrelated scope still detected as outside"
# POSIX-only hosts: root == scope collapses to empty rel (whole-tree scan)
assert_eq "$(scope_rel '/Users/e/IrredenEngine' '/Users/e/IrredenEngine/build' \
                        '/Users/e/IrredenEngine')" \
          "" "posix root == scope -> whole tree"

echo
echo "passed: $PASS  failed: $FAIL"
[[ "$FAIL" -eq 0 ]]
