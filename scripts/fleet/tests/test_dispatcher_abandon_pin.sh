#!/usr/bin/env bash
# Tests for the dispatcher's abandon pin: a first-abandoned target whose pane's
# session sidecar resumes that very dispatch keeps its claim, and the pane is
# held for exactly one re-dispatch of it.
#
#   --handle-abandoned  writes the pin (target, epoch, role) and re-arms the
#                       worker lane; the claim is left standing
#   --find-idle         a pinned pane serves the worker lane only, ordered first
#   --assign            the pin is served before any election, re-acquired as
#                       the incumbent claim, and consumed; a refused re-acquire
#                       releases everything but the label
#   --dispatch-role     a lane with nothing else to elect (the '' stand-down
#                       and the defer verdict) still launches the pinned pane;
#                       a pinned pane in its usage-limit cooldown is held
#   --complete-dispatches  an unserved pin past the claim TTL is released
#   Codex sidecar       the pin relaunches on the reserved Codex route, and
#                       is held while Codex is cooling down
#
# tmux, fleet-claim, pgrep and codex are PATH stubs (hermetic, per
# scripts/fleet/CLAUDE.md); the clock is pinned through FLEET_TASK_CLASS_NOW.

set -euo pipefail
unset FLEET_RUNTIMES FLEET_CROSS_PROVIDER_REVIEW FLEET_WORKER_RUNTIME \
    FLEET_CLAIM_SWEPT_COOLDOWN_SECS FLEET_DISPATCH_ID FLEET_ROLE_MODEL FLEET_CLAIM_STALE_SECS

SCRIPT_DIR=$(cd "$(dirname "$0")/.." && pwd)
source "$(dirname "$0")/lib_preflight.sh"
DISPATCHER="$SCRIPT_DIR/fleet-dispatcher"
[[ -x "$DISPATCHER" ]] || { echo "SKIP: fleet-dispatcher not found at $DISPATCHER" >&2; exit 3; }
source "$(dirname "$0")/lib_assert.sh"

TMPROOT=""; cleanup(){ [[ -n "$TMPROOT" && -d "$TMPROOT" ]] && rm -rf "$TMPROOT"; }
trap cleanup EXIT
TMPROOT=$(mktemp -d)
source "$(dirname "$0")/lib_hermetic.sh"
hermetic_poison_gh_env "$TMPROOT"

export HOME="$TMPROOT/home"
export FLEET_STATE_DIR="$TMPROOT/state"
export FLEET_CONF="$TMPROOT/fleet-up.conf"; touch "$FLEET_CONF"
export FLEET_ENGINE_ROOT="$TMPROOT/engine"
export FLEET_SESSIONS_DIR="$TMPROOT/sessions"
export FLEET_RESERVATIONS_DIR="$TMPROOT/reservations"
mkdir -p "$HOME" "$FLEET_STATE_DIR/projections" "$FLEET_STATE_DIR/dispatch" "$FLEET_STATE_DIR/triggers" \
    "$FLEET_SESSIONS_DIR" "$FLEET_RESERVATIONS_DIR" "$FLEET_ENGINE_ROOT"
export FLEET_MODEL_FABLE='claude-fable-5[1m]'
export FLEET_MODEL_OPUS='claude-opus-4-8[1m]'
export FLEET_MODEL_SONNET='sonnet'
export FLEET_DISPATCH_MIN_GAP_SECONDS=0
export FLEET_DISPATCHER_CLAIM_SETTLE_SECONDS=0
export FLEET_CONCURRENCY_WORKER=5
export FLEET_SESSION="fleet-test-$$"
export FLEET_TEST_HOST=mac
# 1800000000 = 2027-01-15T08:00:00Z.
export FLEET_TASK_CLASS_NOW=1800000000
FIVE_MIN_AGO="2027-01-15T07:55:00Z"

export FLEET_CLAIM_LOG="$TMPROOT/fleet-claim.log"
export SEND_LOG="$TMPROOT/send-keys.log"
STUB_BIN="$TMPROOT/bin"; mkdir -p "$STUB_BIN"
cat > "$STUB_BIN/fleet-claim" <<'EOF'
#!/usr/bin/env bash
printf '%s\n' "$*" >> "$FLEET_CLAIM_LOG"
case "${1:-}" in
    host) echo mac ;;
    amending-claim)
        if [[ -n "${STUB_REFUSE:-}" ]]; then
            echo "fleet-claim: refuse PR#$2 — persistent owner [fleet:claim-linux-pool-1]" >&2
            exit 1
        fi ;;
esac
exit 0
EOF
cat > "$STUB_BIN/tmux" <<'EOF'
#!/usr/bin/env bash
sub="$1"; shift
case "$sub" in
    has-session) exit 0 ;;
    list-panes) for i in 1 2; do printf '%%%s|pool|zsh\n' "$i"; done ;;
    display-message)
        pane=""; fmt=""
        while [[ $# -gt 0 ]]; do
            case "$1" in -t) pane="$2"; shift 2 ;; -p) fmt="$2"; shift 2 ;; *) shift ;; esac
        done
        if [[ "$fmt" == *pane_current_path* ]]; then echo "/fake/worktrees/pool-${pane#%}"
        elif [[ "$fmt" == *pane_pid* ]]; then echo 1; fi
        ;;
    send-keys) printf '%s\n' "$*" >> "$SEND_LOG" ;;
esac
exit 0
EOF
printf '#!/usr/bin/env bash\nexit 1\n' > "$STUB_BIN/pgrep"
chmod +x "$STUB_BIN/fleet-claim" "$STUB_BIN/tmux" "$STUB_BIN/pgrep"
export PATH="$STUB_BIN:$PATH"

TARGET="feedback:engine:3763"
PIN="$FLEET_STATE_DIR/abandon-pin/pool-2"
SIDECAR="$FLEET_SESSIONS_DIR/pool-2.session.json"
HANDOFF="$FLEET_STATE_DIR/handoff/feedback-engine-3763.md"
TRIGGER="$FLEET_STATE_DIR/triggers/worker"

write_slice() { printf '%s\n' "$1" > "$FLEET_STATE_DIR/projections/worker.json"; }
EMPTY_SLICE='{"tasks_open":[],"needs_plan":[],"semantic_conflict_prs":[],"feedback_prs":[]}'
OTHER_SLICE='{"tasks_open":[],"needs_plan":[],"semantic_conflict_prs":[],"feedback_prs":[{"number":50,"repo":"engine","updatedAt":"2027-01-15T07:59:00Z","labels":["fleet:needs-fix"]}]}'
COOLING_SLICE="{\"tasks_open\":[],\"needs_plan\":[],\"semantic_conflict_prs\":[],\"feedback_prs\":[{\"number\":50,\"repo\":\"engine\",\"updatedAt\":\"$FIVE_MIN_AGO\",\"labels\":[\"fleet:needs-fix\",\"fleet:sweep-cooldown\"]}]}"

# Fresh state: pool-2's sidecar is the abandoned dispatch's own session.
reset() {
    rm -rf "$FLEET_STATE_DIR/abandon-pin" "$FLEET_STATE_DIR/abandoned" "$FLEET_STATE_DIR/handoff" \
        "$FLEET_STATE_DIR/rate-limit" "$FLEET_STATE_DIR/target-dispatch-counts"
    rm -f "$FLEET_STATE_DIR/dispatch"/*.json "$TRIGGER" "$SIDECAR"
    printf '{"session_id":"SID-9","role":"worker","model":"%s","effort":"xhigh","runtime":"claude","target":"%s","class":"opus","dispatch_id":"D1","created_epoch":1}\n' \
        "$FLEET_MODEL_OPUS" "$TARGET" > "$SIDECAR"
    : > "$FLEET_CLAIM_LOG"; : > "$SEND_LOG"
}
abandon() { "$DISPATCHER" --handle-abandoned "$TARGET" pool-2 worker 2>&1 >/dev/null | tr -d '\r' || true; }
assign() { "$DISPATCHER" --assign worker pool-2 2>/dev/null | tr -d '\r' || true; }
tick() {  # <count> — consecutive ticks in ONE dispatcher process; prints the log
    "$DISPATCHER" --dispatch-role worker "$1" 2>&1 >/dev/null | tr -d '\r' || true
}
count() { grep -c -- "$2" <<< "$1" || true; }

echo "=== T1: a sidecar-backed first abandon pins the pane and keeps the claim ==="
reset
out=$(abandon)
assert_contains "$out" "retrying once — pane pinned to resume its session" "the keep names the pin"
assert_eq "$(cat "$FLEET_CLAIM_LOG")" "" "the claim is left standing"
assert_eq "$(sed -n 1p "$PIN" 2>/dev/null)" "$TARGET" "the pin names the abandoned target"
assert_eq "$(sed -n 3p "$PIN" 2>/dev/null)" "worker" "the pin records the abandoned role"
[[ -f "$TRIGGER" ]] && ok "the worker lane is re-armed" || bad "no worker trigger written"
[[ -f "$SIDECAR" ]] && ok "the sidecar is kept for the resume" || bad "the sidecar was cleared"

echo "=== T2: a pinned pane serves the worker lane only, ordered first ==="
assert_eq "$("$DISPATCHER" --find-idle sonnet-reviewer | tr -d '\r' | paste -sd ' ' -)" "%1" \
    "another lane never lands on the pinned pane"
assert_eq "$("$DISPATCHER" --find-idle worker | tr -d '\r' | paste -sd ' ' -)" "%2 %1" \
    "the worker lane gets the pinned pane first"

echo "=== T3: --assign serves the pin before the election, once ==="
write_slice "$OTHER_SLICE"
assert_eq "$(assign)" "target=$TARGET" "the pinned target is assigned though the projection offers PR 50 first"
assert_contains "$(cat "$FLEET_CLAIM_LOG")" "amending-claim 3763 pool-2" "re-acquired as the incumbent under the same agent"
assert_absent "$(cat "$FLEET_CLAIM_LOG")" "amending-claim 50" "no election walk for the pinned pane"
[[ ! -f "$PIN" ]] && ok "the pin is consumed by its dispatch" || bad "the pin outlived its dispatch"
: > "$FLEET_CLAIM_LOG"
assert_eq "$(assign)" "target=feedback:engine:50" "the second --assign elects as normal"

echo "=== T4: a lane with nothing to elect still launches the pinned pane ('' stand-down) ==="
reset
write_slice "$EMPTY_SLICE"
touch "$TRIGGER"
out=$(tick 1)
assert_contains "$out" "standing down" "control: with no pin the empty slice stands the lane down"
assert_eq "$(count "$out" 'dispatching ')" 0 "control: nothing launched"
abandon >/dev/null
out=$(tick 1)
assert_contains "$out" "dispatching worker -> %2 [target=$TARGET] runtime=claude" "the pinned pane is dispatched on its target"
assert_eq "$(count "$out" 'dispatching ')" 1 "no free pane launched beside it"
assert_contains "$(cat "$SEND_LOG")" "target=$TARGET" "the launch carries the pinned target"
assert_contains "$(cat "$FLEET_STATE_DIR/dispatch/pane-2.json" 2>/dev/null)" "\"target\":\"$TARGET\"" \
    "the dispatch record names the target"

echo "=== T5: the defer verdict does not hold a pin back, and keeps its trigger ==="
reset
write_slice "$COOLING_SLICE"
abandon >/dev/null
out=$(tick 1)
assert_contains "$out" "dispatching worker -> %2 [target=$TARGET] runtime=claude" "the pinned pane is dispatched through a deferral"
assert_eq "$(count "$out" 'dispatching ')" 1 "the deferred class launches nothing else"
[[ -f "$TRIGGER" ]] && ok "the deferral's trigger is kept" || bad "the trigger was consumed"

echo "=== T6: a pinned pane in its usage-limit cooldown is held, pin and trigger kept ==="
reset
write_slice "$EMPTY_SLICE"
abandon >/dev/null
mkdir -p "$FLEET_STATE_DIR/rate-limit"; date +%s > "$FLEET_STATE_DIR/rate-limit/pane-2.ts"
out=$(tick 1)
assert_eq "$(count "$out" 'dispatching ')" 0 "nothing launched during the cooldown"
assert_contains "$out" "a reserved pane is held" "the pinned pane is held like a reserved one"
[[ -f "$PIN" && -f "$TRIGGER" ]] && ok "pin and trigger kept for the resume" || bad "pin or trigger lost in the cooldown"
assert_absent "$(cat "$FLEET_CLAIM_LOG")" "amending-claim" "no re-acquire while held"

echo "=== T7: a refused re-acquire releases the session but not the label ==="
reset
abandon >/dev/null
write_slice "$EMPTY_SLICE"
STUB_REFUSE=1 assign >/dev/null
assert_contains "$(cat "$FLEET_CLAIM_LOG")" "amending-claim 3763 pool-2" "the re-acquire was attempted"
assert_absent "$(cat "$FLEET_CLAIM_LOG")" "amending-release" "the label is left to its holder"
[[ ! -f "$PIN" && ! -f "$SIDECAR" ]] && ok "pin and sidecar cleared" || bad "pin or sidecar left after the refusal"
assert_contains "$(cat "$HANDOFF" 2>/dev/null)" "pinned re-acquire refused" "the handoff names the refusal"
assert_contains "$(cat "$HANDOFF" 2>/dev/null)" "claim left to its current holder" "and that the claim was not released"

echo "=== T8: an unserved pin past the claim TTL is released; a fresh one is not ==="
reset
abandon >/dev/null
"$DISPATCHER" --complete-dispatches >/dev/null 2>&1 || true
[[ -f "$PIN" ]] && ok "control: a fresh pin survives the cleanup pass" || bad "a fresh pin was expired"
mkdir -p "${PIN%/*}"
printf '%s\n%s\nworker\n' "$TARGET" "$(( $(date +%s) - 1900 ))" > "$PIN"
out=$("$DISPATCHER" --complete-dispatches 2>&1 >/dev/null | tr -d '\r' || true)
assert_contains "$out" "pinned resume not served within 1800s by pool-2 — claim released" "the stale pin is released"
assert_eq "$(cat "$FLEET_CLAIM_LOG")" "amending-release 3763 pool-2" "the claim is released"
[[ ! -f "$PIN" && ! -f "$SIDECAR" ]] && ok "pin and sidecar cleared" || bad "pin or sidecar left after expiry"

echo "=== T9: the second abandon releases, and the release clears the pin ==="
reset
abandon >/dev/null
out=$(abandon)
assert_contains "$out" "abandoned twice by pool-2 — claim released" "second abandon releases"
assert_eq "$(cat "$FLEET_CLAIM_LOG")" "amending-release 3763 pool-2" "through the lane's release arm"
[[ ! -f "$PIN" ]] && ok "the release clears the pin" || bad "the pin outlived the release"

echo "=== T10: a sidecar for another role cannot resume the target: no pin, released ==="
reset
sed -i.bak 's/"role":"worker"/"role":"sonnet-reviewer"/' "$SIDECAR" && rm -f "$SIDECAR.bak"
out=$(abandon)
assert_contains "$out" "abandoned on first exit (no resumable session)" "a role mismatch releases"
[[ ! -f "$PIN" ]] && ok "no pin written" || bad "a pin was written for an unresumable sidecar"

# A Codex sidecar carries no session id: the reserved-resume route relaunches
# its recorded target on the sidecar's own class, model and effort.
CODEX_BIN="$TMPROOT/codex-bin"; mkdir -p "$CODEX_BIN"
printf '#!/usr/bin/env bash\nexit 0\n' > "$CODEX_BIN/codex"; chmod +x "$CODEX_BIN/codex"
reset_codex() {
    reset
    printf '{"role":"worker","model":"gpt-5.6-sol","effort":"high","runtime":"codex","target":"%s","class":"opus","dispatch_id":"D1","created_epoch":1}\n' \
        "$TARGET" > "$SIDECAR"
}

echo "=== T11: a Codex sidecar pins the pane and relaunches the target on the Codex route ==="
reset_codex
write_slice "$EMPTY_SLICE"
out=$(FLEET_RUNTIMES=claude,codex abandon)
assert_contains "$out" "retrying once — pane pinned to resume its session" "a Codex sidecar is resumable"
assert_eq "$(sed -n 1p "$PIN" 2>/dev/null)" "$TARGET" "the pin names the abandoned target"
out=$(PATH="$CODEX_BIN:$PATH" FLEET_RUNTIMES=claude,codex tick 1)
assert_contains "$out" "dispatching worker -> %2 [target=$TARGET] runtime=codex" \
    "the pinned pane is dispatched on its target through the Codex route"
assert_eq "$(count "$out" 'dispatching ')" 1 "no free pane launched beside it"
assert_contains "$(cat "$SEND_LOG")" \
    "fleet-dispatch-wrap pane-2 gpt-5.6-sol high worker '' live target=$TARGET codex opus" \
    "the wrapper gets the sidecar's model, effort, runtime and class"
assert_contains "$(cat "$FLEET_CLAIM_LOG")" "amending-claim 3763 pool-2" "re-acquired as the incumbent"
[[ ! -f "$PIN" ]] && ok "the pin is consumed by its dispatch" || bad "the pin outlived its dispatch"

echo "=== T12: a Codex pin in the Codex cooldown is held, pin kept, nothing re-acquired ==="
reset_codex
write_slice "$EMPTY_SLICE"
FLEET_RUNTIMES=claude,codex abandon >/dev/null
mkdir -p "$FLEET_STATE_DIR/runtime-cooldown"
printf '{"until":%s}\n' "$(( $(date +%s) + 900 ))" > "$FLEET_STATE_DIR/runtime-cooldown/codex.json"
out=$(PATH="$CODEX_BIN:$PATH" FLEET_RUNTIMES=claude,codex tick 1)
rm -rf "$FLEET_STATE_DIR/runtime-cooldown"
assert_eq "$(count "$out" 'dispatching ')" 0 "nothing launched during the Codex cooldown"
assert_contains "$out" "a reserved pane is held" "the pinned pane is held like a reserved one"
[[ -f "$PIN" ]] && ok "the pin is kept for the resume" || bad "the pin was lost"
assert_absent "$(cat "$FLEET_CLAIM_LOG")" "amending-claim" "no re-acquire while held"

summarize "dispatcher abandon pin"
