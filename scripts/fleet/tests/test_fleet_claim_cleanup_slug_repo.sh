#!/usr/bin/env bash
# Tests that `fleet-claim cleanup`'s local walk derives each claim's lookup
# repo from the SLUG (repo_from_slug), never from the caller's --repo flags
# (#2864).
#
# The incident: cmd_cleanup's per-claim loop defaulted a bare engine slug's
# repo to repos[0] -- whatever --repo the CALLER passed -- with no `else`
# clause pinning it to the engine repo (only the game-* branch was special-
# cased). The scout's game sweep
# (`fleet-claim --repo game cleanup --gh --repo jakildev/irreden`) then
# resolved every bare engine slug's issue against the GAME repo instead of
# the engine repo. A bare engine slug whose number happened to collide with
# a CLOSED game issue got its local lock destroyed by the game sweep -- with
# no label cross-check on re-acquire (a bare `mkdir`), that lock is
# immediately re-grantable to a second pane: the exact duplicate-work
# hazard the lock fabric exists to prevent.
#
# Covers:
#   - T1-T4: repo_from_slug unit behavior (bare -> engine, game-* -> game,
#     independent of REPO_NS in either direction)
#   - T5 (ARM1 analog, THE REGRESSION): game sweep -- a bare engine slug
#     survives when its engine issue is OPEN, and is still correctly reaped
#     when its engine issue is CLOSED (proves real per-slug resolution, not
#     just "never touch a bare slug during a foreign sweep")
#   - T6 (ARM2 analog, control): engine sweep -- unaffected by the bug,
#     included so a future regression that flips repo_from_slug's default
#     shows up here too
#   - both arms: a game-* slug is reaped by whichever sweep runs, regardless
#     of the caller's --repo
#   - T7: hermeticity -- no gh mutation escapes the stub

set -euo pipefail

SCRIPT_DIR=$(cd "$(dirname "$0")/.." && pwd)
FLEET_CLAIM="$SCRIPT_DIR/fleet-claim"

if [[ ! -x "$FLEET_CLAIM" ]]; then
    echo "test setup: fleet-claim not found at $FLEET_CLAIM" >&2
    exit 1
fi

source "$(dirname "$0")/lib_assert.sh"

TMPROOT=""
cleanup() { [[ -n "$TMPROOT" && -d "$TMPROOT" ]] && rm -rf "$TMPROOT"; }
trap cleanup EXIT
TMPROOT=$(mktemp -d)

# --- gh stub ----------------------------------------------------------------
# Only `gh issue view <N> --repo <repo> --json state --jq .state` is exercised
# by cmd_cleanup's local walk. Engine issue #500 is OPEN, #600 is CLOSED; game
# issue #500 is CLOSED (the deliberate collision), #600 is OPEN (irrelevant,
# stubbed so the matrix has no gaps). Anything else logs and fails closed --
# a stub miss must never fall through to the real gh (scripts/fleet/CLAUDE.md).
STUB_DIR="$TMPROOT/bin"; mkdir -p "$STUB_DIR"
GH_LOG="$TMPROOT/gh.log"; : > "$GH_LOG"
export GH_LOG
cat > "$STUB_DIR/gh" <<'GHSTUB'
#!/usr/bin/env bash
echo "gh $*" >> "$GH_LOG"
if [[ "$1" == "issue" && "$2" == "view" ]]; then
    num="$3"; shift 3
    repo=""
    while [[ $# -gt 0 ]]; do
        case "$1" in
            --repo) repo="$2"; shift 2 ;;
            *)      shift ;;
        esac
    done
    case "${repo}:${num}" in
        jakildev/IrredenEngine:500) echo OPEN ;;
        jakildev/IrredenEngine:600) echo CLOSED ;;
        jakildev/irreden:500)       echo CLOSED ;;
        jakildev/irreden:600)       echo OPEN ;;
        *) exit 1 ;;
    esac
    exit 0
fi
exit 1
GHSTUB
chmod +x "$STUB_DIR/gh"

# --- T1-T4: repo_from_slug unit behavior ------------------------------------
# Routed through the FLEET_CLAIM_LIB sourcing seam (fleet-claim's own
# convention, see test_fleet_claim_stack_namespace.sh) so each call is
# hermetic and isolated in its own subshell -- no dispatch runs, and no
# mutation to this test's own shell state leaks out.
call_repo_from_slug() {
    local repo_ns="$1" slug="$2"
    (
        export FLEET_CLAIM_LIB=1
        # shellcheck disable=SC1090
        source "$FLEET_CLAIM"
        REPO_NS="$repo_ns" repo_from_slug "$slug"
    )
}

echo "T1-T4: repo_from_slug maps slug -> repo, independent of REPO_NS"
assert_eq "$(call_repo_from_slug "" "500")" "jakildev/IrredenEngine" \
    "T1: bare slug -> engine repo (REPO_NS unset)"
assert_eq "$(call_repo_from_slug "game" "500")" "jakildev/IrredenEngine" \
    "T2: bare slug -> engine repo even under REPO_NS=game (the regression)"
assert_eq "$(call_repo_from_slug "" "game-500")" "jakildev/irreden" \
    "T3: game-<N> slug -> game repo (REPO_NS unset)"
assert_eq "$(call_repo_from_slug "game" "game-500")" "jakildev/irreden" \
    "T4: game-<N> slug -> game repo (REPO_NS=game)"

# --- integration: cmd_cleanup's local walk ----------------------------------
mk_claim() {  # <dir> <slug>
    local base="$1" slug="$2"
    mkdir -p "$base/$slug"
    echo "worker-1" > "$base/$slug/owner"
    echo "$slug"    > "$base/$slug/title"
    date +%s        > "$base/$slug/created"
}

seed() {  # <dir> -- fresh claims dir with the collision fixture
    local base="$1"
    rm -rf "$base"; mkdir -p "$base"
    mk_claim "$base" "500"       # bare engine, engine OPEN / game CLOSED
    mk_claim "$base" "600"       # bare engine, engine CLOSED / game OPEN
    mk_claim "$base" "game-500"  # namespaced, game CLOSED
}

fc() {  # <claims-dir> <fleet-claim args...>
    PATH="$STUB_DIR:$PATH" FLEET_CLAIMS_DIR="$1" bash "$FLEET_CLAIM" "${@:2}" 2>&1
}

claim_present() { [[ -d "$1/$2" ]]; }

echo
echo "T5 (ARM1 analog): game sweep -- mirrors the scout's second cleanup pass"
T5="$TMPROOT/t5-claims"
seed "$T5"
out=$(fc "$T5" --repo game cleanup --repo jakildev/irreden)
echo "$out" | sed 's/^/    /'
if claim_present "$T5" "500"; then
    ok "T5a: bare engine slug #500 (engine OPEN) survives the game sweep (pre-fix: destroyed)"
else
    bad "T5a: bare engine slug #500 (engine OPEN) survives the game sweep (pre-fix: destroyed)"
fi
if ! claim_present "$T5" "600"; then
    ok "T5b: bare engine slug #600 (engine CLOSED) is still reaped during the game sweep"
else
    bad "T5b: bare engine slug #600 (engine CLOSED) is still reaped during the game sweep"
fi
if ! claim_present "$T5" "game-500"; then
    ok "T5c: game-500 (game CLOSED) is reaped by the game sweep"
else
    bad "T5c: game-500 (game CLOSED) is reaped by the game sweep"
fi

echo
echo "T6 (ARM2 analog, control): engine sweep -- mirrors the scout's first cleanup pass"
T6="$TMPROOT/t6-claims"
seed "$T6"
out=$(fc "$T6" cleanup --repo jakildev/IrredenEngine)
echo "$out" | sed 's/^/    /'
if claim_present "$T6" "500"; then
    ok "T6a: bare engine slug #500 (engine OPEN) survives the engine sweep"
else
    bad "T6a: bare engine slug #500 (engine OPEN) survives the engine sweep"
fi
if ! claim_present "$T6" "600"; then
    ok "T6b: bare engine slug #600 (engine CLOSED) is reaped by the engine sweep"
else
    bad "T6b: bare engine slug #600 (engine CLOSED) is reaped by the engine sweep"
fi
if ! claim_present "$T6" "game-500"; then
    ok "T6c: game-500 (game CLOSED) is still reaped by the engine sweep (already-correct branch, unbroken by the fix)"
else
    bad "T6c: game-500 (game CLOSED) is still reaped by the engine sweep (already-correct branch, unbroken by the fix)"
fi

# --- T7: hermeticity ---------------------------------------------------------
echo
echo "T7: hermeticity -- only read-only 'gh issue view' reached the stub"
if grep -qvE '^gh issue view ' "$GH_LOG"; then
    bad "T7: only 'gh issue view' calls reached the stub"
    echo "        unexpected lines:"; grep -vE '^gh issue view ' "$GH_LOG" | sed 's/^/          | /'
else
    ok "T7: only 'gh issue view' calls reached the stub"
fi

summarize "fleet-claim cleanup slug-to-repo tests"
