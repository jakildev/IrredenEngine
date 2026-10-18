#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR=$(cd "$(dirname "$0")/.." && pwd)
source "$(dirname "$0")/lib_preflight.sh"
source "$(dirname "$0")/lib_assert.sh"
source "$(dirname "$0")/lib_hermetic.sh"

SUBJECT="$SCRIPT_DIR/fleet-pr-stamp"
STUB="$(dirname "$0")/lib_gh_stub.py"
if [[ ! -f "$SUBJECT" ]]; then
    echo "SKIP: subject not found at $SUBJECT" >&2
    exit 3
fi

TMPROOT=$(mktemp -d)
cleanup() { [[ -d "${TMPROOT:-}" ]] && rm -rf "$TMPROOT"; return 0; }
trap cleanup EXIT
hermetic_poison_gh_env "$TMPROOT"

BIN="$TMPROOT/bin"
mkdir -p "$BIN" "$TMPROOT/home" "$TMPROOT/state"
cp "$STUB" "$BIN/gh"
printf '@python3 "%%~dp0gh" %%*\r\n' > "$BIN/gh.bat"
chmod +x "$BIN/gh"
export PATH="$BIN:$PATH"
export HOME="$TMPROOT/home"
export FLEET_STATE_DIR="$TMPROOT/state"
export GH_STUB_STATE="$TMPROOT/stub-state.json"
export GH_STUB_LOG="$TMPROOT/stub.log"
export GH_STUB_MISSES="$TMPROOT/stub-misses"
SEED="$TMPROOT/seed.json"

python3 - "$SEED" <<'PY'
import json
import sys

labels = {
    name: {"description": "", "color": "ededed"}
    for name in (
        "fleet:author-claude", "fleet:author-codex",
        "fleet:authored-on-linux", "fleet:authored-on-macos",
        "fleet:authored-on-windows",
    )
}
def pull(sha, labels=()):
    return {
        "title": "Synthetic PR", "body": "", "state": "open", "labels": list(labels),
        "created_at": "2026-01-01T00:00:00Z", "updated_at": "2026-01-01T00:00:00Z",
        "comments": [], "head": "feature", "sha": sha, "base": "master",
        "draft": False, "merged_at": None, "mergeable": True,
    }


# 7: Claude-trailered head, unstamped. 8: trailer-less head, unstamped.
# 9: trailer-less head, stamped by its Codex author.
pulls = {"7": pull("1" * 40), "8": pull("2" * 40),
         "9": pull("3" * 40, ["fleet:author-codex"])}
commits = {
    "1" * 40: {"message": "fix: thing\n\nCo-Authored-By: Claude Fable 5.1 "
                          "<noreply@anthropic.com>\n"},
    "2" * 40: {"message": "fix: thing\n"},
    "3" * 40: {"message": "fix: thing\n"},
}
json.dump({"repo": "acme/widgets", "labels": labels, "pulls": pulls, "commits": commits,
           "issues": {}, "reviews": {}}, open(sys.argv[1], "w"))
PY
printf '{"engine":"acme/widgets","game":"acme/widgets"}\n' > "$FLEET_STATE_DIR/repos.json"

reset_case() {
    cp "$SEED" "$GH_STUB_STATE"
    : > "$GH_STUB_LOG"
    : > "$GH_STUB_MISSES"
    rm -rf "$FLEET_STATE_DIR/usage"
    unset GH_STUB_THROTTLE FLEET_ROLE FLEET_RUNTIME
}

run_stamp() {
    local name="$1" rc=0
    shift
    "$SUBJECT" "$@" > "$TMPROOT/$name.out" 2> "$TMPROOT/$name.err" || rc=$?
    echo "$rc" > "$TMPROOT/$name.rc"
}

labels_now() { jq -c '.pulls["7"].labels' "$GH_STUB_STATE"; }
labels_of() { jq -c --arg n "$1" '.pulls[$n].labels' "$GH_STUB_STATE"; }

reset_case
FLEET_PR_STAMP_KERNEL=Darwin run_stamp fresh 7 --repo engine
assert_eq "$(cat "$TMPROOT/fresh.rc")" "0" "fresh PR passes"
assert_eq "$(cat "$TMPROOT/fresh.out")" '["fleet:author-claude","fleet:authored-on-macos"]' \
    "fresh PR prints the report line"
assert_contains "$(labels_now)" 'fleet:author-claude' "fresh PR gets author label"
assert_contains "$(labels_now)" 'fleet:authored-on-macos' "fresh PR gets host label"

reset_case
jq '.pulls["7"].labels=["fleet:author-codex"]' "$GH_STUB_STATE" > "$TMPROOT/swap.json"
mv "$TMPROOT/swap.json" "$GH_STUB_STATE"
FLEET_PR_STAMP_KERNEL=Darwin run_stamp swap 7 --repo engine --runtime claude
assert_eq "$(cat "$TMPROOT/swap.rc")" "0" "opposite author label is swapped"
assert_absent "$(labels_now)" 'fleet:author-codex' "opposite author label is removed"

reset_case
FLEET_RUNTIME=codex FLEET_PR_STAMP_KERNEL=SunOS run_stamp env-runtime 7 --repo engine
assert_eq "$(cat "$TMPROOT/env-runtime.rc")" "0" "environment runtime passes"
assert_eq "$(cat "$TMPROOT/env-runtime.out")" '["fleet:author-codex"]' \
    "unrecognized host reports only author label"
assert_absent "$(labels_now)" 'fleet:authored-on-' "unrecognized host adds no host label"

reset_case
GH_STUB_THROTTLE=1 FLEET_PR_STAMP_KERNEL=Darwin run_stamp fallback 7 --repo game
assert_eq "$(cat "$TMPROOT/fallback.rc")" "0" "GraphQL refusal falls back to REST"
assert_contains "$(cat "$GH_STUB_LOG")" 'api -X POST repos/acme/widgets/issues/7/labels' \
    "REST fallback writes labels"
if [[ -f "$FLEET_STATE_DIR/usage/github-user-graphql.rejected.json" ]]; then
    ok "GraphQL refusal is latched"
else
    bad "GraphQL refusal is latched"
fi

reset_case
FLEET_PR_STAMP_KERNEL=Linux run_stamp game-alias 7 --repo game
assert_eq "$(cat "$TMPROOT/game-alias.rc")" "0" "game alias resolves through repos.json"
assert_contains "$(cat "$GH_STUB_LOG")" '--repo acme/widgets' "resolved slug reaches gh"

reset_case
run_stamp bad-alias 7 --repo nope
assert_eq "$(cat "$TMPROOT/bad-alias.rc")" "2" "unknown alias is a usage refusal"
assert_eq "$(wc -c < "$GH_STUB_LOG" | tr -d ' ')" "0" "unknown alias makes no gh call"

reset_case
FLEET_ROLE=opus-reviewer run_stamp reviewer 7 --repo engine
assert_eq "$(cat "$TMPROOT/reviewer.rc")" "2" "reviewer role is refused"
assert_eq "$(wc -c < "$GH_STUB_LOG" | tr -d ' ')" "0" "reviewer refusal makes no gh call"

reset_case
mv "$BIN/gh" "$BIN/gh-real"
cat > "$BIN/gh" <<'SH'
#!/usr/bin/env bash
if [[ " $* " == *" --remove-label "* ]]; then
    exit 0
fi
exec python3 "$(dirname "$0")/gh-real" "$@"
SH
chmod +x "$BIN/gh"
FLEET_PR_STAMP_KERNEL=Darwin run_stamp dropped-write 7 --repo engine
assert_eq "$(cat "$TMPROOT/dropped-write.rc")" "1" "read-back rejects a dropped author write"
assert_absent "$(cat "$TMPROOT/dropped-write.out")" 'fleet:author-claude' \
    "dropped author write is visible in the report"
mv "$BIN/gh-real" "$BIN/gh"
chmod +x "$BIN/gh"

reset_case
FLEET_PR_STAMP_KERNEL=Darwin run_stamp infer 7 --repo engine --infer
assert_eq "$(cat "$TMPROOT/infer.rc")" "0" "trailered head infers claude"
assert_eq "$(cat "$TMPROOT/infer.out")" '["fleet:author-claude"]' \
    "inferred stamp reports the author label alone"
assert_absent "$(labels_now)" 'fleet:authored-on-' "inference adds no host label"

reset_case
run_stamp infer-none 8 --repo engine --infer
assert_eq "$(cat "$TMPROOT/infer-none.rc")" "3" "trailer-less head is not inferable"
assert_contains "$(cat "$TMPROOT/infer-none.err")" 'no Claude co-author trailer' \
    "not-inferable names the missing trailer"
assert_eq "$(labels_of 8)" '[]' "not-inferable writes nothing"
assert_absent "$(cat "$GH_STUB_LOG")" '--add-label' "not-inferable makes no label write"

reset_case
run_stamp infer-stamped 9 --repo engine --infer
assert_eq "$(cat "$TMPROOT/infer-stamped.rc")" "0" "an author's own stamp is kept"
assert_eq "$(cat "$TMPROOT/infer-stamped.out")" '["fleet:author-codex"]' \
    "kept stamp is reported as is"
assert_absent "$(cat "$GH_STUB_LOG")" '--add-label' "kept stamp makes no label write"

reset_case
run_stamp infer-both 7 --repo engine --infer --runtime codex
assert_eq "$(cat "$TMPROOT/infer-both.rc")" "2" "--infer with --runtime is a usage refusal"

reset_case
run_stamp backfill --backfill --repo game
assert_eq "$(cat "$TMPROOT/backfill.rc")" "0" "backfill passes with a skip"
assert_eq "$(cat "$TMPROOT/backfill.out")" $'8: skipped\n7: stamped' \
    "backfill reports each unstamped PR once"
assert_contains "$(labels_of 7)" 'fleet:author-claude' "backfill stamps the trailered PR"
assert_eq "$(labels_of 8)" '[]' "backfill leaves the trailer-less PR alone"
assert_eq "$(labels_of 9)" '["fleet:author-codex"]' "backfill never touches a stamped PR"
assert_absent "$(cat "$GH_STUB_LOG")" 'pulls/9' "backfill reads no stamped PR's head"
assert_absent "$(labels_of 7)" 'fleet:authored-on-' "backfill adds no host label"

reset_case
run_stamp backfill-pr 7 --backfill --repo engine
assert_eq "$(cat "$TMPROOT/backfill-pr.rc")" "2" "--backfill with a PR is a usage refusal"

reset_case
FLEET_ROLE=sonnet-reviewer run_stamp backfill-reviewer --backfill --repo engine
assert_eq "$(cat "$TMPROOT/backfill-reviewer.rc")" "2" "reviewer role cannot backfill"
assert_eq "$(wc -c < "$GH_STUB_LOG" | tr -d ' ')" "0" "reviewer backfill refusal makes no gh call"

reset_case
mv "$BIN/gh" "$BIN/gh-real"
cat > "$BIN/gh" <<'SH'
#!/usr/bin/env bash
if [[ " $* " == *" --remove-label "* ]]; then
    exit 0
fi
exec python3 "$(dirname "$0")/gh-real" "$@"
SH
chmod +x "$BIN/gh"
run_stamp backfill-dropped --backfill --repo engine
assert_eq "$(cat "$TMPROOT/backfill-dropped.rc")" "1" "backfill read-back rejects a dropped write"
mv "$BIN/gh-real" "$BIN/gh"
chmod +x "$BIN/gh"

assert_eq "$(wc -c < "$GH_STUB_MISSES" | tr -d ' ')" "0" "stub modeled every call"
summarize
