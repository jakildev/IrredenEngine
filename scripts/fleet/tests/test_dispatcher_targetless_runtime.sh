#!/usr/bin/env bash
# Provider election for the merger (target-bound: one `merge:<repo>:<N>` line
# per launch, provider from the PR's author label or a pin) and for the
# epic steward (target-less batch role: provider from the Claude usage gate).

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

# The merger slice the dispatcher routes `merge:engine:77` against; $1 is the
# PR's label list as JSON.
merger_slice() {
    mkdir -p "$FLEET_STATE_DIR/projections"
    printf '{"prs": [], "merger_candidates": [{"number": 77, "repo": "engine", "labels": %s}]}\n' \
        "$1" > "$FLEET_STATE_DIR/projections/merger.json"
}

tick() {
    local role="$1"; shift
    rm -f "$FLEET_STATE_DIR/dispatch"/*.json
    : > "$SEND_LOG"
    if [[ "$role" == merger ]]; then
        printf 'merge:engine:77\n' > "$FLEET_STATE_DIR/triggers/$role"
    else
        : > "$FLEET_STATE_DIR/triggers/$role"
    fi
    env "$@" "$DISPATCHER" --dispatch-role "$role" 1 2>&1 >/dev/null
}

echo "T1: a Codex-authored PR launches its merger on Codex behind a closed Claude gate"
close_gate
merger_slice '["fleet:author-codex"]'
out=$(tick merger)
assert_contains "$out" "dispatching merger -> %1 [target=merge:engine:77]" "Codex-authored PR is launched"
assert_contains "$out" "runtime=codex" "closed Claude gate does not stop the Codex launch"
assert_contains "$(<"$SEND_LOG")" \
  "fleet-dispatch-wrap pane-1 gpt-5.6-sol medium merger '' live target=merge:engine:77 codex opus" \
  "Codex merger carries its target in the 9-argument launch"
assert_absent "$out" "claude-quota-closed" "Codex launch is not reported as Claude-blocked"

echo "T2: an open gate keeps a Claude-authored PR on Claude"
open_gate
merger_slice '["fleet:author-claude"]'
out=$(tick merger)
assert_contains "$out" "dispatching merger -> %1 [target=merge:engine:77]" "Claude-authored PR is launched"
assert_contains "$out" "runtime=claude" "open gate keeps merger on Claude"
assert_contains "$(<"$SEND_LOG")" \
  "fleet-dispatch-wrap pane-1 sonnet high merger '' live target=merge:engine:77 claude opus" \
  "Claude merger carries its target and provider"
assert_absent "$(<"$SEND_LOG")" "codex" "Claude launch names no Codex argument"

echo "T3: a Claude-authored PR waits at the closed gate"
close_gate
out=$(tick merger)
assert_contains "$out" "claude-quota-closed" "Claude-authored PR reports the closed gate"
assert_absent "$out" "dispatching merger" "closed gate does not launch a Claude merger"
[[ -f "$FLEET_STATE_DIR/triggers/merger" ]] && ok "blocked launch keeps trigger" \
  || bad "blocked launch consumed trigger"

echo "T4: Codex cooldown keeps the trigger"
merger_slice '["fleet:author-codex"]'
printf '{"until":%s}\n' "$((NOW + 900))" > "$FLEET_STATE_DIR/runtime-cooldown/codex.json"
out=$(tick merger)
assert_contains "$out" "codex-cooldown" "live Codex cooldown blocks the Codex launch"
assert_absent "$out" "dispatching merger" "cooling provider does not launch"
[[ -f "$FLEET_STATE_DIR/triggers/merger" ]] && ok "cooldown keeps trigger" \
  || bad "cooldown consumed trigger"
rm -f "$FLEET_STATE_DIR/runtime-cooldown/codex.json"

echo "T5: missing Codex binary keeps the trigger"
NO_CODEX_BIN="$TMPROOT/no-codex-bin"
mkdir -p "$NO_CODEX_BIN"
cp "$BIN/tmux" "$BIN/pgrep" "$BIN/fleet-claim" "$BIN/gh" "$NO_CODEX_BIN/"
out=$(PATH="$NO_CODEX_BIN:/usr/bin:/bin" tick merger)
assert_contains "$out" "codex-unavailable" "missing Codex binary blocks the Codex launch"
assert_absent "$out" "dispatching merger" "missing Codex binary does not launch"
[[ -f "$FLEET_STATE_DIR/triggers/merger" ]] && ok "missing binary keeps trigger" \
  || bad "missing binary consumed trigger"

echo "T6: epic steward uses the same target-less fallback"
out=$(tick epic-steward)
assert_contains "$out" "dispatching epic-steward -> %1 runtime=codex" \
  "closed gate launches epic steward on Codex"
assert_contains "$(<"$SEND_LOG")" \
  "gpt-5.6-sol medium epic-steward '' live target= codex opus" \
  "epic steward uses its opus-class Codex assignment"

echo "T7: target-less sidecars never enter reserved-target routing"
printf '{"runtime":"codex","target":"","role":"epic-steward","session_id":"sid"}\n' \
  > "$FLEET_SESSIONS_DIR/pool-1.session.json"
out=$(tick epic-steward)
assert_contains "$out" "dispatching epic-steward -> %1 runtime=codex" \
  "empty-target sidecar does not prevent batch dispatch"
assert_absent "$out" "resume-route-failed" "batch role skips reserved-target routing"
rm -f "$FLEET_SESSIONS_DIR/pool-1.session.json"

echo "T8: an unset runtime list leaves Claude-only behavior unchanged"
merger_slice '["fleet:author-codex"]'
out=$(tick merger FLEET_RUNTIMES=)
assert_contains "$out" "dispatching merger -> %1 [target=merge:engine:77]" \
  "legacy host launches the merger without provider election"
assert_contains "$out" "runtime=claude" "legacy host stays on Claude"
assert_contains "$(<"$SEND_LOG")" "fleet-dispatch-wrap pane-1 sonnet high merger '' live target=merge:engine:77 C-m" \
  "legacy host launch carries no provider argument"

echo "T9: an explicit Codex pin overrides an open Claude gate"
open_gate
merger_slice '[]'
out=$(tick merger FLEET_WORKER_RUNTIME=codex)
assert_contains "$out" "dispatching merger -> %1 [target=merge:engine:77]" \
  "Codex pin launches the merger"
assert_contains "$out" "runtime=codex" "Codex pin elects Codex while Claude is open"

echo "T10: a merger trigger with no target line and no fleet-rebase stands down"
: > "$SEND_LOG"
printf 'llm\n' > "$FLEET_STATE_DIR/triggers/merger"
out=$(PATH="$BIN:$(dirname "$(command -v python3)"):/usr/bin:/bin" "$DISPATCHER" --dispatch-role merger 1 2>&1 >/dev/null)
assert_contains "$out" "merger: no fleet-rebase on PATH and no target line; standing down" \
  "target-less merger without fleet-rebase stands down"
assert_absent "$out" "dispatching merger" "stood-down merger does not launch"
[[ -f "$FLEET_STATE_DIR/triggers/merger" ]] && bad "stand-down kept trigger" \
  || ok "stand-down consumes trigger"

summarize "target-less runtime dispatcher tests"
