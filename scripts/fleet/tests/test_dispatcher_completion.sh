#!/usr/bin/env bash
# Tests for the dispatcher's completion contract at pane exit
# (fleet_completion.py; docs/agents/FLEET.md § "How a launch ends"):
#
#   --assess-target        `<verdict>\t<updated_at>\t<detail>` for one finished
#                          dispatch, read off the target's issue object and
#                          comments since dispatch (+ open PRs for the kinds
#                          whose label rides a PR — fetched once per pass)
#   --handle-abandoned     the abandonment fold: first a logged retry, second a
#                          release + worktree salvage + handoff + sidecar clear
#   --complete-dispatches  one cleanup pass folding each verdict into the
#                          outcome bookkeeping — `finished` clears the target's
#                          ledgers, `declined` writes the decline memory the
#                          resolver reads, legacy records never touch gh
#
# gh, fleet-claim, tmux and pgrep are PATH stubs (hermetic, per
# scripts/fleet/CLAUDE.md); git runs for real against a scratch repo so the
# salvage is the actual `diff HEAD` + reset the dispatcher would perform.

set -euo pipefail

SCRIPT_DIR=$(cd "$(dirname "$0")/.." && pwd)
# shellcheck source=scripts/fleet/tests/lib_assert.sh
source "$(dirname "$0")/lib_assert.sh"
DISPATCHER="$SCRIPT_DIR/fleet-dispatcher"

TMPROOT=$(mktemp -d)
trap 'rm -rf "$TMPROOT"' EXIT
export HOME="$TMPROOT/home"
export FLEET_STATE_DIR="$TMPROOT/state"
export FLEET_CONF="$TMPROOT/fleet-up.conf"
export FLEET_ENGINE_ROOT="$TMPROOT/engine"
export FLEET_SESSIONS_DIR="$TMPROOT/sessions"
export FLEET_RESERVATIONS_DIR="$TMPROOT/reservations"
export FLEET_MODEL_FABLE='claude-fable-5[1m]'
export FLEET_MODEL_OPUS='claude-opus-4-8[1m]'
export FLEET_MODEL_SONNET='sonnet'
mkdir -p "$HOME" "$FLEET_STATE_DIR/dispatch" "$FLEET_STATE_DIR/projections" \
    "$FLEET_SESSIONS_DIR" "$FLEET_RESERVATIONS_DIR"
FIX="$TMPROOT/fixtures"; mkdir -p "$FIX"; export FIX
export GH_LOG="$TMPROOT/gh.log" CLAIM_LOG="$TMPROOT/claim.log"

STUB_BIN="$TMPROOT/bin"; mkdir -p "$STUB_BIN"
cat > "$STUB_BIN/gh" <<'EOF'
#!/usr/bin/env bash
printf '%s\n' "$*" >> "$GH_LOG"
[[ -n "${STUB_GH_DOWN:-}" ]] && exit 1
case "$*" in
    *"/comments -f since="*) cat "$FIX/comments.json" ;;
    "api repos/"*"/issues/"*) cat "$FIX/issue.json" ;;
    "pr list "*)              cat "$FIX/prs.json" ;;
esac
exit 0
EOF
cat > "$STUB_BIN/fleet-claim" <<'EOF'
#!/usr/bin/env bash
printf '%s\n' "$*" >> "$CLAIM_LOG"
[[ "${1:-}" == host ]] && echo mac
exit 0
EOF
cat > "$STUB_BIN/tmux" <<'EOF'
#!/usr/bin/env bash
sub="$1"; shift
case "$sub" in
    has-session)     exit 0 ;;
    list-panes)      printf '%%1|worker|zsh\n%%2|worker|zsh\n'; exit 0 ;;
    display-message) echo 1; exit 0 ;;
esac
exit 0
EOF
printf '#!/usr/bin/env bash\nexit 1\n' > "$STUB_BIN/pgrep"
chmod +x "$STUB_BIN"/*
export PATH="$STUB_BIN:$PATH"

issue()    { printf '{"labels":[%s],"updated_at":"2026-09-06T18:30:00Z"}\n' "$1" > "$FIX/issue.json"; }
comments() { printf '%s\n' "$1" > "$FIX/comments.json"; }
prs()      { printf '%s\n' "$1" > "$FIX/prs.json"; }
issue ''; comments '[]'; prs '[]'
DISPATCHED=$(python3 -c 'import calendar,time;print(calendar.timegm(time.strptime("2026-09-06T17:00:00Z","%Y-%m-%dT%H:%M:%SZ")))')
DECLINE_BODY='declined: worker/opus @mac-pool-3 needs a mac host'
CLAIM='{"name":"fleet:claim-mac-pool-3"}'
assess() {  # prints the verdict; the full line lands in $TMPROOT/line
    : > "$GH_LOG"
    "$DISPATCHER" --assess-target "$@" 2>/dev/null | tr -d "\r" > "$TMPROOT/line" || true
    cut -f1 "$TMPROOT/line"
}

echo "T1: verdicts off the target's own state"
assert_eq "$(assess task:engine:42 pool-3 "$DISPATCHED")" "finished" "claim label released -> finished"
issue "$CLAIM,{\"name\":\"fleet:queued\"}"
assert_eq "$(assess task:engine:42 pool-3 "$DISPATCHED")" "abandoned" "label standing, no PR, no record -> abandoned"
prs '[{"number":50,"headRefName":"claude/42-fix","body":""}]'
assert_eq "$(assess task:engine:42 pool-3 "$DISPATCHED")" "finished" "an open PR on the task's branch -> finished (the label rides it)"
prs '[]'
comments "[{\"created_at\":\"2026-09-06T17:26:19Z\",\"body\":\"$DECLINE_BODY\"}]"
assert_eq "$(assess task:engine:42 pool-3 "$DISPATCHED")" "declined" "a declined: comment from this pane since dispatch -> declined"
assert_eq "$(cat "$TMPROOT/line")" "declined	2026-09-06T18:30:00Z	declined this iteration: needs a mac host" \
    "the line carries the issue's updated_at and the reason"
issue "$CLAIM,{\"name\":\"fleet:claim-mac-pool-4\"}"
assert_eq "$(assess task:engine:42 pool-4 "$DISPATCHED")" "abandoned" "another pane's decline is not ours"
issue "$CLAIM"
comments '[]'

echo "T2: the comments fetch is scoped to the dispatch, the label to the lane"
assess task:engine:42 pool-3 "$DISPATCHED" >/dev/null
assert_contains "$(cat "$GH_LOG")" "issues/42/comments -f since=2026-09-06T17:00:00Z -f per_page=100" \
    "comments fetched since the dispatch epoch, one page"
issue '{"name":"fleet:amending-mac-pool-3"}'
assert_eq "$(assess feedback:engine:50 pool-3 "$DISPATCHED")" "abandoned" "amending label standing -> abandoned"
assert_absent "$(cat "$GH_LOG")" "pr list" "no PR list fetched for a feedback target"
issue "$CLAIM"
assert_eq "$(assess feedback:engine:50 pool-3 "$DISPATCHED")" "finished" "a task label is not the feedback lane's label -> finished"
issue '{"name":"fleet:reviewing-mac-pool-3"}'
assert_eq "$(assess review:game:12 pool-3 "$DISPATCHED")" "abandoned" "review label standing -> abandoned"
assert_contains "$(cat "$GH_LOG")" "repos/jakildev/irreden/issues/12" "game target read off the game repo"
issue '{"name":"fleet:planning-mac-pool-3"}'
assert_eq "$(assess plan:engine:99 pool-3 "$DISPATCHED")" "abandoned" "planning label standing -> abandoned"
issue ''

echo "T3: unknown when gh cannot answer or the target is malformed"
assert_eq "$(assess task:engine:42 pool-3 "$DISPATCHED" 2>/dev/null)" "finished" "sanity: gh up"
assert_eq "$(STUB_GH_DOWN=1 assess task:engine:42 pool-3 "$DISPATCHED")" "unknown" "gh down -> unknown"
assert_eq "$(assess bogus:engine:42 pool-3 "$DISPATCHED")" "unknown" "unknown kind -> unknown"
assert_eq "$(cat "$GH_LOG")" "" "a malformed target never reaches gh"

# --- Abandonment fold ---------------------------------------------------------
git init -q "$FLEET_ENGINE_ROOT"
git -C "$FLEET_ENGINE_ROOT" -c user.name=t -c user.email=t@t commit -q --allow-empty -m init
printf 'base\n' > "$FLEET_ENGINE_ROOT/tracked.txt"
git -C "$FLEET_ENGINE_ROOT" add tracked.txt
git -C "$FLEET_ENGINE_ROOT" -c user.name=t -c user.email=t@t commit -q -m tracked
WT="$FLEET_ENGINE_ROOT/.claude/worktrees/pool-3"
git -C "$FLEET_ENGINE_ROOT" worktree add -q "$WT" -b claude/42-work >/dev/null 2>&1
printf 'edited\n' > "$WT/tracked.txt"
printf 'new work\n' > "$WT/untracked.txt"
printf '{"session":"abc"}\n' > "$FLEET_SESSIONS_DIR/pool-3.session.json"
handle() { : > "$CLAIM_LOG"; "$DISPATCHER" --handle-abandoned "$@" 2>&1 >/dev/null | tr -d '\r' || true; }

echo "T4: the first abandonment is a logged retry — nothing released, nothing touched"
assert_contains "$(handle task:engine:42 pool-3)" "retrying once" "retry logged"
assert_eq "$(cat "$FLEET_STATE_DIR/abandoned/task-engine-42")" "1" "abandonment counter = 1"
assert_eq "$(cat "$CLAIM_LOG")" "" "claim left standing"
[[ -f "$WT/untracked.txt" && -f "$FLEET_SESSIONS_DIR/pool-3.session.json" ]] \
    && ok "worktree and sidecar untouched" || bad "worktree/sidecar touched on the first abandonment"

echo "T5: the second abandonment releases, salvages the worktree, hands off, clears the sidecar"
assert_contains "$(handle task:engine:42 pool-3)" "abandoned twice" "second abandonment logged"
assert_eq "$(cat "$CLAIM_LOG")" "release 42" "task claim released"
patch=$(ls "$FLEET_STATE_DIR"/salvage/task-engine-42-engine-*.patch 2>/dev/null | head -n 1 || true)
[[ -n "$patch" ]] && grep -q '^+edited$' "$patch" && grep -q '^+new work$' "$patch" \
    && ok "salvage patch holds the tracked edit and the untracked file" \
    || bad "salvage patch missing or incomplete: ${patch:-none}"
assert_eq "$(git -C "$WT" status --porcelain | tr -d '\r')" "" "worktree reset clean after salvage"
handoff=$(cat "$FLEET_STATE_DIR/handoff/task-engine-42.md" 2>&1 || true)
assert_contains "$handoff" "# Handoff — task:engine:42" "handoff names the target"
assert_contains "$handoff" "salvage: $FLEET_STATE_DIR/salvage/task-engine-42-engine-" "handoff names the patch"
[[ ! -f "$FLEET_SESSIONS_DIR/pool-3.session.json" ]] \
    && ok "session sidecar cleared" || bad "sidecar left standing"
[[ ! -f "$FLEET_STATE_DIR/abandoned/task-engine-42" ]] \
    && ok "abandonment counter cleared" || bad "counter left after handoff"

echo "T6: a game feedback target releases through its own lane, namespaced"
handle feedback:game:7 pool-3 >/dev/null
handle feedback:game:7 pool-3 >/dev/null
assert_eq "$(cat "$CLAIM_LOG")" "--repo game amending-release 7 pool-3" "amending-release under --repo game"
assert_contains "$(cat "$FLEET_STATE_DIR/handoff/feedback-game-7.md")" "salvage: none" \
    "a clean worktree hands off with no patch"

# --- The cleanup pass -----------------------------------------------------------
record() {  # $1 = pane number  $2 = target ("" for a legacy record)
    local extra=""
    [[ -n "$2" ]] && extra=$(printf ',"target":"%s","agent":"pool-3"' "$2")
    printf '{"role":"worker","pane":"%%%s","class":"opus","dispatched_at":"x","dispatched_epoch":%s,"claim_marker":1%s}\n' \
        "$1" "$DISPATCHED" "$extra" > "$FLEET_STATE_DIR/dispatch/pane-$1.json"
}
complete() { : > "$GH_LOG"; "$DISPATCHER" --complete-dispatches 2>&1 >/dev/null | tr -d '\r' || true; }
COUNTS_DIR="$FLEET_STATE_DIR/target-dispatch-counts"

echo "T7: finished clears the target's ledgers and counts as productive"
mkdir -p "$COUNTS_DIR" "$FLEET_STATE_DIR/abandoned"
printf '3' > "$COUNTS_DIR/task-engine-42"; printf '1' > "$FLEET_STATE_DIR/abandoned/task-engine-42"
record 1 task:engine:42
out=$(complete)
assert_contains "$out" "outcome=yes, target=task:engine:42, verdict=finished" "logged outcome=yes verdict=finished"
[[ ! -f "$COUNTS_DIR/task-engine-42" && ! -f "$FLEET_STATE_DIR/abandoned/task-engine-42" ]] \
    && ok "dispatch counter and abandonment counter cleared" || bad "a ledger survived the finish"
[[ ! -f "$FLEET_STATE_DIR/dispatch/pane-1.json" ]] && ok "record consumed" || bad "record left"

echo "T8: declined is an empty exit that writes the decline memory; the dispatch counter stands"
printf '3' > "$COUNTS_DIR/task-engine-42"
comments "[{\"created_at\":\"2026-09-06T17:26:19Z\",\"body\":\"$DECLINE_BODY\"}]"
record 1 task:engine:42
out=$(complete)
comments '[]'
assert_contains "$out" "outcome=no, target=task:engine:42, verdict=declined" "logged outcome=no verdict=declined"
assert_eq "$(cat "$COUNTS_DIR/task-engine-42")" "3" "dispatch counter kept"
assert_eq "$(sed -n 1p "$FLEET_STATE_DIR/declined/task-engine-42")" "2026-09-06T18:30:00Z" \
    "decline memory line 1 = the issue's post-release updated_at"
assert_eq "$(sed -n 2p "$FLEET_STATE_DIR/declined/task-engine-42")" "declined this iteration: needs a mac host" \
    "decline memory line 2 = the detail"
declined_py=$(FLEET_STATE_DIR="$FLEET_STATE_DIR" python3 -c "
import sys; sys.path.insert(0, sys.argv[1])
import fleet_task_class as f
print(f._declined('task', {'repo': 'engine', 'issue': '#42', 'updatedAt': '2026-09-06T18:30:00Z'}))
" "$SCRIPT_DIR" 2>/dev/null | tr -d '\r' || true)
assert_eq "$declined_py" "True" "the resolver reads that memory and skips the item"

echo "T9: abandoned folds through the abandonment counter"
rm -f "$FLEET_STATE_DIR/abandoned/task-engine-42"
issue "$CLAIM"
record 1 task:engine:42
out=$(complete)
assert_contains "$out" "retrying once" "first abandonment logged as a retry"
assert_contains "$out" "verdict=abandoned" "verdict logged"
assert_eq "$(cat "$FLEET_STATE_DIR/abandoned/task-engine-42")" "1" "abandonment counter = 1"

echo "T10: two task panes exiting in one pass share one PR-list fetch"
record 1 task:engine:42
record 2 task:engine:43
out=$(complete)
assert_eq "$(grep -c '^pr list' "$GH_LOG")" "1" "one gh pr list for two task completions"
assert_eq "$(grep -c '^api repos/jakildev/IrredenEngine/issues/4[23]$' "$GH_LOG")" "2" "each target's own issue fetched"
issue ''

echo "T11: legacy records never consult gh"
record 1 ""
out=$(complete)
assert_eq "$(cat "$GH_LOG")" "" "no target -> no gh call"
assert_absent "$out" "verdict=" "no verdict logged"

summarize "fleet-dispatcher completion-contract tests"
