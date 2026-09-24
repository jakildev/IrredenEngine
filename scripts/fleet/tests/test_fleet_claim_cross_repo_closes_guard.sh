#!/usr/bin/env bash
# cmd_claim's duplicate-open-PR guard honors GitHub's cross-repo closing
# form. An engine issue remedied by a PR in the OTHER fleet repo whose body
# carries `Closes jakildev/IrredenEngine#N` is in-flight work for engine #N —
# a fresh engine worker cannot advance that PR, so the claim must refuse. The
# guard runs a second `gh pr list` against the other repo and refuses on the
# namespaced grammar (`body_closes_issue_in`); the same-repo arm is unchanged
# in shape but reads the same grammar, so a repo-qualified ref to the claim's
# own repo counts too.
#
# Controls pin the namespace: a bare `Closes #N` or a `claude/<N>-…` branch in
# the other repo is THAT repo's #N and must not refuse an engine claim; a
# qualified ref naming the other repo itself is likewise not ours. The mirror
# (a `--repo game` claim refused by an engine PR) proves the arm is symmetric.
#
# The `gh` stub serves a different `pr list` per `--repo` and dies on an
# invocation it does not model (scripts/fleet/CLAUDE.md §stubs); the death is
# counted from a file because several call sites swallow gh's exit code.
#
# Hermetic per scripts/fleet/CLAUDE.md: no live GitHub, no live ~/.fleet.

set -euo pipefail

export FLEET_SKIP_CLONE_FRESHNESS=1

SCRIPT_DIR=$(cd "$(dirname "$0")/.." && pwd)
source "$(dirname "$0")/lib_preflight.sh"
FLEET_CLAIM="$SCRIPT_DIR/fleet-claim"

if [[ ! -x "$FLEET_CLAIM" ]]; then
    echo "test setup: fleet-claim not found at $FLEET_CLAIM" >&2
    exit 1
fi

source "$(dirname "$0")/lib_assert.sh"

TMPROOT=""
cleanup() {
    [[ -n "$TMPROOT" && -d "$TMPROOT" ]] && rm -rf "$TMPROOT"
}
trap cleanup EXIT

TMPROOT=$(mktemp -d)
source "$(dirname "$0")/lib_hermetic.sh"
hermetic_poison_gh_env "$TMPROOT"
export FLEET_CLAIMS_DIR="$TMPROOT/claims"
export FLEET_RESERVATIONS_DIR="$TMPROOT/reservations"
export FLEET_STATE_DIR="$TMPROOT/state"
mkdir -p "$FLEET_CLAIMS_DIR" "$FLEET_RESERVATIONS_DIR" "$FLEET_STATE_DIR"

# Per-repo open-PR fixtures the stub serves; each case rewrites them.
export STUB_ENGINE_PRS="$TMPROOT/engine-prs.json"
export STUB_GAME_PRS="$TMPROOT/game-prs.json"
export STUB_UNMODELED="$TMPROOT/unmodeled"
export STUB_PR_LIST_LOG="$TMPROOT/pr-list-repos"

STUB_DIR="$TMPROOT/bin"
mkdir -p "$STUB_DIR"
cat >"$STUB_DIR/gh" <<'GHSTUB'
#!/usr/bin/env bash
# Models exactly the calls a claim makes: issue view (open, no labels, empty
# body), the label REST acquire, issue edit, and pr list --state open keyed on
# --repo. Anything else is an unmodeled invocation: recorded, then fail.
case "$1 $2" in
    "issue view")
        issue_num="$3"
        for a in "$@"; do
            if [[ "$a" == "--jq" ]]; then echo "OPEN"; exit 0; fi
        done
        printf '%s' "{\"number\":$issue_num,\"state\":\"OPEN\",\"labels\":[],\"body\":\"\",\"title\":\"stub\"}"
        exit 0
        ;;
    "api "*)
        label=""
        while [[ $# -gt 0 ]]; do
            if [[ "$1" == "-f" ]]; then
                shift
                case "${1:-}" in labels\[\]=*) label="${1#labels[]=}" ;; esac
            fi
            shift || true
        done
        # POST echoes the add; the paginated --slurp verification GET returns
        # one page holding the candidate the acquire exported.
        if [[ -n "$label" ]]; then printf '[{"name":"%s"}]\n' "$label"; else printf '[[{"name":"%s"}]]\n' "${FLEET_CLAIM_CANDIDATE:-}"; fi
        exit 0
        ;;
    "pr list")
        repo=""; state=""
        prev=""
        for a in "$@"; do
            [[ "$prev" == "--repo" ]] && repo="$a"
            [[ "$prev" == "--state" ]] && state="$a"
            prev="$a"
        done
        if [[ "$state" != "open" ]]; then
            echo "gh stub: pr list --state $state not modeled" >&2
            echo "pr list --state $state" >>"$STUB_UNMODELED"; exit 97
        fi
        echo "$repo" >>"$STUB_PR_LIST_LOG"
        case "$repo" in
            jakildev/IrredenEngine) cat "$STUB_ENGINE_PRS"; exit 0 ;;
            jakildev/irreden)       cat "$STUB_GAME_PRS"; exit 0 ;;
        esac
        echo "gh stub: pr list --repo '$repo' not modeled" >&2
        echo "pr list --repo $repo" >>"$STUB_UNMODELED"; exit 97
        ;;
    "issue edit")
        exit 0
        ;;
esac
echo "gh stub: unmodeled invocation: $*" >&2
echo "$*" >>"$STUB_UNMODELED"
exit 97
GHSTUB
chmod +x "$STUB_DIR/gh"
export PATH="$STUB_DIR:$PATH"

assert_exit() {
    local actual_exit="$1" expected_exit="$2" msg="$3"
    if [[ "$actual_exit" -eq "$expected_exit" ]]; then ok "$msg"; else
        bad "$msg"; echo "        expected exit: $expected_exit, actual: $actual_exit"; fi
}
assert_dir() {
    if [[ -d "$1" ]]; then ok "$2"; else bad "$2 (dir missing: $1)"; fi
}
assert_no_dir() {
    if [[ ! -d "$1" ]]; then ok "$2"; else bad "$2 (dir exists: $1)"; fi
}

reset_case() {
    # $1 = engine PR list JSON, $2 = game PR list JSON
    printf '%s' "$1" >"$STUB_ENGINE_PRS"
    printf '%s' "$2" >"$STUB_GAME_PRS"
    rm -rf "$FLEET_CLAIMS_DIR" "$FLEET_RESERVATIONS_DIR"
    mkdir -p "$FLEET_CLAIMS_DIR" "$FLEET_RESERVATIONS_DIR"
    : >"$STUB_UNMODELED"
    : >"$STUB_PR_LIST_LOG"
}

CROSS='[{"number":415,"headRefName":"claude/game-3255-classifier-parity","body":"Closes jakildev/IrredenEngine#3255"}]'

# --- T1: the fired incident — engine claim refused by the game PR -----------
echo "T1: engine claim on #3255 refused by a game PR carrying the cross-repo Closes"
reset_case '[]' "$CROSS"
err=$("$FLEET_CLAIM" claim 3255 worker-4 2>&1 1>/dev/null) && actual=0 || actual=$?
assert_exit "$actual" 1 "claim 3255 -> exit 1"
assert_contains "$err" "already has open PR jakildev/irreden#415" \
    "refusal names the other repo's PR"
assert_contains "$err" "Closes jakildev/IrredenEngine#3255" \
    "refusal names the cross-repo closing line"
assert_contains "$err" "refusing duplicate claim" "refusal carries the duplicate phrase"
assert_no_dir "$FLEET_CLAIMS_DIR/3255" "no claim dir created on refusal"
assert_contains "$(cat "$STUB_PR_LIST_LOG")" "jakildev/irreden" \
    "guard queried the other repo's open PRs"
assert_eq "$(wc -c <"$STUB_UNMODELED" | tr -d ' ')" "0" "no unmodeled gh invocation"

# --- T2: controls — the other repo's own refs never refuse an engine claim --
echo "T2: qualified ref to the game repo itself is the game's #3255, not ours"
reset_case '[]' '[{"number":416,"headRefName":"claude/hand-named","body":"Closes jakildev/irreden#3255"}]'
actual=0; "$FLEET_CLAIM" claim 3255 worker-4 >/dev/null 2>&1 || actual=$?
assert_exit "$actual" 0 "claim 3255 -> exit 0"
assert_dir "$FLEET_CLAIMS_DIR/3255" "claim dir created"
assert_eq "$(wc -c <"$STUB_UNMODELED" | tr -d ' ')" "0" "no unmodeled gh invocation"

echo "T3: bare Closes #3255 in the game repo is the game's #3255"
reset_case '[]' '[{"number":417,"headRefName":"claude/hand-named","body":"Closes #3255"}]'
actual=0; "$FLEET_CLAIM" claim 3255 worker-4 >/dev/null 2>&1 || actual=$?
assert_exit "$actual" 0 "claim 3255 -> exit 0"
assert_dir "$FLEET_CLAIMS_DIR/3255" "claim dir created"

echo "T4: a claude/3255-x branch in the game repo is the game's #3255"
reset_case '[]' '[{"number":418,"headRefName":"claude/3255-x","body":""}]'
actual=0; "$FLEET_CLAIM" claim 3255 worker-4 >/dev/null 2>&1 || actual=$?
assert_exit "$actual" 0 "claim 3255 -> exit 0"
assert_dir "$FLEET_CLAIMS_DIR/3255" "claim dir created"

echo "T5: a quoted cross-repo ref inside a code span is not a link"
reset_case '[]' '[{"number":419,"headRefName":"claude/hand-named","body":"Not `Closes jakildev/IrredenEngine#3255` here"}]'
actual=0; "$FLEET_CLAIM" claim 3255 worker-4 >/dev/null 2>&1 || actual=$?
assert_exit "$actual" 0 "claim 3255 -> exit 0"
assert_dir "$FLEET_CLAIMS_DIR/3255" "claim dir created"

# --- T6: same-repo arm reads the namespaced grammar too ----------------------
echo "T6: an engine PR with a qualified ref to the engine repo refuses an engine claim"
reset_case '[{"number":3600,"headRefName":"claude/hand-named","body":"Closes jakildev/IrredenEngine#3255"}]' '[]'
err=$("$FLEET_CLAIM" claim 3255 worker-4 2>&1 1>/dev/null) && actual=0 || actual=$?
assert_exit "$actual" 1 "claim 3255 -> exit 1"
assert_contains "$err" "already has open PR #3600" "same-repo arm names the PR"
assert_no_dir "$FLEET_CLAIMS_DIR/3255" "no claim dir created on refusal"

# --- T7: mirror — a game claim refused by an engine PR -----------------------
echo "T7: --repo game claim on #3255 refused by an engine PR carrying Closes jakildev/irreden#3255"
reset_case '[{"number":3601,"headRefName":"claude/hand-named","body":"Fixes jakildev/irreden#3255"}]' '[]'
err=$("$FLEET_CLAIM" --repo game claim 3255 worker-4 2>&1 1>/dev/null) && actual=0 || actual=$?
assert_exit "$actual" 1 "--repo game claim 3255 -> exit 1"
assert_contains "$err" "already has open PR jakildev/IrredenEngine#3601" \
    "refusal names the engine PR"
assert_no_dir "$FLEET_CLAIMS_DIR/game-3255" "no claim dir created on refusal"
assert_eq "$(wc -c <"$STUB_UNMODELED" | tr -d ' ')" "0" "no unmodeled gh invocation"

echo "T8: mirror control — an engine PR closing engine #3255 does not refuse a game claim"
reset_case '[{"number":3602,"headRefName":"claude/3255-engine-work","body":"Closes #3255"}]' '[]'
actual=0; "$FLEET_CLAIM" --repo game claim 3255 worker-4 >/dev/null 2>&1 || actual=$?
assert_exit "$actual" 0 "--repo game claim 3255 -> exit 0"
assert_dir "$FLEET_CLAIMS_DIR/game-3255" "claim dir created"

# --- T9: stub fidelity — an unmodeled invocation is fatal, not silent --------
echo "T9: the stub dies on an unmodeled invocation"
: >"$STUB_UNMODELED"
set +e
gh pr list --repo someone/other --state open >/dev/null 2>&1; rc=$?
set -e
assert_exit "$rc" 97 "unmodeled --repo dies with 97"
assert_contains "$(cat "$STUB_UNMODELED")" "someone/other" "and is recorded"

summarize "fleet-claim cross-repo Closes guard (#3520)"
