#!/usr/bin/env bash

set -uo pipefail
SCRIPT_DIR=$(cd "$(dirname "$0")/.." && pwd)
source "$(dirname "$0")/lib_preflight.sh"
source "$(dirname "$0")/lib_assert.sh"
DISPATCHER="$SCRIPT_DIR/fleet-dispatcher"

TMPROOT=""
cleanup() { [[ -n "$TMPROOT" && -d "$TMPROOT" ]] && rm -rf "$TMPROOT"; }
trap cleanup EXIT
TMPROOT=$(mktemp -d)
source "$(dirname "$0")/lib_hermetic.sh"
hermetic_poison_gh_env "$TMPROOT"

export HOME="$TMPROOT/home"
export FLEET_STATE_DIR="$TMPROOT/state"
export FLEET_SESSIONS_DIR="$TMPROOT/sessions"
export FLEET_RESERVATIONS_DIR="$TMPROOT/reservations"
export FLEET_ALERTS_DIR="$TMPROOT/alerts"
export FLEET_CONF=/dev/null
export FLEET_SESSION="fleet-test-$$"
export FLEET_DISPATCH_MIN_GAP_SECONDS=0
export BOOT_FANOUT_WINDOW_SECONDS=0
export FLEET_CONCURRENCY_EPIC_STEWARD=2
export FLEET_RUNTIMES=claude
mkdir -p "$FLEET_STATE_DIR/dispatch" "$FLEET_STATE_DIR/triggers" \
    "$FLEET_STATE_DIR/usage" "$FLEET_STATE_DIR/runtime-cooldown" \
    "$FLEET_SESSIONS_DIR" "$FLEET_RESERVATIONS_DIR" "$FLEET_ALERTS_DIR"

REPO="$TMPROOT/repo"
mkdir -p "$REPO"
git -C "$REPO" init -q
git -C "$REPO" config user.email test@example.invalid
git -C "$REPO" config user.name Test
printf 'seed\n' > "$REPO/seed"
git -C "$REPO" add seed
git -C "$REPO" commit -qm seed
git -C "$REPO" branch -M master
HEALTHY="$REPO/pool-healthy"
BROKEN="$REPO/pool-broken"
git -C "$REPO" worktree add -q -b fleet/pool-healthy "$HEALTHY" master
git -C "$REPO" worktree add -q -b fleet/pool-broken "$BROKEN" master
BROKEN_ADMIN=$(sed -n 's/^gitdir: //p' "$BROKEN/.git")
rm -rf "$BROKEN_ADMIN"

BIN="$TMPROOT/bin"
mkdir -p "$BIN"
export SEND_LOG="$TMPROOT/send.log"
export CLAIM_LOG="$TMPROOT/claim.log"
: > "$SEND_LOG"
: > "$CLAIM_LOG"
cat > "$BIN/tmux" <<'STUB'
#!/usr/bin/env bash
sub="$1"; shift
case "$sub" in
    has-session) exit 0 ;;
    list-panes) printf '%%1|pool|zsh\n%%2|pool|zsh\n' ;;
    display-message)
        if [[ "$*" == *pane_current_path* ]]; then
            if [[ "$*" == *%1* && -n "${STUB_FAKE:-}" ]]; then
                printf '/fake/.claude/worktrees/pool-fake\n'
            elif [[ "$*" == *%1* ]]; then
                printf '%s\n' "$BROKEN"
            fi
            [[ "$*" == *%2* ]] && printf '%s\n' "$HEALTHY"
        elif [[ "$*" == *pane_pid* ]]; then
            printf '1\n'
        fi
        ;;
    send-keys)
        printf '%s\n' "$*" >> "$SEND_LOG"
        : > "$TRIGGER"
        ;;
esac
exit 0
STUB
cat > "$BIN/fleet-claim" <<'STUB'
#!/usr/bin/env bash
printf '%s\n' "$*" >> "$CLAIM_LOG"
exit 99
STUB
printf '#!/usr/bin/env bash\nexit 1\n' > "$BIN/pgrep"
printf '#!/usr/bin/env bash\nexit 99\n' > "$BIN/gh"
chmod +x "$BIN"/*
export BROKEN HEALTHY SEND_LOG CLAIM_LOG
export TRIGGER="$FLEET_STATE_DIR/triggers/epic-steward"
export PATH="$BIN:$PATH"

tick() {
    : > "$TRIGGER"
    "$DISPATCHER" --dispatch-role epic-steward 2 2>&1 >/dev/null
}

echo "T1: a broken pane is skipped while a healthy pane dispatches"
out=$(tick)
assert_absent "$(<"$SEND_LOG")" "-t %1" "broken pane receives no send-keys"
assert_contains "$(<"$SEND_LOG")" "-t %2" "healthy pane is dispatched"
assert_eq "$(<"$CLAIM_LOG")" "" "gate performs no claim operation for the broken pane"
assert_contains "$out" "skipping pane %1 (pool-broken)" "first skip is logged"
[[ -f "$FLEET_ALERTS_DIR/dispatch-pane-pool-broken" ]] \
    && ok "broken pane alert is written" || bad "broken pane alert missing"

echo "T2: the standing trigger stays quiet on the identical second tick"
skip_count=$(printf '%s\n' "$out" | grep -cF 'skipping pane %1' || true)
assert_eq "$skip_count" "1" "two in-process ticks log the persistent skip once"
assert_absent "$out" "no fresh dispatch" "gate-only second tick suppresses generic retry spam"

echo "T3: healing clears the alert"
mkdir -p "$BROKEN_ADMIN"
printf 'ref: refs/heads/fleet/pool-broken\n' > "$BROKEN_ADMIN/HEAD"
printf '../..\n' > "$BROKEN_ADMIN/commondir"
printf '%s/.git\n' "$BROKEN" > "$BROKEN_ADMIN/gitdir"
git -C "$BROKEN" reset -q
: > "$SEND_LOG"
rm -f "$FLEET_STATE_DIR/dispatch"/*.json
tick >/dev/null
[[ ! -e "$FLEET_ALERTS_DIR/dispatch-pane-pool-broken" ]] \
    && ok "healthy pass clears the alert" || bad "healed pane alert survived"

echo "T4: a non-existent pane path keeps the existing dispatch behavior"
: > "$SEND_LOG"
rm -f "$FLEET_STATE_DIR/dispatch"/*.json
STUB_FAKE=1 tick >/dev/null
assert_contains "$(<"$SEND_LOG")" "-t %1" "non-existent path is still dispatched"

summarize "dispatcher pane registration gate"
