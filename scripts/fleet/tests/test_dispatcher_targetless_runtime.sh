#!/usr/bin/env bash
# Provider election for target-less dispatches. The epic steward is the
# target-less role; the merger is target-bound and routes through `route`.

set -uo pipefail

SCRIPT_DIR=$(cd "$(dirname "$0")/.." && pwd)
source "$(dirname "$0")/lib_preflight.sh"
DISPATCHER="$SCRIPT_DIR/fleet-dispatcher"
[[ -x "$DISPATCHER" ]] || { echo "test setup: fleet-dispatcher not found" >&2; exit 1; }
source "$(dirname "$0")/lib_assert.sh"

TMPROOT=""
cleanup() { [[ -n "$TMPROOT" && -d "$TMPROOT" ]] && rm -rf "$TMPROOT"; }
trap cleanup EXIT
TMPROOT=$(mktemp -d)
source "$(dirname "$0")/lib_hermetic.sh"
hermetic_poison_gh_env "$TMPROOT"

export FLEET_STATE_DIR="$TMPROOT/state"
export FLEET_SESSIONS_DIR="$TMPROOT/sessions"
export FLEET_RESERVATIONS_DIR="$TMPROOT/reservations"
export FLEET_CONF=/dev/null
export FLEET_SESSION="fleet-test-$$"
export FLEET_DISPATCH_MIN_GAP_SECONDS=0
export BOOT_FANOUT_WINDOW_SECONDS=0
export FLEET_CONCURRENCY_MERGER=1
export FLEET_CONCURRENCY_EPIC_STEWARD=1
mkdir -p "$FLEET_STATE_DIR/dispatch" "$FLEET_STATE_DIR/triggers" \
  "$FLEET_STATE_DIR/usage" "$FLEET_STATE_DIR/runtime-cooldown" \
  "$FLEET_SESSIONS_DIR" "$FLEET_RESERVATIONS_DIR"

for name in FLEET_DISPATCHER_USAGE_GATE FLEET_DISPATCHER_USAGE_GATE_FIVE_HOUR \
  FLEET_WORKER_RUNTIME FLEET_CODEX_MODEL_FABLE FLEET_CODEX_MODEL_OPUS \
  FLEET_CODEX_MODEL_SONNET FLEET_CODEX_EFFORT_FABLE FLEET_CODEX_EFFORT_OPUS \
  FLEET_CODEX_EFFORT_SONNET FLEET_EFFORT FLEET_EFFORT_MERGER \
  FLEET_EFFORT_EPIC_STEWARD FLEET_MODEL_MERGER FLEET_MODEL_EPIC_STEWARD; do
    unset "$name"
done
export FLEET_RUNTIMES=claude,codex
export FLEET_MODEL_SONNET=sonnet
export FLEET_MODEL_OPUS=opus

BIN="$TMPROOT/bin"
mkdir -p "$BIN"
export SEND_LOG="$TMPROOT/send-keys.log"
cat > "$BIN/tmux" <<'TMUXEOF'
#!/usr/bin/env bash
sub="$1"; shift
case "$sub" in
    has-session) exit 0 ;;
    list-panes) printf '%%1|pool|zsh\n' ;;
    display-message)
        if [[ "$*" == *pane_current_path* ]]; then
            printf '/fake/.claude/worktrees/pool-1\n'
        elif [[ "$*" == *pane_pid* ]]; then
            printf '1\n'
        fi
        ;;
    send-keys) printf '%s\n' "$*" >> "$SEND_LOG" ;;
esac
exit 0
TMUXEOF
printf '#!/usr/bin/env bash\nexit 1\n' > "$BIN/pgrep"
printf '#!/usr/bin/env bash\nexit 99\n' > "$BIN/fleet-claim"
printf '#!/usr/bin/env bash\nexit 99\n' > "$BIN/gh"
printf '#!/usr/bin/env bash\nexit 0\n' > "$BIN/codex"
chmod +x "$BIN"/*
export PATH="$BIN:$PATH"

NOW=$(date +%s)
RESETS=$(python3 -c "import datetime,time; print(datetime.datetime.fromtimestamp(time.time()+3600,tz=datetime.timezone.utc).strftime('%Y-%m-%dT%H:%M:%SZ'))")
close_gate() {
    printf '{"rateLimitType":"five_hour","utilization":0.99,"resetsAt":"%s","observed_at":%s}\n' \
        "$RESETS" "$NOW" > "$FLEET_STATE_DIR/usage/five_hour.json"
}
open_gate() { rm -f "$FLEET_STATE_DIR/usage/five_hour.json"; }

tick() {
    rm -f "$FLEET_STATE_DIR/dispatch"/*.json
    : > "$SEND_LOG"
    : > "$FLEET_STATE_DIR/triggers/epic-steward"
    env "$@" "$DISPATCHER" --dispatch-role epic-steward 1 2>&1 >/dev/null
}
TRIGGER="$FLEET_STATE_DIR/triggers/epic-steward"

echo "T1: closed Claude gate elects Codex for the epic steward"
close_gate
out=$(tick)
assert_contains "$out" "dispatching epic-steward -> %1 runtime=codex" \
  "closed gate launches epic steward on Codex"
assert_contains "$(<"$SEND_LOG")" \
  "fleet-dispatch-wrap pane-1 gpt-5.6-sol medium epic-steward '' live target= codex opus" \
  "Codex epic steward uses the target-less 9-argument launch"
assert_absent "$out" "claude-quota-closed" "successful fallback is not reported as Claude-blocked"

echo "T2: open gate keeps the legacy Claude launch"
open_gate
out=$(tick)
assert_contains "$out" "dispatching epic-steward -> %1 runtime=claude" \
  "open gate keeps epic steward on Claude"
assert_contains "$(<"$SEND_LOG")" \
  "fleet-dispatch-wrap pane-1 opus xhigh epic-steward '' live" \
  "open gate keeps the legacy six-argument launch"
assert_absent "$(<"$SEND_LOG")" "target=" "legacy launch carries no target argument"

echo "T3: a Claude pin waits at the closed gate"
close_gate
out=$(tick FLEET_WORKER_RUNTIME=claude)
assert_contains "$out" "claude-quota-closed" "Claude pin reports the closed gate"
assert_absent "$out" "dispatching epic-steward" "Claude pin does not launch"
[[ -f "$TRIGGER" ]] && ok "blocked launch keeps trigger" || bad "blocked launch consumed trigger"

echo "T4: Codex cooldown keeps the trigger"
printf '{"until":%s}\n' "$((NOW + 900))" > "$FLEET_STATE_DIR/runtime-cooldown/codex.json"
out=$(tick)
assert_contains "$out" "codex-cooldown" "live Codex cooldown blocks fallback"
assert_absent "$out" "dispatching epic-steward" "cooling provider does not launch"
[[ -f "$TRIGGER" ]] && ok "cooldown keeps trigger" || bad "cooldown consumed trigger"
rm -f "$FLEET_STATE_DIR/runtime-cooldown/codex.json"

echo "T5: missing Codex binary keeps the trigger"
NO_CODEX_BIN="$TMPROOT/no-codex-bin"
mkdir -p "$NO_CODEX_BIN"
cp "$BIN/tmux" "$BIN/pgrep" "$BIN/fleet-claim" "$BIN/gh" "$NO_CODEX_BIN/"
out=$(PATH="$NO_CODEX_BIN:/usr/bin:/bin" tick)
assert_contains "$out" "codex-unavailable" "missing Codex binary blocks fallback"
assert_absent "$out" "dispatching epic-steward" "missing Codex binary does not launch"
[[ -f "$TRIGGER" ]] && ok "unavailable provider keeps trigger" \
  || bad "unavailable provider consumed trigger"

echo "T6: target-less sidecars never enter reserved-target routing"
printf '{"runtime":"codex","target":"","role":"epic-steward","session_id":"sid"}\n' \
  > "$FLEET_SESSIONS_DIR/pool-1.session.json"
out=$(tick)
assert_contains "$out" "dispatching epic-steward -> %1 runtime=codex" \
  "empty-target sidecar does not prevent target-less dispatch"
assert_absent "$out" "resume-route-failed" "target-less role skips reserved-target routing"
rm -f "$FLEET_SESSIONS_DIR/pool-1.session.json"

echo "T7: an unset runtime list leaves Claude-only behavior unchanged"
out=$(tick FLEET_RUNTIMES=)
assert_contains "$out" "dispatching epic-steward -> %1 runtime=claude" \
  "legacy host launches Claude without provider election"
assert_contains "$(<"$SEND_LOG")" "fleet-dispatch-wrap pane-1 opus xhigh epic-steward '' live" \
  "legacy host keeps six-argument launch"

echo "T8: an explicit Codex pin overrides an open Claude gate"
open_gate
out=$(tick FLEET_WORKER_RUNTIME=codex)
assert_contains "$out" "dispatching epic-steward -> %1 runtime=codex" \
  "Codex pin elects Codex while Claude is open"
assert_contains "$(<"$SEND_LOG")" "target= codex opus" "Codex pin uses the 9-argument launch"

summarize "target-less runtime dispatcher tests"
