#!/usr/bin/env bash
# test_daemon_reload.sh — persistent-daemon self-reload at the tick boundary.
#
# Both persistent fleet daemons bind their source ONCE — fleet-dispatcher is
# bash (a function body is parsed at exec and never re-parsed while the loop
# runs), fleet-state-scout is python (modules bound at import). Mechanism and
# the operator diagnostic: docs/agents/FLEET-CACHE.md
# §"Daemon source staleness".
#
# What the E2E arms actually prove, and why the assertions are shaped this way:
#
#   - The reload happens: a log line naming the surface transition appears
#     within a few ticks of editing a sandboxed surface file.
#   - It reloads IN PLACE: the second `started` line carries the SAME pid. That
#     is the regression lock on the lock-adoption arm in
#     acquire_dispatcher_lock. `exec` keeps the pid and does NOT run the EXIT
#     trap (verified), so the lock dir survives with this process recorded as
#     its owner; without the `owner_pid == $$` arm the liveness probe asks
#     `kill -0 $$`, which a running process always answers yes to, and the
#     freshly exec'd image exits with "another dispatcher is already running".
#     A different pid, or no second `started` at all, is that bug.
#   - The new code is what is running: the `rev=` field on the second `started`
#     line differs from the first. Asserting only that a reload was LOGGED
#     would pass on a daemon that logs and then fails to exec.
#
# The daemons are driven in a sandbox HOME so nothing touches the operator's
# live ~/.fleet: both derive every state/log/pid path from $HOME, and PATH is
# stubbed so no tmux session is found (dispatch pass skipped) and no gh token
# is minted. No live GitHub, no live ~/.fleet.

set -uo pipefail
SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)
source "$SCRIPT_DIR/lib_preflight.sh"
FLEET_DIR=$(cd "$SCRIPT_DIR/.." && pwd)
source "$SCRIPT_DIR/lib_assert.sh"

DISPATCHER="$FLEET_DIR/fleet-dispatcher"
SCOUT="$FLEET_DIR/fleet-state-scout"
COMMON="$FLEET_DIR/fleet-common.sh"
for subject in "$DISPATCHER" "$SCOUT" "$COMMON"; do
    if [[ ! -f "$subject" ]]; then
        echo "SKIP: subject under test not found: $subject" >&2
        exit 3
    fi
done

TMPROOT=""
DAEMON_PIDS=()
cleanup() {
    local p
    for p in ${DAEMON_PIDS[@]+"${DAEMON_PIDS[@]}"}; do
        kill -TERM "$p" 2>/dev/null
    done
    sleep 1
    for p in ${DAEMON_PIDS[@]+"${DAEMON_PIDS[@]}"}; do
        kill -9 "$p" 2>/dev/null
    done
    [[ -n "$TMPROOT" && -d "$TMPROOT" ]] && rm -rf "$TMPROOT"
    return 0
}
trap cleanup EXIT
TMPROOT=$(mktemp -d)

# ======================================================================
# Unit: fleet_surface_hash
# ======================================================================
echo "T1-T4: fleet_surface_hash"
# shellcheck source=../fleet-common.sh
source "$COMMON"

U="$TMPROOT/unit"
mkdir -p "$U"
echo one >"$U/a"
echo two >"$U/b"

h_first=$(fleet_surface_hash "$U/a" "$U/b")
h_again=$(fleet_surface_hash "$U/a" "$U/b")
assert_eq "$h_again" "$h_first" "T1: stable across calls on unchanged files"

echo changed >"$U/b"
h_edited=$(fleet_surface_hash "$U/a" "$U/b")
if [[ "$h_edited" != "$h_first" ]]; then
    ok "T2: changes when a file's contents change"
else
    bad "T2: changes when a file's contents change"
fi

# A missing entry must hash to something distinct from that entry being absent
# from the surface altogether. This is what makes an operator creating or
# deleting $FLEET_CONF register as a source change rather than as no change —
# a hash that simply SKIPS missing files reports the two states identically.
h_with_missing=$(fleet_surface_hash "$U/a" "$U/nonexistent")
h_without=$(fleet_surface_hash "$U/a")
if [[ "$h_with_missing" != "$h_without" ]]; then
    ok "T3: a missing entry is a distinct token, not a skipped one"
else
    bad "T3: a missing entry is a distinct token, not a skipped one"
fi

# Order is part of the identity, so a surface reordering is a change too.
h_ordered=$(fleet_surface_hash "$U/a" "$U/b")
h_reversed=$(fleet_surface_hash "$U/b" "$U/a")
if [[ "$h_ordered" != "$h_reversed" ]]; then
    ok "T4: entry order is part of the aggregate"
else
    bad "T4: entry order is part of the aggregate"
fi

# ======================================================================
# Unit: fleet_reload_gate
# ======================================================================
echo "T5-T7: fleet_reload_gate"
GATE="$TMPROOT/gate-history"
gate_calls=""
for _ in 1 2 3 4; do
    if fleet_reload_gate "$GATE" 3 900; then gate_calls+="A"; else gate_calls+="S"; fi
done
assert_eq "$gate_calls" "AAAS" "T5: max=3 permits three attempts then suppresses the fourth"

# <max> is the count PERMITTED, so the smallest meaningful cap still permits
# one reload. Under a `<` spelling FLEET_RELOAD_MAX=1 refuses every reload — an
# operator setting the documented floor gets no reloads at all, and no runtime
# signal distinguishes that from a quiet surface.
GATE1="$TMPROOT/gate-history-max1"
gate_calls=""
for _ in 1 2; do
    if fleet_reload_gate "$GATE1" 1 900; then gate_calls+="A"; else gate_calls+="S"; fi
done
assert_eq "$gate_calls" "AS" "T5b: max=1 permits one attempt, not zero"

# ...and 0 is therefore the operator's kill switch, under both spellings of the
# comparison. Pinned so the documented disable value can't drift into meaning
# "one reload" on a later edit of this gate.
GATE0="$TMPROOT/gate-history-max0"
if fleet_reload_gate "$GATE0" 0 900; then
    bad "T5c: max=0 permits nothing (documented kill switch)"
else
    ok "T5c: max=0 permits nothing (documented kill switch)"
fi

# A zero-width window prunes every prior entry, so the cap re-arms — the
# drain half of the oscillation cap, without making the test sleep 15 minutes.
if fleet_reload_gate "$GATE" 3 0; then
    ok "T6: entries outside the window are pruned, re-arming the cap"
else
    bad "T6: entries outside the window are pruned, re-arming the cap"
fi

# Independent state files must not share a budget.
GATE2="$TMPROOT/gate-history-2"
if fleet_reload_gate "$GATE2" 3 900; then
    ok "T7: a fresh state file starts with a fresh budget"
else
    bad "T7: a fresh state file starts with a fresh budget"
fi

# ======================================================================
# Parity ratchet: one surface entry per `source` line, plus the script itself
# ======================================================================
echo "T8-T9: dispatcher surface/source parity"
source_lines=$(grep -cE '^[[:space:]]*source ' "$DISPATCHER")
surface_entries=$(awk '
    /^DAEMON_SOURCE_SURFACE=\(/ { inarr = 1; next }
    inarr && /^\)/             { exit }
    inarr && NF                { n++ }
    END                        { print n + 0 }
' "$DISPATCHER")
# The script itself is in the surface but is not a `source` line, hence +1.
assert_eq "$surface_entries" "$((source_lines + 1))" \
    "T8: DAEMON_SOURCE_SURFACE has one entry per \`source\` line plus the script"

# A count-only ratchet passes on a surface of five wrong paths. Name one entry
# that must be there: fleet-clone-freshness.sh is reached only by `source`, so
# a merged fix to it runs inert unless the surface covers it.
if grep -qE '^\s*"\$FLEET_LIB_DIR/fleet-common\.sh"' "$DISPATCHER"; then
    ok "T9: surface names fleet-common.sh explicitly"
else
    bad "T9: surface names fleet-common.sh explicitly"
fi

# ======================================================================
# --print-surface + corpus simulation, both directions
# ======================================================================
echo "T10-T16: --print-surface and the inert-commit corpus"
disp_surface=$("$DISPATCHER" --print-surface 2>&1)
disp_rc=$?
assert_eq "$disp_rc" "0" "T10: fleet-dispatcher --print-surface exits 0"
disp_paths=$(printf '%s\n' "$disp_surface" | grep -vc '^aggregate')
if (( disp_paths >= 5 )); then
    ok "T11: dispatcher surface lists >=5 paths ($disp_paths)"
else
    bad "T11: dispatcher surface lists >=5 paths ($disp_paths)"
fi
assert_contains "$disp_surface" "aggregate" "T12: dispatcher surface prints an aggregate"

scout_surface=$(python3 "$SCOUT" --print-surface 2>&1)
scout_rc=$?
assert_eq "$scout_rc" "0" "T13: fleet-state-scout --print-surface exits 0"
scout_paths=$(printf '%s\n' "$scout_surface" | grep -vc '^aggregate')
if (( scout_paths >= 6 )); then
    ok "T14: scout surface lists >=6 paths ($scout_paths)"
else
    bad "T14: scout surface lists >=6 paths ($scout_paths)"
fi

# Positive direction: each file a daemon binds at load time is inside its own
# daemon's surface, so a merged change to it triggers a reload. The four shapes
# a surface has to cover are the daemon script itself, a `source`d file, the
# scout body, and a module the scout imports.
for f in fleet-dispatcher fleet-clone-freshness.sh fleet-common.sh; do
    assert_contains "$disp_surface" "/$f" "T15: dispatcher surface covers $f"
done
for f in fleet-state-scout fleet_stack_base.py fleet_gh_poll.py; do
    assert_contains "$scout_surface" "/$f" "T15: scout surface covers $f"
done

# Negative direction, and the reason this ratchet is not just a longer
# positive list: fleet_task_class.py is spawned as a SUBPROCESS by both
# daemons, so it already picks up merged fixes and a reload on it would be
# spurious churn. A surface that swept the whole directory would pass every
# assertion above and fail here.
assert_absent "$disp_surface" "fleet_task_class.py" \
    "T16: dispatcher surface excludes the subprocess-reloaded class resolver"
assert_absent "$scout_surface" "fleet_task_class.py" \
    "T16: scout surface excludes the subprocess-reloaded class resolver"

# ======================================================================
# E2E harness
# ======================================================================
# Stage a whole copy of scripts/fleet so the daemon's FLEET_LIB_DIR resolves
# inside the sandbox and editing a "source file" edits the sandbox's, never
# the repo's.
STAGE="$TMPROOT/lib"
cp -R "$FLEET_DIR" "$STAGE"
SANDBOX_HOME="$TMPROOT/home"
mkdir -p "$SANDBOX_HOME/.fleet/state" "$SANDBOX_HOME/.fleet/logs" "$TMPROOT/bin"

# Stub every outbound edge so the sandboxed daemons stay hermetic — no live
# GitHub, no live ~/.fleet. `gh` and `git` failing is a state the scout
# already handles (it logs "degraded: preserving last-known-good" and ticks
# on), which is exactly the condition the reload check must survive: it sits
# OUTSIDE the try/except around tick_once(), so a daemon that cannot reach
# GitHub must still be able to load its own fix. tmux exiting 1 makes
# session_exists false, so the dispatcher never reaches its dispatch pass.
# The stubs must precede the real binaries on PATH, which is why SANDBOX_PATH
# puts $TMPROOT/bin ahead of /usr/bin.
for stub in gh git tmux; do
    printf '#!/usr/bin/env bash\nexit 1\n' >"$TMPROOT/bin/$stub"
done
printf '#!/usr/bin/env bash\nexit 0\n' >"$TMPROOT/bin/fleet-gh-token"
chmod +x "$TMPROOT/bin"/*
# Stubs first (they must shadow a real gh/git on this host), then the running
# bash's directory ahead of /bin. The dispatcher re-execs through its
# `#!/usr/bin/env bash` shebang, so the bash PATH order decides which
# interpreter the RELOADED image gets: this daemon needs bash 4 and macOS's
# /bin/bash is 3.2, which cannot even parse it.
SANDBOX_PATH="$TMPROOT/bin":"$(dirname "$BASH")":/usr/bin:/bin:/usr/sbin:/sbin

# wait_for <file> <fixed-string> <seconds> — poll rather than sleep a flat
# worst case.
#
# Every limit is 12s, and the budget is deliberate: run_all.sh kills a suite at
# 120s, and there are 8 waits, so generous per-wait limits sum past the cap and
# a TOTAL regression comes back as "timed out after 120s" instead of naming the
# assertions that failed — the diagnostic is worth more than the headroom.
# 8 x 12 = 96s worst case. The daemons tick at 1s and a reload needs two ticks
# (debounce) plus the exec, observed at ~2-3s, so 12s is ~4x headroom; the
# happy path returns as soon as the condition holds and the whole suite runs
# in ~12s.
wait_for() {
    local file="$1" needle="$2" limit="$3" waited=0
    while (( waited < limit )); do
        [[ -f "$file" ]] && grep -qF -- "$needle" "$file" && return 0
        sleep 1
        waited=$((waited + 1))
    done
    return 1
}

started_revs() { grep -o 'rev=[0-9a-f]*' "$1" | sed 's/rev=//'; }
started_pids() { grep -o 'started (pid=[0-9]*' "$1" | sed 's/.*pid=//'; }

# wait_for_boots <log> <n> <seconds> — wait until the log holds N `started`
# lines. NOT wait_for on a substring of that line: every marker on a boot line
# ("rearm=", "interval=") is already present from boot 1, so a substring wait
# returns instantly and the count assertion after it races the re-exec'd image
# rather than waiting for it. That vacuous wait passed standalone and failed
# only under a loaded `run_all.sh` — the assertion has to wait on the thing it
# is about to assert.
wait_for_boots() {
    local file="$1" want="$2" limit="$3" waited=0
    while (( waited < limit )); do
        [[ -f "$file" ]] && (( $(started_pids "$file" | wc -l) >= want )) && return 0
        sleep 1
        waited=$((waited + 1))
    done
    return 1
}

# ======================================================================
# E2E: fleet-dispatcher
# ======================================================================
echo "T17-T20: fleet-dispatcher reloads in place"
DLOG="$TMPROOT/dispatcher.log"
env -i HOME="$SANDBOX_HOME" PATH="$SANDBOX_PATH" \
    FLEET_ENGINE_ROOT="$TMPROOT/no-such-engine" \
    FLEET_DISPATCHER_INTERVAL=1 \
    "$BASH" "$STAGE/fleet-dispatcher" >"$DLOG" 2>&1 &
DISP_PID=$!
DAEMON_PIDS+=("$DISP_PID")

if wait_for "$DLOG" "started (pid=" 12; then
    ok "T17: sandboxed dispatcher booted"
else
    bad "T17: sandboxed dispatcher booted"
    echo "        log (tail):"; tail -15 "$DLOG" | sed 's/^/          | /'
fi
boot_rev=$(started_revs "$DLOG" | head -1)

# Edit a SOURCED file rather than the daemon script — the harder half: the
# running image re-reads it only through the surface hash.
echo "# reload probe" >>"$STAGE/fleet-common.sh"

if wait_for "$DLOG" "reloading: source surface advanced" 12; then
    ok "T18: dispatcher logs the reload after a surface edit"
else
    bad "T18: dispatcher logs the reload after a surface edit"
    echo "        log (tail):"; tail -15 "$DLOG" | sed 's/^/          | /'
fi

if wait_for_boots "$DLOG" 2 12; then
    ok "T19: dispatcher printed a second 'started' line (it re-exec'd)"
else
    bad "T19: dispatcher printed a second 'started' line (it re-exec'd)"
    echo "        log (tail):"; tail -15 "$DLOG" | sed 's/^/          | /'
fi

# The lock-adoption regression lock. Same pid across both boots means the
# process survived the exec and adopted its own lock dir; a second pid, or a
# missing second line, is the naive-exec self-kill.
disp_pids=$(started_pids "$DLOG" | sort -u | wc -l | tr -d ' ')
disp_boots=$(started_pids "$DLOG" | wc -l | tr -d ' ')
if [[ "$disp_pids" == "1" && "$disp_boots" -ge 2 ]]; then
    ok "T20a: both boots share one pid (lock adopted, no self-kill)"
else
    bad "T20a: both boots share one pid (lock adopted, no self-kill)"
    echo "        pids: $(started_pids "$DLOG" | tr '\n' ' ')"
fi
reload_rev=$(started_revs "$DLOG" | tail -1)
if [[ -n "$boot_rev" && -n "$reload_rev" && "$boot_rev" != "$reload_rev" ]]; then
    ok "T20b: the reloaded image reports a different rev ($boot_rev -> $reload_rev)"
else
    bad "T20b: the reloaded image reports a different rev ($boot_rev -> $reload_rev)"
fi

kill -TERM "$DISP_PID" 2>/dev/null

# ======================================================================
# E2E: fleet-state-scout
# ======================================================================
echo "T21-T23: fleet-state-scout reloads in place"
SLOG="$TMPROOT/scout.log"
SCOUT_HOME="$TMPROOT/home-scout"
mkdir -p "$SCOUT_HOME/.fleet/state"
# The scout aborts when the engine repo is absent, so give it an empty dir to
# find. Its ticks will fail without gh — irrelevant, and deliberately so: the
# reload check sits OUTSIDE the try/except around tick_once(), which is the
# property this arm exercises by construction.
mkdir -p "$SCOUT_HOME/src/IrredenEngine"
env -i HOME="$SCOUT_HOME" PATH="$SANDBOX_PATH" \
    python3 "$STAGE/fleet-state-scout" --interval 1 >"$SLOG" 2>&1 &
SCOUT_PID=$!
DAEMON_PIDS+=("$SCOUT_PID")

if wait_for "$SLOG" "started (pid=" 12; then
    ok "T21: sandboxed scout booted"
else
    bad "T21: sandboxed scout booted"
    echo "        log (tail):"; tail -15 "$SLOG" | sed 's/^/          | /'
fi

# The published-revision half: a consumer must be able to compare the
# cache against on-disk source without inspecting the process. Read the
# aggregate from a FRESH --print-surface process and require the running
# daemon's cache to agree — that equality IS the diagnostic, and it is exactly
# what breaks when the running scout is stale.
SCOUT_STATE="$SCOUT_HOME/.fleet/state/state.json"
if wait_for "$SCOUT_STATE" "scout_source_rev" 12; then
    ok "T21b: sandboxed scout stamped scout_source_rev into state.json"
else
    bad "T21b: sandboxed scout stamped scout_source_rev into state.json"
fi
boot_agg=$(python3 "$STAGE/fleet-state-scout" --print-surface | awk -F'\t' '/^aggregate/{print $2}')
state_rev=$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1])).get("scout_source_rev",""))' "$SCOUT_STATE" 2>/dev/null)
assert_eq "$state_rev" "$boot_agg" "T21c: state.json rev equals --print-surface aggregate"

# Edit an IMPORTED module, not the script — the closure half, and what
# _source_surface()'s sys.modules derivation exists to cover.
echo "# reload probe" >>"$STAGE/fleet_stack_base.py"

if wait_for "$SLOG" "reloading: source surface advanced" 12; then
    ok "T22: scout logs the reload after a closure-module edit"
else
    bad "T22: scout logs the reload after a closure-module edit"
    echo "        log (tail):"; tail -15 "$SLOG" | sed 's/^/          | /'
fi

wait_for_boots "$SLOG" 2 12
scout_pids=$(started_pids "$SLOG" | sort -u | wc -l | tr -d ' ')
scout_boots=$(started_pids "$SLOG" | wc -l | tr -d ' ')
if [[ "$scout_pids" == "1" && "$scout_boots" -ge 2 ]]; then
    ok "T23: both scout boots share one pid (execv kept the pid)"
else
    bad "T23: both scout boots share one pid (execv kept the pid)"
    echo "        pids: $(started_pids "$SLOG" | tr '\n' ' ')"
fi

# ...and the reloaded image must republish. A cache still carrying the OLD
# revision after a reload would mean the daemon logged a reload it did not
# perform — the failure T23 catches from the process side, caught here from
# the artifact side.
reload_agg=$(python3 "$STAGE/fleet-state-scout" --print-surface | awk -F'\t' '/^aggregate/{print $2}')
if [[ "$reload_agg" != "$boot_agg" ]]; then
    ok "T24a: editing a closure module moved the on-disk aggregate"
else
    bad "T24a: editing a closure module moved the on-disk aggregate"
fi
if wait_for "$SCOUT_STATE" "$reload_agg" 12; then
    ok "T24b: the reloaded scout republished the new rev into state.json"
else
    bad "T24b: the reloaded scout republished the new rev into state.json"
    echo "        state rev: $(python3 -c 'import json,sys; print(json.load(open(sys.argv[1])).get("scout_source_rev",""))' "$SCOUT_STATE" 2>/dev/null)"
    echo "        expected : $reload_agg"
fi

kill -TERM "$SCOUT_PID" 2>/dev/null

summarize "daemon self-reload"
