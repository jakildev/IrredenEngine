#!/usr/bin/env bash
# Each case builds a throwaway git repo mirroring a synchronize-event graph
# and asserts the classify script's emitted rebase_only/docs_only decision.
#
# Hermetic: no live GitHub, no origin remote, no ~/.fleet. The script's
# best-effort `git fetch origin` fails closed to the local objects built here.

set -euo pipefail

SCRIPT_DIR=$(cd "$(dirname "$0")/.." && pwd)
SCRIPT="$SCRIPT_DIR/classify-auto-rereview.sh"

if [[ ! -f "$SCRIPT" ]]; then
    echo "SKIP: script not found at $SCRIPT" >&2
    exit 3  # skip status — run_all.sh must not count this as a pass
fi
if ! command -v git >/dev/null 2>&1; then
    echo "SKIP: git not available" >&2
    exit 0
fi

PASS=0
FAIL=0
TMPROOT=""
cleanup() { [[ -n "$TMPROOT" && -d "$TMPROOT" ]] && rm -rf "$TMPROOT"; }
trap cleanup EXIT
TMPROOT=$(mktemp -d)

ok()   { echo "  ok: $1";   PASS=$((PASS+1)); }
fail() { echo "  FAIL: $1"; FAIL=$((FAIL+1)); }

# Deterministic commit identity + dates so SHAs don't depend on wall-clock.
export GIT_AUTHOR_NAME=test GIT_AUTHOR_EMAIL=t@t
export GIT_COMMITTER_NAME=test GIT_COMMITTER_EMAIL=t@t
export GIT_AUTHOR_DATE="2026-01-01T00:00:00 +0000"
export GIT_COMMITTER_DATE="2026-01-01T00:00:00 +0000"
# Never let rebase/commit open an editor.
export GIT_EDITOR=true GIT_SEQUENCE_EDITOR=true

new_repo() {  # $1 = name -> echoes path
    local d="$TMPROOT/$1"
    git -c init.defaultBranch=master init -q "$d"
    ( cd "$d" && echo base > base.txt && git add base.txt && git commit -qm M0 )
    echo "$d"
}

add() {  # $1 = repo  $2 = file  $3 = msg
    ( cd "$1" && echo "$2" > "$2" && git add "$2" && git commit -qm "$3" )
}

classify() {  # $1 = repo  BEFORE AFTER BASE_SHA — echoes the decision line
    ( cd "$1" && BEFORE="$2" AFTER="$3" BASE_SHA="$4" bash "$SCRIPT" 2>/dev/null )
}

expect() {  # $1 = label  $2 = got  $3 = want
    if [[ "$2" == "$3" ]]; then ok "$1 -> $2"; else fail "$1: got '$2' want '$3'"; fi
}

# --- T1: stacked child retargeted after parent SQUASH-merge ------------------
echo "T1: retarget-after-squash-merge (the bug) -> rebase_only=true"
R=$(new_repo t1)
git -C "$R" checkout -q -b parent
add "$R" parent1.txt p1
add "$R" parent2.txt p2
PARENT_TIP=$(git -C "$R" rev-parse HEAD)
git -C "$R" checkout -q -b child
add "$R" child1.txt c1
add "$R" child2.txt c2
BEFORE=$(git -C "$R" rev-parse HEAD)
git -C "$R" checkout -q master
git -C "$R" merge --squash -q parent >/dev/null
git -C "$R" commit -qm "squash parent"
BASE_SHA=$(git -C "$R" rev-parse HEAD)
git -C "$R" rebase -q --onto master "$PARENT_TIP" child >/dev/null
AFTER=$(git -C "$R" rev-parse child)
expect "T1" "$(classify "$R" "$BEFORE" "$AFTER" "$BASE_SHA")" "rebase_only=true"

# --- T2: in-place catch-up rebase onto advanced master -----------------------
echo "T2: in-place catch-up rebase, child diff unchanged -> rebase_only=true"
R=$(new_repo t2)
git -C "$R" checkout -q -b feature
add "$R" child1.txt c1
add "$R" child2.txt c2
BEFORE=$(git -C "$R" rev-parse HEAD)
git -C "$R" checkout -q master
add "$R" master_extra.txt m1
BASE_SHA=$(git -C "$R" rev-parse HEAD)
git -C "$R" checkout -q feature
git -C "$R" rebase -q master >/dev/null
AFTER=$(git -C "$R" rev-parse HEAD)
expect "T2" "$(classify "$R" "$BEFORE" "$AFTER" "$BASE_SHA")" "rebase_only=true"

# --- T3: real content change (amended tip) -----------------------------------
echo "T3: real content change -> rebase_only=false"
R=$(new_repo t3)
BASE_SHA=$(git -C "$R" rev-parse master)
git -C "$R" checkout -q -b feature
add "$R" child1.txt c1
add "$R" child2.txt c2
BEFORE=$(git -C "$R" rev-parse HEAD)
( cd "$R" && echo "edited content" > child2.txt && git add child2.txt \
    && git commit -q --amend -m c2 )
AFTER=$(git -C "$R" rev-parse HEAD)
expect "T3" "$(classify "$R" "$BEFORE" "$AFTER" "$BASE_SHA")" \
    "rebase_only=false"$'\n'"docs_only=false"

# --- T4: BEFORE unavailable (orphaned, unfetchable) --------------------------
echo "T4: before commit unavailable -> rebase_only=false (conservative)"
R=$(new_repo t4)
BASE_SHA=$(git -C "$R" rev-parse master)
git -C "$R" checkout -q -b feature
add "$R" child1.txt c1
AFTER=$(git -C "$R" rev-parse HEAD)
BEFORE=0000000000000000000000000000000000000000
expect "T4" "$(classify "$R" "$BEFORE" "$AFTER" "$BASE_SHA")" \
    "rebase_only=false"$'\n'"docs_only=false"

# --- T5: plain force-push, identical tree, new SHA ---------------------------
echo "T5: no-op force-push (same tree, new SHA) -> rebase_only=true"
R=$(new_repo t5)
BASE_SHA=$(git -C "$R" rev-parse master)
git -C "$R" checkout -q -b feature
add "$R" child1.txt c1
add "$R" child2.txt c2
BEFORE=$(git -C "$R" rev-parse HEAD)
# Re-committing the identical tree with a later committer date yields a new
# SHA over the same tree, mirroring a bare `git push --force` with no edits.
GIT_COMMITTER_DATE="2026-02-02T00:00:00 +0000" \
    git -C "$R" commit -q --amend --no-edit
AFTER=$(git -C "$R" rev-parse HEAD)
[[ "$AFTER" != "$BEFORE" ]] || fail "T5 setup: AFTER SHA should differ from BEFORE"
expect "T5" "$(classify "$R" "$BEFORE" "$AFTER" "$BASE_SHA")" "rebase_only=true"

add_at() {  # $1 = repo  $2 = (possibly nested) path  $3 = content  $4 = msg
    ( cd "$1" && mkdir -p "$(dirname "$2")" && echo "$3" > "$2" \
        && git add "$2" && git commit -qm "$4" )
}

# --- T6: docs-only amend (non-canon .md) -> docs_only=true -------------------
echo "T6: net delta touches only a non-canon .md -> docs_only=true"
R=$(new_repo t6)
BASE_SHA=$(git -C "$R" rev-parse master)
git -C "$R" checkout -q -b feature
add_at "$R" "docs/agents/notes.md" "v1" doc
add "$R" child1.txt c1
BEFORE=$(git -C "$R" rev-parse HEAD)
add_at "$R" "docs/agents/notes.md" "v2 wording tweak" doc-amend
AFTER=$(git -C "$R" rev-parse HEAD)
expect "T6" "$(classify "$R" "$BEFORE" "$AFTER" "$BASE_SHA")" \
    "rebase_only=false"$'\n'"docs_only=true"

# --- T7: canon design doc (docs/design/**) -> docs_only=false ----------------
echo "T7: net delta touches a canon design doc -> docs_only=false"
R=$(new_repo t7)
BASE_SHA=$(git -C "$R" rev-parse master)
git -C "$R" checkout -q -b feature
add_at "$R" "docs/design/feature.md" "v1" design
BEFORE=$(git -C "$R" rev-parse HEAD)
add_at "$R" "docs/design/feature.md" "v2 changed intent" design-amend
AFTER=$(git -C "$R" rev-parse HEAD)
expect "T7" "$(classify "$R" "$BEFORE" "$AFTER" "$BASE_SHA")" \
    "rebase_only=false"$'\n'"docs_only=false"

# --- T8: mixed .md + code delta -> docs_only=false ---------------------------
echo "T8: net delta mixes docs and code -> docs_only=false"
R=$(new_repo t8)
BASE_SHA=$(git -C "$R" rev-parse master)
git -C "$R" checkout -q -b feature
add_at "$R" "docs/agents/notes.md" "v1" doc
add "$R" child1.txt c1
BEFORE=$(git -C "$R" rev-parse HEAD)
( cd "$R" && echo "v2" > docs/agents/notes.md && echo "edited" > child1.txt \
    && git add docs/agents/notes.md child1.txt && git commit -qm mixed-amend )
AFTER=$(git -C "$R" rev-parse HEAD)
expect "T8" "$(classify "$R" "$BEFORE" "$AFTER" "$BASE_SHA")" \
    "rebase_only=false"$'\n'"docs_only=false"

# The docs-only decision compares NET diffs per file, so master advancing a
# code file between the two anchors (rebase noise from a catch-up rebase) must
# not count as a net-changed code path; this exercises the BEFORE~N recovery
# arm of that decision (T6/T8 exercise the fast-forward arm).
echo "T9: docs-only amend atop a catch-up rebase -> docs_only=true"
R=$(new_repo t9)
git -C "$R" checkout -q -b feature
add_at "$R" ".fleet/status/notes-42.md" "notes v1" notes
BEFORE=$(git -C "$R" rev-parse HEAD)
git -C "$R" checkout -q master
add "$R" master_code.txt m1
BASE_SHA=$(git -C "$R" rev-parse master)
git -C "$R" checkout -q feature
git -C "$R" rebase -q master >/dev/null
( cd "$R" && echo "notes v2 amended" > .fleet/status/notes-42.md \
    && git add .fleet/status/notes-42.md && git commit -q --amend -m notes )
AFTER=$(git -C "$R" rev-parse HEAD)
expect "T9" "$(classify "$R" "$BEFORE" "$AFTER" "$BASE_SHA")" \
    "rebase_only=false"$'\n'"docs_only=true"

echo
echo "PASS=$PASS FAIL=$FAIL"
[[ "$FAIL" -eq 0 ]]
