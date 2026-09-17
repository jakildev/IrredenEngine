#!/usr/bin/env bash
# Tests for the merger lane's target binding in fleet-dispatcher.
#
# A merger LLM launch is bound to one PR that tier-0 (fleet-rebase) named
# as a `merge:<repo>:<N>` line in the merger trigger; a trigger with no
# such line is tier-0's cue, never an untargeted LLM launch. Measured
# before this contract (2026-09-17): 39 merger iterations in a day, none
# targeted, none productive — every one launched off a bare re-arm and
# walked both repos to report nothing to do.
#
#   T1: a bare scout touch spawns tier-0 and consumes the trigger; no launch
#   T2: the legacy "llm" content is no target either — tier-0, no launch
#   T3: two target lines, cap 1 -> one launch carrying the first line as its
#       target; the second line survives in the trigger; the next tick (after
#       the first iteration ends) launches it and consumes the trigger
#   T4: a line whose PR is already in flight is not re-launched; with nothing
#       else claimable the trigger is consumed
#   T5: no fleet-rebase on PATH and no line -> stand down, nothing spawned
#   T6: under provider routing a line whose record left the merger slice is
#       dropped rather than blocking the lane; the next line launches
#   T7: the per-target dispatch cap parks a target that keeps being launched
#   T7b: the next tick after the park does not re-launch it
#
# tmux, pgrep, gh, fleet-claim and fleet-rebase are PATH stubs (hermetic, per
# scripts/fleet/CLAUDE.md); the fleet-rebase stub only records that it was
# spawned.

set -uo pipefail

SCRIPT_DIR=$(cd "$(dirname "$0")/.." && pwd)
source "$(dirname "$0")/lib_preflight.sh"
DISPATCHER="$SCRIPT_DIR/fleet-dispatcher"
[[ -x "$DISPATCHER" ]] || { echo "SKIP: fleet-dispatcher not found at $DISPATCHER" >&2; exit 3; }

# shellcheck source=/dev/null
source "$SCRIPT_DIR/tests/lib_assert.sh"

TMPROOT=""
cleanup() { [[ -n "$TMPROOT" && -d "$TMPROOT" ]] && rm -rf "$TMPROOT"; }
trap cleanup EXIT
TMPROOT=$(mktemp -d)

export HOME="$TMPROOT/home"
export FLEET_STATE_DIR="$TMPROOT/state"
export FLEET_CONF="$TMPROOT/fleet-up.conf"
export FLEET_RESERVATIONS_DIR="$TMPROOT/reservations"
export FLEET_SESSIONS_DIR="$TMPROOT/sessions"
export FLEET_CLAIMS_DIR="$TMPROOT/claims"
export FLEET_SESSION="fleet-test-$$"
export FLEET_DISPATCH_MIN_GAP_SECONDS=0
export FLEET_CONCURRENCY_MERGER=1
export FLEET_CAP_MODE=strict
export BOOT_FANOUT_WINDOW_SECONDS=0
export FLEET_TARGET_DISPATCH_CAP=2
mkdir -p "$HOME" "$FLEET_STATE_DIR/projections" "$FLEET_STATE_DIR/dispatch" \
         "$FLEET_STATE_DIR/triggers" "$FLEET_RESERVATIONS_DIR" "$FLEET_SESSIONS_DIR"
touch "$FLEET_CONF"

STUB_BIN="$TMPROOT/bin"; mkdir -p "$STUB_BIN"
export STUB_LOG="$TMPROOT/stub.log"
cat > "$STUB_BIN/tmux" <<'TMUXEOF'
#!/usr/bin/env bash
sub="$1"; shift
case "$sub" in
    has-session) exit 0 ;;
    list-panes) printf '%%1|pool|zsh\n%%2|pool|zsh\n'; exit 0 ;;
    display-message)
        pane=""; fmt=""
        while [[ $# -gt 0 ]]; do
            case "$1" in
                -t) pane="$2"; shift 2 ;;
                -p) fmt="$2"; shift 2 ;;
                *)  shift ;;
            esac
        done
        if [[ "$fmt" == *pane_current_path* ]]; then
            echo "/fake/worktrees/pool-${pane#%}"
        elif [[ "$fmt" == *pane_pid* ]]; then
            echo "1"
        fi
        exit 0
        ;;
    send-keys) exit 0 ;;
    *) exit 0 ;;
esac
TMUXEOF
printf '#!/usr/bin/env bash\nexit 1\n' > "$STUB_BIN/pgrep"
# The merger lane is claimless: any fleet-claim call from it is a defect.
cat > "$STUB_BIN/fleet-claim" <<'CLAIMEOF'
#!/usr/bin/env bash
[[ "${1:-}" == host ]] && { echo mac; exit 0; }
echo "unexpected fleet-claim call from the merger lane: $*" >&2
printf 'fleet-claim %s\n' "$*" >> "$STUB_LOG"
exit 99
CLAIMEOF
cat > "$STUB_BIN/gh" <<'GHEOF'
#!/usr/bin/env bash
printf 'gh %s\n' "$*" >> "$STUB_LOG"
exit 0
GHEOF
cat > "$STUB_BIN/fleet-rebase" <<'REBASEEOF'
#!/usr/bin/env bash
printf 'fleet-rebase %s\n' "$*" >> "$STUB_LOG"
exit 0
REBASEEOF
chmod +x "$STUB_BIN"/*
export PATH="$STUB_BIN:$PATH"
unset FLEET_RUNTIMES FLEET_CROSS_PROVIDER_REVIEW FLEET_WORKER_RUNTIME

TRIGGER="$FLEET_STATE_DIR/triggers/merger"
SLICE="$FLEET_STATE_DIR/projections/merger.json"
cat > "$SLICE" <<'JSON'
{"prs": [], "merger_candidates": [
  {"number": 77, "repo": "engine", "labels": ["fleet:approved", "fleet:author-claude"], "signal": "needs-resolve"},
  {"number": 78, "repo": "game", "labels": ["fleet:author-claude"], "signal": "llm"}
]}
JSON

# Absolute, so T5's narrowed PATH still reaches it.
TIMEOUT_BIN=""
if command -v timeout >/dev/null 2>&1; then TIMEOUT_BIN="$(command -v timeout)"
elif command -v gtimeout >/dev/null 2>&1; then TIMEOUT_BIN="$(command -v gtimeout)"; fi
PYTHON_DIR="$(dirname "$(command -v python3)")"
tick() {  # one dispatch_role merger tick; prints the log
    : > "$STUB_LOG"
    if [[ -n "$TIMEOUT_BIN" ]]; then
        "$TIMEOUT_BIN" 20 "$DISPATCHER" --dispatch-role merger 2>&1 >/dev/null
    else
        "$DISPATCHER" --dispatch-role merger 2>&1 >/dev/null
    fi
}
reset_state() {
    rm -f "$FLEET_STATE_DIR/dispatch"/*.json "$TRIGGER" "$STUB_LOG"
    rm -rf "$FLEET_STATE_DIR/target-dispatch-counts" "$FLEET_STATE_DIR/empty-streak" \
           "$FLEET_STATE_DIR/standdown"
    touch "$STUB_LOG"
}
# Wait for the backgrounded tier-0 spawn to land in the stub log.
spawned_tier0() {
    local i
    for i in 1 2 3 4 5 6 7 8 9 10; do
        grep -q '^fleet-rebase --auto --rearm-trigger$' "$STUB_LOG" 2>/dev/null && return 0
        sleep 0.2
    done
    return 1
}
records_targets() { grep -ho '"target":"[^"]*"' "$FLEET_STATE_DIR/dispatch"/*.json 2>/dev/null | sort | tr '\n' ' '; }

echo "T1: a bare scout touch spawns tier-0 and consumes the trigger"
reset_state
: > "$TRIGGER"
out=$(tick)
assert_contains "$out" "merger: spawned tier-0 fleet-rebase" "tier-0 spawn logged"
spawned_tier0 && ok "fleet-rebase --auto --rearm-trigger was spawned" || bad "fleet-rebase not spawned: $(cat "$STUB_LOG")"
assert_absent "$out" "dispatching merger" "no LLM launch off a bare touch"
[[ -e "$TRIGGER" ]] && bad "trigger consumed by the tier-0 spawn" || ok "trigger consumed by the tier-0 spawn"
assert_eq "$(records_targets)" "" "no dispatch record written"

echo "T2: the legacy 'llm' content is not a target — tier-0 again, no launch"
reset_state
printf 'llm\n' > "$TRIGGER"
out=$(tick)
assert_contains "$out" "merger: spawned tier-0 fleet-rebase" "tier-0 spawn logged"
assert_absent "$out" "dispatching merger" "never an untargeted LLM launch"
[[ -e "$TRIGGER" ]] && bad "legacy trigger consumed" || ok "legacy trigger consumed"

echo "T3: two target lines, cap 1 -> one targeted launch per iteration, lines popped in order"
reset_state
printf 'merge:engine:77\nmerge:game:78\n' > "$TRIGGER"
out=$(tick)
assert_contains "$out" "dispatching merger -> %1 [target=merge:engine:77]" "first launch carries the first line as its target"
assert_absent "$out" "spawned tier-0" "target lines do not spawn tier-0"
assert_eq "$(records_targets)" '"target":"merge:engine:77" ' "one dispatch record, bound to the target"
assert_eq "$(cat "$TRIGGER" 2>/dev/null)" "merge:game:78" "the launched line is popped; the other survives"
assert_contains "$out" "target line(s) remain — trigger kept" "retention logged"
assert_absent "$(cat "$STUB_LOG")" "fleet-claim" "the merger lane takes no fleet-claim lock"
out=$(tick)
assert_contains "$out" "merger at concurrency cap" "the cap holds the second line while the first iteration runs"
assert_eq "$(cat "$TRIGGER" 2>/dev/null)" "merge:game:78" "the held line is untouched"
rm -f "$FLEET_STATE_DIR/dispatch"/*.json   # the first iteration ended
out=$(tick)
assert_contains "$out" "dispatching merger -> %1 [target=merge:game:78]" "the next tick launches the remaining line"
[[ -e "$TRIGGER" ]] && bad "trigger consumed once every line launched" || ok "trigger consumed once every line launched"
for key in merge-engine-77 merge-game-78; do
    assert_eq "$(cat "$FLEET_STATE_DIR/target-dispatch-counts/$key" 2>/dev/null)" "1" "$key dispatch counted"
done

echo "T4: a line whose PR is in flight is skipped; nothing else claimable consumes the trigger"
reset_state
printf 'merge:engine:77\n' > "$TRIGGER"
printf '{"role":"merger","pane":"%%9","class":"","target":"merge:engine:77","agent":"pool-9","runtime":"claude","dispatched_epoch":%s}\n' "$(date +%s)" \
    > "$FLEET_STATE_DIR/dispatch/pane-9.json"
out=$(tick)
assert_contains "$out" "merger at concurrency cap" "an in-flight merger holds the cap"
export FLEET_CONCURRENCY_MERGER=2
out=$(tick)
assert_absent "$out" "dispatching merger" "the in-flight PR is not launched twice"
assert_contains "$out" "no candidate could be claimed" "the lane stands down"
[[ -e "$TRIGGER" ]] && bad "trigger consumed (tier-0 re-emits if the PR still needs judgment)" || ok "trigger consumed (tier-0 re-emits if the PR still needs judgment)"
export FLEET_CONCURRENCY_MERGER=1

echo "T5: no fleet-rebase on PATH and no line -> stand down"
reset_state
: > "$TRIGGER"
mv "$STUB_BIN/fleet-rebase" "$STUB_BIN/fleet-rebase.off"
out=$(PATH="$STUB_BIN:$PYTHON_DIR:/usr/bin:/bin" tick)   # no ~/bin: the installed fleet-rebase must not leak in
assert_contains "$out" "no fleet-rebase on PATH and no target line; standing down" "stand-down logged"
assert_absent "$out" "dispatching merger" "no launch without a classifier"
mv "$STUB_BIN/fleet-rebase.off" "$STUB_BIN/fleet-rebase"

echo "T6: under provider routing an unroutable line is dropped, not a standing block"
reset_state
printf 'merge:engine:99\nmerge:engine:77\n' > "$TRIGGER"
out=$(FLEET_RUNTIMES=claude tick)
assert_contains "$out" "merger: dropped merge:engine:99 — no longer routable" "the line whose record left the slice is dropped"
assert_contains "$out" "dispatching merger -> %1 [target=merge:engine:77]" "the next line launches"
assert_absent "$out" "claim budget" "no gate block on the lane"
[[ -e "$TRIGGER" ]] && bad "trigger consumed" || ok "trigger consumed"

echo "T7: the per-target dispatch cap parks a target that keeps being launched"
reset_state
mkdir -p "$FLEET_STATE_DIR/target-dispatch-counts"
echo 2 > "$FLEET_STATE_DIR/target-dispatch-counts/merge-engine-77"
printf 'merge:engine:77\n' > "$TRIGGER"
out=$(tick)
assert_contains "$out" "dispatch circuit breaker: parked merge:engine:77 after 2 dispatches" "parked at the cap"
assert_contains "$(cat "$STUB_LOG")" "gh api repos/jakildev/IrredenEngine/issues/77/labels --method POST -f labels[]=fleet:needs-human" "fleet:needs-human added"
assert_absent "$out" "dispatching merger" "no launch past the cap"

echo "T7b: the next tick after the park does not re-launch it"
[[ -e "$TRIGGER" ]] && bad "the whole trigger is consumed by the park, not just the parked line" \
    || ok "the whole trigger is consumed by the park, not just the parked line"
out=$(tick)
assert_absent "$out" "dispatching merger" "no re-launch on the tick right after the park"
assert_absent "$out" "dispatch circuit breaker" "nothing left to park a second time"

summarize "fleet-dispatcher merger target binding"
