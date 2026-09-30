#!/usr/bin/env bash
# Tests the host-profile knobs fleet-dispatcher reads from fleet-up.conf:
#
#   FLEET_DISPATCH_ROLES          — the exact set of roles this host serves
#   FLEET_WORKER_HOST_PINNED_ONLY — the worker lane elects only items pinned
#                                   to this host (`**Host:** <key>`)
#
# Together they are the "satellite host" profile
# (docs/agents/FLEET-CROSS-HOST-SMOKE.md § "Satellite host profile"): the
# native-Windows box that only clears
# fleet:needs-windows-smoke and takes `**Host:** windows` tasks, while the
# merger, reviewer, and unpinned worker lanes stay on the primary fleet.
#
# fleet-up sources the same conf before gating its bootstrap triggers and
# launching the dispatcher; T5 drives it to prove a caller's value outranks
# the conf on both sides, and that `--satellite` outranks both.
#
# A cap of 0 means UNCAPPED in dispatch_role, so the served-role list is the
# only way to switch a lane off — T3 is the load-bearing arm: a tick with a
# standing merger trigger must leave it unread when merger is not served.
#
# Hermetic: stubbed tmux / pgrep / fleet-claim / gh, a sandbox state dir, and
# an explicit conf file. No live GitHub, no live ~/.fleet.

set -uo pipefail

SCRIPT_DIR=$(cd "$(dirname "$0")/.." && pwd)
source "$(dirname "$0")/lib_preflight.sh"
DISPATCHER="$SCRIPT_DIR/fleet-dispatcher"
FLEET_UP="$SCRIPT_DIR/fleet-up"
[[ -x "$DISPATCHER" ]] || { echo "SKIP: fleet-dispatcher not found at $DISPATCHER" >&2; exit 3; }
[[ -x "$FLEET_UP" ]] || { echo "SKIP: fleet-up not found at $FLEET_UP" >&2; exit 3; }

# shellcheck source=/dev/null
source "$SCRIPT_DIR/tests/lib_assert.sh"

TMPROOT=""
cleanup() { [[ -n "$TMPROOT" && -d "$TMPROOT" ]] && rm -rf "$TMPROOT"; }
trap cleanup EXIT
TMPROOT=$(mktemp -d)
source "$(dirname "$0")/lib_hermetic.sh"
hermetic_poison_gh_env "$TMPROOT"

export FLEET_STATE_DIR="$TMPROOT/state"
export FLEET_CONF="$TMPROOT/fleet-up.conf"
export FLEET_RESERVATIONS_DIR="$TMPROOT/reservations"
export FLEET_SESSION="fleet-test-$$"
export FLEET_DISPATCH_MIN_GAP_SECONDS=0
export FLEET_MODEL_PROBE=0
export BOOT_FANOUT_WINDOW_SECONDS=0
mkdir -p "$FLEET_STATE_DIR/projections" "$FLEET_STATE_DIR/dispatch" \
         "$FLEET_STATE_DIR/triggers" "$FLEET_RESERVATIONS_DIR"
# The knobs under test are read from the conf; make sure the pane's own
# environment cannot pre-empt them.
unset FLEET_DISPATCH_ROLES FLEET_WORKER_HOST_PINNED_ONLY FLEET_SMOKE_WORKER FLEET_EPIC_STEWARD
unset FLEET_RUNTIMES FLEET_CROSS_PROVIDER_REVIEW FLEET_WORKER_RUNTIME
# A dispatched pane exports its host's caps (FLEET_CONCURRENCY_WORKER=5 and
# friends), which outrank the conf; T4c asserts the default cap.
# shellcheck disable=SC2046
unset $(compgen -v FLEET_CONCURRENCY_)

# One idle pool pane; pgrep exits 1 so it reads as not running a wrapper.
STUB_BIN="$TMPROOT/bin"; mkdir -p "$STUB_BIN"
cat > "$STUB_BIN/tmux" <<'TMUXEOF'
#!/usr/bin/env bash
sub="$1"; shift
case "$sub" in
    has-session) exit 0 ;;
    list-panes) printf '%%1|pool|zsh\n'; exit 0 ;;
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
# Any claim or gh call from this suite is a test defect: fail closed.
printf '#!/usr/bin/env bash\necho "unexpected test claim: $*" >&2; exit 99\n' > "$STUB_BIN/fleet-claim"
printf '#!/usr/bin/env bash\nexit 99\n' > "$STUB_BIN/gh"
chmod +x "$STUB_BIN/tmux" "$STUB_BIN/pgrep" "$STUB_BIN/fleet-claim" "$STUB_BIN/gh"
export PATH="$STUB_BIN:$PATH"
export FLEET_CLAIMS_DIR="$TMPROOT/claims"
export FLEET_SESSIONS_DIR="$TMPROOT/sessions"

TIMEOUT_BIN=""
if command -v timeout >/dev/null 2>&1; then
    TIMEOUT_BIN="timeout"
elif command -v gtimeout >/dev/null 2>&1; then
    TIMEOUT_BIN="gtimeout"
fi
run_dispatcher() {  # run_dispatcher <host> <args...>
    local host="$1"; shift
    if [[ -n "$TIMEOUT_BIN" ]]; then
        FLEET_TEST_HOST="$host" "$TIMEOUT_BIN" 30 "$DISPATCHER" "$@"
    else
        FLEET_TEST_HOST="$host" "$DISPATCHER" "$@"
    fi
}
config_line() { run_dispatcher windows --print-config 2>/dev/null | tail -n 1; }
roles_field() { config_line | sed -n 's/^roles=\(.*\) pinned-only=.*/\1/p'; }
pinned_field() { config_line | sed -n 's/^roles=.* pinned-only=\([01]\);.*/\1/p'; }

# ======================================================================
echo "T1: no knob -> the default served set, pinned-only off"
: > "$FLEET_CONF"
assert_eq "$(roles_field)" "merger sonnet-reviewer opus-reviewer worker" "default roles"
assert_eq "$(pinned_field)" "0" "pinned-only defaults off"

echo "T1b: the legacy opt-in flags still append"
printf 'FLEET_SMOKE_WORKER=1\nFLEET_EPIC_STEWARD=1\n' > "$FLEET_CONF"
assert_eq "$(roles_field)" "merger sonnet-reviewer opus-reviewer worker smoke-worker epic-steward" \
    "FLEET_SMOKE_WORKER / FLEET_EPIC_STEWARD append their roles"

echo "T2: FLEET_DISPATCH_ROLES is the exact served set, in canonical order"
printf 'FLEET_DISPATCH_ROLES="worker, smoke-worker"\n' > "$FLEET_CONF"
assert_eq "$(roles_field)" "worker smoke-worker" "comma/space list, canonical order, no opt-in flag needed"
printf 'FLEET_DISPATCH_ROLES="smoke-worker"\nFLEET_SMOKE_WORKER=0\n' > "$FLEET_CONF"
assert_eq "$(roles_field)" "smoke-worker" "naming a role enables it even with its opt-in flag off"

echo "T2b: an unknown name is dropped with a warning; the rest are kept"
printf 'FLEET_DISPATCH_ROLES="bogus worker"\n' > "$FLEET_CONF"
warn=$(run_dispatcher windows --print-config 2>&1 >/dev/null)
assert_contains "$warn" "names unknown role bogus" "unknown role is reported"
assert_eq "$(roles_field)" "worker" "known roles survive"

echo "T2c: a list of only unknown names serves nothing, visibly"
printf 'FLEET_DISPATCH_ROLES="bogus"\n' > "$FLEET_CONF"
assert_eq "$(config_line | cut -d' ' -f1)" "roles=none" "empty served set prints roles=none"

echo "T2d: the caller's environment outranks the conf"
printf 'FLEET_DISPATCH_ROLES="merger"\n' > "$FLEET_CONF"
out=$(FLEET_DISPATCH_ROLES="worker" run_dispatcher windows --print-config 2>/dev/null | tail -n 1)
assert_contains "$out" "roles=worker " "env FLEET_DISPATCH_ROLES wins over conf"

# ======================================================================
echo "T3: a tick serves only the listed roles — the merger trigger stays unread"
printf 'FLEET_DISPATCH_ROLES="worker smoke-worker"\n' > "$FLEET_CONF"
printf 'live\n' > "$FLEET_STATE_DIR/dispatch-mode"
: > "$FLEET_STATE_DIR/triggers/merger"
: > "$FLEET_STATE_DIR/triggers/sonnet-reviewer"
printf '{"smoke_pending_prs":[]}\n' > "$FLEET_STATE_DIR/projections/smoke-worker.json"
: > "$FLEET_STATE_DIR/triggers/smoke-worker"
out=$(run_dispatcher windows --dispatch-tick 2>&1 >/dev/null)
assert_absent "$out" "merger:" "no merger lane activity"
assert_absent "$out" "sonnet-reviewer:" "no sonnet-reviewer lane activity"
if [[ -f "$FLEET_STATE_DIR/triggers/merger" ]]; then
    ok "merger trigger left in place (unserved role, never consumed)"
else
    bad "merger trigger left in place (unserved role, never consumed)"
fi
assert_contains "$out" "smoke-worker: no smoke work pending for this host" \
    "positive control: a served role is still ticked"

# ======================================================================
echo "T4: FLEET_WORKER_HOST_PINNED_ONLY narrows the worker election to this host's pins"
cat > "$FLEET_STATE_DIR/projections/worker.json" <<'JSON'
{"tasks_open": [
  {"issue": "#40", "repo": "engine", "number": 40, "model": "opus", "owner": "free", "blocked": false},
  {"issue": "#41", "repo": "engine", "number": 41, "model": "opus", "owner": "free", "blocked": false, "needs_host": "windows"},
  {"issue": "#42", "repo": "engine", "number": 42, "model": "opus", "owner": "free", "blocked": false, "needs_host": "linux"}
], "feedback_prs": [], "semantic_conflict_prs": [], "needs_plan": []}
JSON
printf 'FLEET_DISPATCH_ROLES="worker smoke-worker"\n' > "$FLEET_CONF"
out=$(run_dispatcher windows --resolve-class worker 2>/dev/null)
assert_contains "$out" "class=opus" "mode off: the lane elects"
assert_contains "$out" "count=2" "mode off: the unpinned and the windows-pinned task both count"

printf 'FLEET_DISPATCH_ROLES="worker smoke-worker"\nFLEET_WORKER_HOST_PINNED_ONLY=1\n' > "$FLEET_CONF"
assert_eq "$(pinned_field)" "1" "the conf knob reaches the config line"
out=$(run_dispatcher windows --resolve-class worker 2>/dev/null)
assert_contains "$out" "class=opus" "mode on: the windows-pinned task is still elected"
assert_contains "$out" "count=1" "mode on: only the windows-pinned task counts"

echo "T4b: mode on with no pin for this host routes nothing (lane-default gate, not a class)"
out=$(run_dispatcher linux --resolve-class worker 2>/dev/null)
assert_contains "$out" "count=1" "each host sees only its own pin (#42 on linux)"
out=$(run_dispatcher mac --resolve-class worker 2>/dev/null)
assert_contains "$out" "class= model= " "no mac pin: empty class, no lane-default dispatch"
assert_contains "$out" "defer=0" "and not a defer either — the '' gate stands the lane down"

echo "T4c: the knob is not a cap — worker stays served and its cap untouched"
assert_contains "$(config_line)" "roles=worker smoke-worker pinned-only=1; caps worker=4" \
    "served roles and caps unchanged by the pinned-only mode"
printf 'FLEET_DISPATCH_ROLES="worker smoke-worker"\nFLEET_WORKER_HOST_PINNED_ONLY=1\nFLEET_CONCURRENCY_WORKER=2\n' > "$FLEET_CONF"
assert_contains "$(config_line)" "roles=worker smoke-worker pinned-only=1; caps worker=2" \
    "a configured worker cap is kept under the pinned-only mode"

# ======================================================================
# fleet-up launch path. fleet-up sources the same conf before it gates the
# bootstrap triggers and launches the dispatcher, so a caller's export has to
# survive that source for both to see it. Driven through the real script to
# its session-exists exit: this tmux stub records the environment its
# `has-session` child inherits — the export state the nohup'd dispatcher
# inherits later in the same boot — and the dispatcher is then run on exactly
# that environment.
UP_BIN="$TMPROOT/up-bin"; mkdir -p "$UP_BIN" "$TMPROOT/home"
UP_ENV_DUMP="$TMPROOT/fleet-up-child-env"
cat > "$UP_BIN/tmux" <<'TMUXEOF'
#!/usr/bin/env bash
if [[ "$1" == "has-session" ]]; then
    : > "$UP_ENV_DUMP"
    for k in FLEET_DISPATCH_ROLES FLEET_WORKER_HOST_PINNED_ONLY; do
        [[ -n "${!k+x}" ]] && printf '%s=%s\n' "$k" "${!k}" >> "$UP_ENV_DUMP"
    done
    exit 0
fi
exit 0
TMUXEOF
printf '#!/usr/bin/env bash\nexit 0\n' > "$UP_BIN/claude"
chmod +x "$UP_BIN/tmux" "$UP_BIN/claude"
run_fleet_up() {  # run_fleet_up [VAR=value...] [fleet-up arg...] — prints fleet-up's host-profile line
    rm -f "$UP_ENV_DUMP"
    local a envs=() args=()
    for a in "$@"; do
        if [[ "$a" == *=* ]]; then envs+=("$a"); else args+=("$a"); fi
    done
    env HOME="$TMPROOT/home" PATH="$UP_BIN:$PATH" UP_ENV_DUMP="$UP_ENV_DUMP" \
        ${envs[@]+"${envs[@]}"} "$BASH" "$FLEET_UP" ${args[@]+"${args[@]}"} 2>&1 \
        | grep '^fleet-up: host profile'
}
up_child_env() { [[ -f "$UP_ENV_DUMP" ]] && tr '\n' ' ' < "$UP_ENV_DUMP"; }
# The dispatcher's config line under the environment fleet-up handed down.
up_dispatcher_config() {
    (
        while IFS= read -r kv; do export "${kv?}"; done < "$UP_ENV_DUMP"
        config_line
    )
}

echo "T5: fleet-up keeps the caller's host-profile knobs across its conf source"
printf 'FLEET_DISPATCH_ROLES="merger"\nFLEET_WORKER_HOST_PINNED_ONLY=0\n' > "$FLEET_CONF"
out=$(run_fleet_up FLEET_DISPATCH_ROLES="worker" FLEET_WORKER_HOST_PINNED_ONLY=1)
assert_contains "$out" "host profile: full" "no flag: the full profile"
assert_contains "$out" "dispatch roles: worker;" "bootstrap gating sees the caller's roles, not the conf's"
assert_contains "$out" "worker host-pinned-only: 1" "the caller's pinned-only outranks the conf"
assert_eq "$(up_child_env)" "FLEET_DISPATCH_ROLES=worker FLEET_WORKER_HOST_PINNED_ONLY=1 " \
    "fleet-up's children inherit the caller's values, not the conf's"
assert_contains "$(up_dispatcher_config)" "roles=worker pinned-only=1;" \
    "the dispatcher launched on that environment serves the caller's set"

echo "T5b: with no caller value the conf decides, and the dispatcher re-reads it"
out=$(run_fleet_up)
assert_contains "$out" "dispatch roles: merger;" "bootstrap gating sees the conf's roles"
assert_eq "$(up_child_env)" "" "a conf-sourced knob is not exported to fleet-up's children"
assert_contains "$(up_dispatcher_config)" "roles=merger pinned-only=0;" \
    "the dispatcher resolves the same set from the conf itself"

echo "T5c: an explicit empty caller value asks for the default set on both sides"
out=$(run_fleet_up FLEET_DISPATCH_ROLES=)
assert_contains "$out" "dispatch roles: <default set>;" "bootstrap gating serves every role"
assert_contains "$(up_dispatcher_config)" "roles=merger sonnet-reviewer opus-reviewer worker pinned-only=0;" \
    "the dispatcher serves the default set too"

echo "T5d: --satellite outranks both the conf and the caller's own knobs"
out=$(run_fleet_up FLEET_DISPATCH_ROLES="merger" --satellite)
assert_contains "$out" "host profile: satellite" "the flag names the profile on the boot line"
assert_contains "$out" "dispatch roles: smoke-worker worker;" "the satellite served set"
assert_contains "$out" "worker host-pinned-only: 1" "the satellite pinned-only mode"

summarize "fleet-dispatcher host-profile knobs (FLEET_DISPATCH_ROLES / FLEET_WORKER_HOST_PINNED_ONLY)"
