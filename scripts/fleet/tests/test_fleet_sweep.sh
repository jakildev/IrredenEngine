#!/usr/bin/env bash
# Tests for scripts/fleet/fleet-sweep (#2739).
#
# The bug this pins: a tree-wide detector sweep whose search root is a
# directory that owns a `dir/*` + `!dir/child/` block in .gitignore walks
# almost nothing and reports a clean pass. Because a sweep's whole product
# is the NEGATIVE result, every failure mode is indistinguishable from
# success — so the regression check has to be a POSITIVE CONTROL: plant a
# known match inside the hazardous scope and fail if the sweep does not
# find it.
#
# Everything runs against a throwaway repo built in a temp dir that
# reproduces the engine's two offending .gitignore blocks. Nothing here
# touches the real repo, the network, or ~/.fleet.

set -uo pipefail

SCRIPT_DIR=$(cd "$(dirname "$0")/.." && pwd)
SWEEP="$SCRIPT_DIR/fleet-sweep"

if [[ ! -x "$SWEEP" ]]; then
    echo "SKIP: fleet-sweep not executable at $SWEEP" >&2
    exit 0
fi
if ! command -v git >/dev/null 2>&1; then
    echo "SKIP: git not available" >&2
    exit 0
fi

# shellcheck source=/dev/null
source "$(dirname "$0")/lib_assert.sh"

TMPROOT=""
cleanup() { [[ -n "$TMPROOT" && -d "$TMPROOT" ]] && rm -rf "$TMPROOT"; }
trap cleanup EXIT
TMPROOT=$(mktemp -d)

REPO="$TMPROOT/repo"
mkdir -p "$REPO"
git -c init.defaultBranch=master init -q "$REPO"
git -C "$REPO" config user.email t@t
git -C "$REPO" config user.name test

# --- the fixture: the exact .gitignore shape that breaks the walker ------
# `creations/*` prunes every child before `!creations/demos/` can re-include
# it, but only when the walk STARTS at `creations`. `.claude/*` is the same
# shape with the extra twist of being a hidden directory.
cat > "$REPO/.gitignore" <<'EOF'
creations/*
!creations/CLAUDE.md
!creations/demos/
!creations/demos/**
.claude/*
!.claude/rules/
!.claude/rules/**
!.claude/agents/
!.claude/agents/**
EOF

mkdir -p "$REPO/creations/demos/probe" "$REPO/.claude/rules" "$REPO/.claude/agents" "$REPO/engine"

# The planted match. If a sweep of `creations/*` ever returns zero for this
# token, the walker has regressed into the #2739 hole.
echo 'int probe() { return SWEEP_CANARY_TOKEN; }' > "$REPO/creations/demos/probe/main.cpp"
echo 'demo notes' > "$REPO/creations/CLAUDE.md"
echo 'rule text SWEEP_CANARY_TOKEN' > "$REPO/.claude/rules/cpp-probe.md"
# A tracked symlink — ripgrep's walker never follows these (vector 3).
ln -s ../rules/cpp-probe.md "$REPO/.claude/agents/role-probe.md"
echo 'int engine_side() { return 0; }' > "$REPO/engine/main.cpp"
# Genuinely ignored: must stay OUT of the denominator (cross-repo isolation
# — an engine sweep must not reach the private creations/game clone).
mkdir -p "$REPO/creations/game"
echo 'SWEEP_CANARY_TOKEN in private clone' > "$REPO/creations/game/secret.cpp"

git -C "$REPO" add -A >/dev/null 2>&1
git -C "$REPO" commit -qm "fixture" >/dev/null 2>&1

sweep() { ( cd "$REPO" && "$SWEEP" "$@" 2>&1 ); }
sweep_rc() { ( cd "$REPO" && "$SWEEP" "$@" >/dev/null 2>&1; echo $?; ); }

echo "fleet-sweep: fixture sanity"

tracked=$(git -C "$REPO" ls-files -- 'creations/*' | wc -l | tr -d ' ')
assert_eq "$tracked" "2" "git tracks both creations/ files (CLAUDE.md + probe main.cpp)"
assert_eq "$(git -C "$REPO" ls-files -- 'creations/game/*' | wc -l | tr -d ' ')" "0" \
    "the gitignored private clone is NOT tracked"

echo ""
echo "fleet-sweep: THE POSITIVE CONTROL (#2739)"

# This is the assertion that would have caught the original bug.
out=$(sweep -E 'SWEEP_CANARY_TOKEN' -- 'creations/*')
assert_contains "$out" "creations/demos/probe/main.cpp" \
    "POSITIVE CONTROL: a creations/-scoped sweep FINDS the planted match"
assert_eq "$(sweep_rc -E 'SWEEP_CANARY_TOKEN' -- 'creations/*')" "0" \
    "  ...and exits 0 (found), never a silent clean"
assert_absent "$out" "creations/game/secret.cpp" \
    "  ...and does NOT reach the gitignored private clone"

out=$(sweep -E 'SWEEP_CANARY_TOKEN' -- '.claude/*')
assert_contains "$out" ".claude/rules/cpp-probe.md" \
    "POSITIVE CONTROL: a .claude/-scoped sweep reaches the hidden dotdir"

# Vector 3: tracked symlinks are in the denominator even though a
# filesystem walker skips them. Assert on the symlink itself rather than a
# raw count — the count would drift with any fixture edit, the mode bit is
# what actually pins the vector (120000 = symlink).
assert_contains "$(git -C "$REPO" ls-files -- '.claude/*')" "agents/role-probe.md" \
    "the tracked symlink is part of the .claude denominator"
assert_contains "$(git -C "$REPO" ls-files -s -- '.claude/agents/role-probe.md')" "120000" \
    "  ...and it really is a symlink, the shape a walker skips"

echo ""
echo "fleet-sweep: a zero-file sweep is an ERROR, never a clean"

# The heart of the contract. Before #2739 these two were indistinguishable.
assert_eq "$(sweep_rc -E 'anything' -- 'creationz/*.cpp')" "2" \
    "typo'd pathspec exits 2 (did not run), not 1 (clean)"
out=$(sweep -E 'anything' -- 'creationz/*.cpp')
assert_contains "$out" "read ZERO files" "  ...and says so loudly"
assert_eq "$(sweep_rc -E 'anything' -- 'creations/game/*')" "2" \
    "a scope that is real on disk but untracked also exits 2"

echo ""
echo "fleet-sweep: a real clean is distinguishable from a broken sweep"

assert_eq "$(sweep_rc -E 'NO_SUCH_TOKEN_ANYWHERE' -- 'creations/*')" "1" \
    "genuine no-match over a non-empty denominator exits 1"
out=$(sweep -E 'NO_SUCH_TOKEN_ANYWHERE' -- 'creations/*')
assert_contains "$out" "PROVEN clean" "  ...and reports the file count that proves it"
assert_contains "$out" "swept 2 files" "  ...naming the denominator explicitly"

echo ""
echo "fleet-sweep: --min pins an expected scope size"

assert_eq "$(sweep_rc --min 99 -E 'x' -- 'creations/*')" "2" \
    "a scope smaller than --min exits 2 (silently narrowed scope)"
assert_eq "$(sweep_rc --min 2 -E 'SWEEP_CANARY_TOKEN' -- 'creations/*')" "0" \
    "a scope meeting --min proceeds normally"

echo ""
echo "fleet-sweep: argument validation"

assert_eq "$(sweep_rc)" "2" "no pattern exits 2"
assert_contains "$(sweep --min)" "requires a value" "--min with no value is rejected"
assert_contains "$(sweep --min= -E x)" "requires a value" "--min= (empty) is rejected identically"
assert_contains "$(sweep --min abc -E x)" "non-negative integer" "non-numeric --min is rejected"
assert_contains "$(sweep -E a b)" "put pathspecs after" "a bare second argument is rejected"
assert_contains "$(sweep --bogus x)" "unknown option" "unknown options are rejected"

echo ""
echo "fleet-sweep: pattern modes"

assert_eq "$(sweep_rc -F 'return SWEEP_CANARY_TOKEN;' -- 'creations/*')" "0" \
    "-F matches a fixed string"
assert_eq "$(sweep_rc -i -E 'sweep_canary_token' -- 'creations/*')" "0" \
    "-i is case-insensitive"

# -P is what the .claude/rules Detection patterns need (negative lookahead).
# On a git without PCRE the script must fail loudly rather than fall back.
p_rc=$(sweep_rc -P 'SWEEP_(?!NOPE)CANARY' -- 'creations/*')
if [[ "$p_rc" == "0" ]]; then
    ok "-P runs PCRE lookahead patterns"
elif [[ "$p_rc" == "2" ]]; then
    ok "-P without PCRE support fails loudly (exit 2), not as a false clean"
else
    bad "-P returned unexpected rc=$p_rc"
fi

echo ""
echo "fleet-sweep: the walker hazard itself (only if a real rg binary exists)"

# In a Claude Code pane `rg` is a shell function re-execing the claude
# binary, not a PATH binary — it is deliberately NOT inherited by this
# subprocess, so this arm runs only where ripgrep is genuinely installed.
if command -v rg >/dev/null 2>&1; then
    rg_walked=$( ( cd "$REPO" && rg --files creations 2>/dev/null | wc -l | tr -d ' ' ) )
    if [[ "$rg_walked" -lt "$tracked" ]]; then
        ok "reproduced the hazard: rg walked $rg_walked of $tracked tracked creations/ files"
    else
        ok "this ripgrep walks creations/ correctly ($rg_walked/$tracked) — fleet-sweep is still the contract"
    fi
else
    ok "no rg binary on PATH — skipping the walker-hazard arm (fleet-sweep needs none)"
fi

summarize "fleet-sweep tests"
