#!/usr/bin/env bash
# Tests for #2837: the two per-record cache readers must not serve a stale
# snapshot as if it were current.
#
# `fleet-pr comments` and `fleet-issue view` are both woken BY an event that
# postdates the scout's snapshot — the verdict review that stamps a feedback
# label, the `## Plan` comment that stamps fleet:plan-review — so a cache read
# there structurally drops the one item its consumer was dispatched to read.
# Both are now live-first, falling back to the cache only when `gh` fails, and
# saying so loudly when they do.
#
# The first assertion in each pair is the positive control: it fails against
# master by construction, because master never invokes `gh` when the cached
# record parses.
#
# Two review nits from PR #2998 are covered here too: the inline fetch must
# page past the REST default window (the `gh` shim only serves its second page
# when `--paginate` is passed), and the inline-degradation warning must not
# name a "cached snapshot (unknown)" when no cached record exists at all.
#
# Hermetic: HOME is a mktemp dir (both scripts compute
# `Path.home() / ".fleet" / "state"` at import, and neither honors a
# FLEET_STATE_DIR override), and `gh` is a PATH shim. No network, no live
# ~/.fleet.

set -uo pipefail

SCRIPT_DIR=$(cd "$(dirname "$0")/.." && pwd)
source "$(dirname "$0")/lib_assert.sh"

FLEET_PR="$SCRIPT_DIR/fleet-pr"
FLEET_ISSUE="$SCRIPT_DIR/fleet-issue"
for exe in "$FLEET_PR" "$FLEET_ISSUE"; do
    [[ -f "$exe" ]] || { echo "SKIP: subject under test missing at $exe" >&2; exit 3; }
done

TMPROOT=$(mktemp -d "${TMPDIR:-/tmp}/fleet-cache-freshness.XXXXXX")
trap '[[ -n "${TMPROOT:-}" ]] && rm -rf "$TMPROOT"' EXIT

FAKE_HOME="$TMPROOT/home"
SHIM_BIN="$TMPROOT/bin"
mkdir -p "$FAKE_HOME/.fleet/state/prs/engine" \
         "$FAKE_HOME/.fleet/state/issues/engine" \
         "$FAKE_HOME/.fleet/state/diffs/engine" \
         "$SHIM_BIN"

STAMP="2026-08-04T05:44:25Z"

# ----------------------------------------------------------------------
# Fixtures. The cached records carry ONLY the pre-verdict material, exactly
# as the scout would have written them one tick before the triggering event.
# ----------------------------------------------------------------------
cat > "$FAKE_HOME/.fleet/state/prs/engine/2683.json" <<EOF
{
  "_cached_at": "$STAMP",
  "number": 2683,
  "title": "cached title",
  "body": "cached body",
  "updatedAt": "$STAMP",
  "headRefOid": "deadbeef",
  "files": [],
  "inlineComments": [],
  "comments": [
    {"author": {"login": "jakildev"}, "createdAt": "2026-08-04T05:22:31Z",
     "body": "STALE_ONLY_COMMENT"}
  ],
  "reviews": [
    {"author": {"login": "jakildev"}, "state": "APPROVED",
     "submittedAt": "2026-08-04T05:30:00Z", "body": "STALE_APPROVE_REVIEW"}
  ]
}
EOF

cat > "$FAKE_HOME/.fleet/state/issues/engine/2565.json" <<EOF
{
  "_cached_at": "$STAMP",
  "number": 2565,
  "title": "cached issue title",
  "state": "OPEN",
  "updatedAt": "$STAMP",
  "labels": ["fleet:needs-plan", "fleet:planning-mac-pool-3"],
  "body": "cached issue body",
  "comments": [
    {"author": {"login": "jakildev"}, "createdAt": "2026-07-28T00:00:00Z",
     "body": "STALE_ONLY_ISSUE_COMMENT"}
  ]
}
EOF

# ----------------------------------------------------------------------
# gh shim. GH_MODE picks the behaviour:
#   live  — return material that postdates the snapshot
#   fail  — exit nonzero on everything (offline / rate-limited)
#   inline-fail — conversation succeeds, the inline endpoint does not
# ----------------------------------------------------------------------
cat > "$SHIM_BIN/gh" <<'SHIM'
#!/usr/bin/env bash
mode="${GH_MODE:-live}"
[[ "$mode" == "fail" ]] && { echo "gh: shim failure" >&2; exit 1; }

# `gh api repos/<slug>/pulls/<N>/comments` — inline review threads.
if [[ "${1:-}" == "api" ]]; then
    if [[ "$mode" == "inline-fail" ]]; then
        echo "gh: shim inline failure" >&2
        exit 1
    fi
    # Model the REST window rather than pre-baking the answer: the endpoint
    # serves one page by default, and only --paginate reaches the second.
    # Without the flag the caller comes back short — the truncation the flag
    # exists to prevent — so an unflagged call cannot pass the assertions.
    paginate=0
    for arg in "$@"; do
        [[ "$arg" == "--paginate" ]] && paginate=1
    done
    if (( paginate )); then
        cat <<'JSON'
[{"path": "engine/render/src/foo.cpp", "line": 42,
  "user": {"login": "jakildev"}, "body": "LIVE_INLINE_ITEM"},
 {"path": "engine/render/src/bar.cpp", "line": 7,
  "user": {"login": "jakildev"}, "body": "LIVE_INLINE_PAGE2_ITEM"}]
JSON
    else
        cat <<'JSON'
[{"path": "engine/render/src/foo.cpp", "line": 42,
  "user": {"login": "jakildev"}, "body": "LIVE_INLINE_ITEM"}]
JSON
    fi
    exit 0
fi

if [[ "${1:-}" == "pr" && "${2:-}" == "view" ]]; then
    cat <<'JSON'
{"number": 2683,
 "comments": [{"author": {"login": "jakildev"},
               "createdAt": "2026-08-04T05:46:28Z",
               "body": "LIVE_NEW_COMMENT"}],
 "reviews": [{"author": {"login": "jakildev"}, "state": "CHANGES_REQUESTED",
              "submittedAt": "2026-08-04T05:45:51Z",
              "body": "LIVE_TRIGGERING_REVIEW"}]}
JSON
    exit 0
fi

if [[ "${1:-}" == "issue" && "${2:-}" == "view" ]]; then
    cat <<'JSON'
{"number": 2565, "title": "live issue title", "state": "OPEN",
 "labels": [{"name": "fleet:plan-review"}, {"name": "human:approved"}],
 "body": "live issue body",
 "comments": [{"author": {"login": "jakildev"},
               "createdAt": "2026-08-04T17:27:20Z",
               "body": "LIVE_PLAN_COMMENT"}]}
JSON
    exit 0
fi

echo "gh: shim got an unexpected invocation: $*" >&2
exit 64
SHIM
chmod +x "$SHIM_BIN/gh"

# run <mode> <script> <args...> → stdout on fd1; stderr captured to $ERRFILE
ERRFILE="$TMPROOT/err"
run_reader() {
    local mode="$1"; shift
    HOME="$FAKE_HOME" PATH="$SHIM_BIN:$PATH" GH_MODE="$mode" \
        python3 "$@" 2>"$ERRFILE"
}

# ----------------------------------------------------------------------
echo "fleet-pr comments — live-first (positive control: fails on master)"
# ----------------------------------------------------------------------
out=$(run_reader live "$FLEET_PR" comments 2683)
rc=$?
assert_eq "$rc" "0" "comments exits 0 on the live path"
assert_contains "$out" "LIVE_TRIGGERING_REVIEW" \
    "the review that postdates the snapshot is present (master serves the cache and drops it)"
assert_contains "$out" "LIVE_NEW_COMMENT" \
    "the comment that postdates the snapshot is present"
assert_absent "$out" "STALE_APPROVE_REVIEW" \
    "the superseded cached approve is not served alongside the live verdict"
assert_contains "$out" "[engine/render/src/foo.cpp:42]" \
    "inline [path:line] items render on the live path (the fallback used to drop them)"
assert_contains "$out" "LIVE_INLINE_PAGE2_ITEM" \
    "the inline fetch pages past the REST default window (an unpaginated call truncates)"

echo "fleet-pr comments — gh failure degrades to the snapshot, loudly"
out=$(run_reader fail "$FLEET_PR" comments 2683)
rc=$?
err=$(cat "$ERRFILE")
assert_eq "$rc" "0" "comments still exits 0 when it degrades to the cache"
assert_contains "$out" "STALE_APPROVE_REVIEW" "cached data is served when gh fails"
assert_contains "$err" "serving cached snapshot from $STAMP" \
    "the degraded read names the snapshot time on stderr"

echo "fleet-pr comments — inline endpoint alone failing keeps the fresh review"
out=$(run_reader inline-fail "$FLEET_PR" comments 2683)
err=$(cat "$ERRFILE")
assert_contains "$out" "LIVE_TRIGGERING_REVIEW" \
    "a failed inline endpoint does not throw away the fresh conversation"
assert_contains "$err" "inline-comment endpoint failed" \
    "the inline degradation is announced rather than silent"
assert_contains "$err" "cached snapshot ($STAMP)" \
    "the inline degradation names the snapshot its items actually came from"

echo "fleet-pr comments — inline fails with no cached record ⇒ names no phantom snapshot"
out=$(run_reader inline-fail "$FLEET_PR" comments 9999)
rc=$?
err=$(cat "$ERRFILE")
assert_eq "$rc" "0" "comments exits 0 when only the inline endpoint fails"
assert_contains "$out" "LIVE_TRIGGERING_REVIEW" \
    "the fresh conversation still renders with no cached record to fall back on"
assert_contains "$err" "no cached record exists" \
    "the no-cache inline degradation says the cache is absent"
assert_absent "$err" "cached snapshot (unknown)" \
    "it does not report an unresolved snapshot timestamp for a cache that does not exist"

echo "fleet-pr comments — no live, no cache ⇒ nonzero"
out=$(run_reader fail "$FLEET_PR" comments 9999)
rc=$?
assert_eq "$rc" "1" "live failure with no cached record exits nonzero"

# ----------------------------------------------------------------------
echo "fleet-issue view — live-first (positive control: fails on master)"
# ----------------------------------------------------------------------
out=$(run_reader live "$FLEET_ISSUE" view 2565)
rc=$?
assert_eq "$rc" "0" "view exits 0 on the live path"
assert_contains "$out" "LIVE_PLAN_COMMENT" \
    "the ## Plan comment that postdates the snapshot is present (master drops it)"
assert_contains "$out" "fleet:plan-review" \
    "labels are current, not the snapshot's two-transitions-behind set"
assert_absent "$out" "fleet:planning-mac-pool-3" \
    "the stale planning claim is not shown as live"

echo "fleet-issue view — gh failure degrades to the snapshot, loudly"
out=$(run_reader fail "$FLEET_ISSUE" view 2565)
rc=$?
err=$(cat "$ERRFILE")
assert_eq "$rc" "0" "view still exits 0 when it degrades to the cache"
assert_contains "$out" "STALE_ONLY_ISSUE_COMMENT" "cached data is served when gh fails"
assert_contains "$err" "serving cached snapshot from $STAMP" \
    "the degraded read names the snapshot time on stderr"

echo "fleet-issue view — no live, no cache ⇒ nonzero"
run_reader fail "$FLEET_ISSUE" view 9999 >/dev/null
assert_eq "$?" "1" "live failure with no cached record exits nonzero"

# ----------------------------------------------------------------------
echo "parity — the two mirrored policies cannot drift silently"
# ----------------------------------------------------------------------
# The scripts share no code by install design, so the identical wording is the
# only thing holding the policy together. Compare the notes as emitted, not as
# source literals.
run_reader fail "$FLEET_PR" comments 2683 >/dev/null
pr_note=$(sed -n 's/^fleet-pr: \(serving cached snapshot.*\)$/\1/p' "$ERRFILE")
run_reader fail "$FLEET_ISSUE" view 2565 >/dev/null
issue_note=$(sed -n 's/^fleet-issue: \(serving cached snapshot.*\)$/\1/p' "$ERRFILE")
assert_eq "$pr_note" "$issue_note" \
    "fleet-pr and fleet-issue emit an identical cache-fallback note"
[[ -n "$pr_note" ]] && ok "the parity assertion compared a non-empty note" \
    || bad "the parity assertion compared two empty strings (vacuous)"

# ----------------------------------------------------------------------
echo "snapshot subcommands — still cache reads, no longer silent"
# ----------------------------------------------------------------------
out=$(run_reader live "$FLEET_PR" view 2683)
rc=$?
err=$(cat "$ERRFILE")
assert_eq "$rc" "0" "view exits 0 from the cache"
assert_contains "$out" "STALE_APPROVE_REVIEW" "view still serves the snapshot"
assert_contains "$err" "snapshot as of $STAMP" "view announces its snapshot time"
assert_contains "$err" "'comments' is the live subcommand" \
    "view points at the live subcommand"

printf 'diff body\n' > "$FAKE_HOME/.fleet/state/diffs/engine/2683-abc1234.diff"
out=$(run_reader live "$FLEET_PR" diff 2683)
err=$(cat "$ERRFILE")
assert_contains "$out" "diff body" "diff still serves the cached diff"
assert_contains "$err" "snapshot diff for head abc1234" \
    "diff names the head SHA it is serving"

summarize "fleet cache-reader freshness tests (#2837)"
