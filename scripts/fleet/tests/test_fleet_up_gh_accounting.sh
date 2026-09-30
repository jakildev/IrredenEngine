#!/usr/bin/env bash
# Tests for fleet-up's GitHub CLI accounting wiring and fleet-down's teardown:
#
#   resolve_gh_accounting — pins the real gh (never the launcher) and an
#     absolute interpreter, or turns accounting off with a reason;
#   the daemon launch environment — a `gh` run from the dispatcher's env
#     reaches the launcher first on PATH and is counted as `dispatcher`;
#   seed_tmux_pane_env + the new-session block — on a PRE-EXISTING tmux
#     server, pool-1 (the new-session pane) and split panes both start with
#     this boot's values, never PATH and never a stale token;
#   clear_tmux_gh_accounting — removes exactly the accounting names, with or
#     without a fleet session, and is idempotent.
#
# Functions and the new-session block are sed-extracted from the real
# scripts. tmux runs on a private socket dir (TMUX_TMPDIR), never the host's
# server; without tmux on PATH the tmux half is skipped loudly.

set -uo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$SCRIPT_DIR/lib_assert.sh"
source "$SCRIPT_DIR/lib_hermetic.sh"

FLEET_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
FLEET_UP="$FLEET_DIR/fleet-up"
FLEET_DOWN="$FLEET_DIR/fleet-down"
LAUNCHER_SRC="$FLEET_DIR/gh-accounting-bin/gh"
for subject in "$FLEET_UP" "$FLEET_DOWN" "$LAUNCHER_SRC"; do
    if [[ ! -f "$subject" ]]; then
        echo "SKIP: subject not found: $subject" >&2
        exit 3
    fi
done

TMPROOT="$(mktemp -d "${TMPDIR:-/tmp}/fleet-up-gh.XXXXXX")"
SKIP=0
skip() { SKIP=$((SKIP + 1)); echo "  SKIP: $1"; }
cleanup() {
    [[ -n "${TMUX_TMPDIR:-}" ]] && tmux kill-server 2>/dev/null
    rm -rf "$TMPROOT"
}
trap cleanup EXIT
hermetic_poison_gh_env "$TMPROOT"
export HOME="$TMPROOT/home"
mkdir -p "$HOME"
for v in FLEET_GH_ACCOUNTING FLEET_GH_REAL FLEET_GH_PYTHON FLEET_GH_LAUNCHER \
         FLEET_GH_EVENT_ROOT FLEET_GH_ACTOR FLEET_ROLE; do
    unset "$v"
done

IS_WINDOWS=0
case "$(uname -s)" in MINGW*|MSYS*|CYGWIN*) IS_WINDOWS=1 ;; esac
native() { if (( IS_WINDOWS )); then cygpath -m "$1"; else printf '%s' "$1"; fi; }
PYTHON="$(type -P python3)"

extract_fn() {  # extract_fn <file> <name>
    sed -n "/^$2() {/,/^}/p" "$1"
}
eval "$(extract_fn "$FLEET_UP" resolve_gh_accounting)"
eval "$(extract_fn "$FLEET_UP" seed_tmux_pane_env)"
eval "$(extract_fn "$FLEET_DOWN" clear_tmux_gh_accounting)"
for fn in resolve_gh_accounting seed_tmux_pane_env clear_tmux_gh_accounting; do
    [[ "$(type -t "$fn")" == "function" ]] || { echo "test setup: $fn not extracted" >&2; exit 2; }
done

# The launcher is used in place, so it resolves the real fleet_github.py.
LAUNCHER="$LAUNCHER_SRC"
LAUNCHER_DIR="${LAUNCHER%/*}"
REAL_DIR="$TMPROOT/real"
mkdir -p "$REAL_DIR"
cat > "$REAL_DIR/gh" <<EOF
#!$PYTHON
import sys
print("REAL", *sys.argv[1:])
EOF
chmod +x "$REAL_DIR/gh"
if (( IS_WINDOWS )); then
    printf '@"%s" "%%~dp0gh" %%*\r\n' "$(native "$PYTHON")" > "$REAL_DIR/gh.bat"
fi
SYS_PATH="$PATH"

echo "1. resolve_gh_accounting pins the real gh, never the launcher"
(
    PATH="$LAUNCHER_DIR:$REAL_DIR:$SYS_PATH"
    resolve_gh_accounting "$LAUNCHER" || { echo "resolve failed"; exit 1; }
    printf 'acct=%s\nreal=%s\npy=%s\nlauncher=%s\nroot=%s\n' "$FLEET_GH_ACCOUNTING" \
        "$FLEET_GH_REAL" "$FLEET_GH_PYTHON" "$FLEET_GH_LAUNCHER" "$FLEET_GH_EVENT_ROOT"
) > "$TMPROOT/resolved" 2>&1
resolved="$(cat "$TMPROOT/resolved")"
assert_contains "$resolved" "acct=1" "accounting on"
if (( IS_WINDOWS )); then
    assert_contains "$resolved" "real=$(native "$REAL_DIR/gh")" "pinned in mixed form"
    assert_contains "$resolved" "py=$(native "$PYTHON")" "interpreter pinned in mixed form"
    [[ "$(sed -n 's/^py=//p' "$TMPROOT/resolved")" == *.exe ]] \
        && ok "the interpreter pin carries its .exe" || bad "interpreter pin lacks .exe"
else
    assert_contains "$resolved" "real=$REAL_DIR/gh" "the first non-launcher gh on PATH"
    assert_contains "$resolved" "py=$PYTHON" "absolute interpreter"
fi
assert_contains "$resolved" "root=$(native "$HOME/.fleet/state/gh-accounting")" "event root under fleet state"

out="$(FLEET_GH_ACCOUNTING=0 PATH="$REAL_DIR:$SYS_PATH" bash -c "$(extract_fn "$FLEET_UP" resolve_gh_accounting)"'
    resolve_gh_accounting "$0"; echo "rc=$? acct=${FLEET_GH_ACCOUNTING-unset} real=${FLEET_GH_REAL-unset}"' "$LAUNCHER" 2>&1)"
assert_contains "$out" "rc=1 acct=unset real=unset" "FLEET_GH_ACCOUNTING=0 turns it off"

mkdir -p "$TMPROOT/nogh"
out="$(PATH="$TMPROOT/nogh:$LAUNCHER_DIR" "$BASH" -c "$(extract_fn "$FLEET_UP" resolve_gh_accounting)"'
    resolve_gh_accounting "$0"; echo "rc=$? acct=${FLEET_GH_ACCOUNTING-unset}"' "$LAUNCHER" 2>&1)"
assert_contains "$out" "rc=1 acct=unset" "a PATH holding only the launcher resolves to off, not to itself"
assert_contains "$out" "accounting off (gh=missing" "and says why"

echo "2. a gh from the dispatcher's launch environment is counted as dispatcher"
EVENTS="$TMPROOT/events"
# What native Python's PATH walk finds there: the twin on Windows.
PINNED="$REAL_DIR/gh"
(( IS_WINDOWS )) && PINNED="$REAL_DIR/gh.bat"
out="$(env PATH="$LAUNCHER_DIR:$REAL_DIR:$SYS_PATH" FLEET_GH_ACTOR=dispatcher \
        FLEET_GH_ACCOUNTING=1 FLEET_GH_REAL="$(native "$PINNED")" \
        FLEET_GH_PYTHON="$PYTHON" FLEET_GH_LAUNCHER="$(native "$LAUNCHER")" \
        FLEET_GH_EVENT_ROOT="$(native "$EVENTS")" \
        bash -c 'gh pr list --repo o/r' 2>&1)"
assert_contains "$out" "REAL pr list --repo o/r" "the real gh answered through the launcher"
events="$(find "$EVENTS" -type f -name '*.json' ! -name '.*' 2>/dev/null)"
assert_eq "$(printf '%s\n' "$events" | grep -c .)" "1" "exactly one event"
assert_contains "$(cat $events 2>/dev/null)" '"actor": "dispatcher"' "attributed to the dispatcher"
assert_contains "$(grep -A2 '^    nohup env' "$FLEET_UP")" "FLEET_GH_ACTOR=scout" \
    "fleet-up launches the scout as actor scout"
assert_contains "$(grep '_dispatcher_env=(' "$FLEET_UP")" "FLEET_GH_ACTOR=dispatcher" \
    "fleet-up launches the dispatcher as actor dispatcher"

echo "3. one owned-name list across fleet-up, fleet-down and fleet_github"
names_of() { grep -oE 'FLEET_GH_[A-Z_]+' | sort -u | tr '\n' ' '; }
up_names="$(extract_fn "$FLEET_UP" seed_tmux_pane_env | names_of)"
down_names="$(extract_fn "$FLEET_DOWN" clear_tmux_gh_accounting | names_of)"
module_names="$(grep -E '^ENV_[A-Z_]+ = "FLEET_GH_' "$FLEET_DIR/fleet_github.py" \
                | grep -v FLEET_GH_ACTOR | names_of)"
assert_eq "$up_names" "$module_names" "fleet-up seeds every pane-facing accounting name"
assert_eq "$down_names" "$up_names" "fleet-down clears exactly what fleet-up seeds"

echo "4. pool-1 and split panes on a pre-existing tmux server"
if ! command -v tmux >/dev/null 2>&1; then
    skip "no tmux on PATH — pane seeding not exercised on this host"
else
    export TMUX_TMPDIR="$TMPROOT/tmux"
    mkdir -p "$TMUX_TMPDIR"
    unset TMUX
    # The previous boot's server, with its stale values still in the global env.
    tmux new-session -d -s previous "sleep 600"
    tmux set-environment -g FLEET_GH_REAL "/stale/gh"
    tmux set-environment -g GH_TOKEN "stale-app-token"
    tmux set-environment -g FLEET_GH_LAUNCHER "/stale/launcher"

    ENGINE="$TMPROOT/engine"
    mkdir -p "$ENGINE/.claude/worktrees/pool-1"
    DUMP="$TMPROOT/dump"
    mkdir -p "$DUMP"
    printf '#!%s\nenv > "%s/$1.env"\nsleep 600\n' "$BASH" "$DUMP" > "$TMPROOT/dump-env"
    chmod +x "$TMPROOT/dump-env"
    SESSION=fleet-test
    TRANSIENT_PANE_CMD="$TMPROOT/dump-env pool-1"
    IR_FLEET_WORKERS=6 FLEET_FABLE_FALLBACK="opus" _gh_app_token=""
    export FLEET_GH_ACCOUNTING=1 FLEET_GH_REAL="/new/gh" FLEET_GH_PYTHON="/new/python3" \
           FLEET_GH_LAUNCHER="/new/launcher" FLEET_GH_EVENT_ROOT="/new/events"
    block="$(sed -n '/^if tmux list-sessions >\/dev\/null 2>&1; then$/,/^seed_tmux_pane_env$/p' "$FLEET_UP")"
    assert_contains "$block" "tmux new-session -d -s \"\$SESSION\"" "the new-session block was extracted"
    eval "$block"
    tmux split-window -t "$SESSION" "$TMPROOT/dump-env split"
    for _ in $(seq 1 50); do
        [[ -s "$DUMP/pool-1.env" && -s "$DUMP/split.env" ]] && break
        sleep 0.1
    done
    for pane in pool-1 split; do
        envf="$(cat "$DUMP/$pane.env" 2>/dev/null)"
        assert_contains "$envf" "FLEET_GH_REAL=/new/gh" "$pane: this boot's pinned gh"
        assert_contains "$envf" "FLEET_GH_LAUNCHER=/new/launcher" "$pane: this boot's launcher"
        assert_contains "$envf" "IR_FLEET_WORKERS=6" "$pane: IR_FLEET_WORKERS"
        assert_absent "$envf" "stale-app-token" "$pane: the stale App token is evicted"
        assert_absent "$envf" "FLEET_GH_ACTOR" "$pane: no daemon actor leaks into a pane"
    done
    global="$(tmux show-environment -g)"
    assert_absent "$global" "gh-accounting-bin" "no launcher PATH prefix is stored globally"

    echo "5. fleet-down clears only its names, session or not, repeatably"
    tmux kill-session -t "$SESSION"
    clear_tmux_gh_accounting
    clear_tmux_gh_accounting
    global="$(tmux show-environment -g)"
    assert_absent "$global" "FLEET_GH_" "every accounting name is gone"
    assert_contains "$global" "IR_FLEET_WORKERS=6" "names fleet-down does not own stay"
    tmux kill-server 2>/dev/null
    unset TMUX_TMPDIR
    clear_tmux_gh_accounting && ok "no server at all is not an error" || bad "clear failed with no server"
fi

[[ "$SKIP" -gt 0 ]] && echo "" && echo "($SKIP check group(s) skipped — see notes above)"
summarize "fleet-up GitHub accounting tests"
