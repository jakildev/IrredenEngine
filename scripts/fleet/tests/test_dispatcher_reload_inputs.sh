#!/usr/bin/env bash
# test_dispatcher_reload_inputs.sh — what a self-reloaded fleet-dispatcher
# actually runs on.
#
# test_daemon_reload.sh proves both daemons reload in place. This suite proves
# the dispatcher's reloaded image runs on the right inputs, in the two places
# the running image cannot judge them for it:
#
#   - Config. fleet-up exports its own resolution of every knob, and the
#     running image exports conf-sourced values of its own. An image that
#     inherits either reads the edited conf and then lets the value the OLD
#     conf held outrank it, so the reload logs a new rev and runs the old
#     config. The arms edit a knob, delete a knob, and hold a genuine override
#     across the edit, and read the result off the reloaded image's own
#     `config:` line.
#   - Source membership. A merged change that adds a `source` line adds a file
#     the running image never listed, so a gate over the running image's
#     surface never reads it. The arms add a broken one, require a refusal,
#     then repair ONLY that file — a change the running image hashes nothing
#     of — and require the reload to follow.
#
# The daemon runs in a sandbox HOME with stubbed outbound edges
# (lib_daemon_reload.sh). No live GitHub, no live ~/.fleet.

set -uo pipefail
SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)
source "$SCRIPT_DIR/lib_preflight.sh"
FLEET_DIR=$(cd "$SCRIPT_DIR/.." && pwd)
source "$SCRIPT_DIR/lib_assert.sh"

DISPATCHER="$FLEET_DIR/fleet-dispatcher"
COMMON="$FLEET_DIR/fleet-common.sh"
FLEET_UP="$FLEET_DIR/fleet-up"
for subject in "$DISPATCHER" "$COMMON" "$FLEET_UP"; do
    if [[ ! -f "$subject" ]]; then
        echo "SKIP: subject under test not found: $subject" >&2
        exit 3
    fi
done

TMPROOT=""
DISP_PID=""
cleanup() {
    if [[ -n "$DISP_PID" ]]; then
        kill -TERM "$DISP_PID" 2>/dev/null
        sleep 1
        kill -9 "$DISP_PID" 2>/dev/null
    fi
    [[ -n "$TMPROOT" && -d "$TMPROOT" ]] && rm -rf "$TMPROOT"
    return 0
}
trap cleanup EXIT
TMPROOT=$(mktemp -d)

# ======================================================================
# Unit: fleet_env_override_names
# ======================================================================
echo "T1: fleet_env_override_names"
names=$(env -i PATH="$PATH" FLEET_T_EXPORTED=1 OPUS_MODEL=x OTHER_T=1 \
    "$BASH" -c 'FLEET_T_UNEXPORTED=1; source "$1"; fleet_env_override_names' _ "$COMMON")
assert_contains " $names " " FLEET_T_EXPORTED " "T1a: names an exported FLEET_ knob"
assert_contains " $names " " OPUS_MODEL " "T1b: names the legacy OPUS_MODEL knob"
assert_absent "$names" "FLEET_T_UNEXPORTED" \
    "T1c: omits a shell-local knob — only the launch environment is an override"
assert_absent "$names" "OTHER_T" "T1d: omits a variable outside the knob namespace"
# fleet-up hands the list to the dispatcher's launch and to nothing else. An
# exported copy would reach every pane, where a dispatcher started by hand
# would drop the operator's own `FLEET_X=… fleet-dispatcher` as unlisted.
fleet_up_src=$(<"$FLEET_UP")
assert_contains "$fleet_up_src" '_dispatcher_env=(FLEET_ENV_OVERRIDES="$_fleet_env_overrides")' \
    "T1e: fleet-up passes the list on the dispatcher launch"
assert_contains "$fleet_up_src" 'nohup env ${_dispatcher_env[@]+"${_dispatcher_env[@]}"} "$dispatcher_cmd"' \
    "T1f: ...and that launch is the one that starts the daemon"
assert_absent "$fleet_up_src" "export FLEET_ENV_OVERRIDES" "T1g: fleet-up never exports the list"

# ======================================================================
# Unit: knob precedence, read through --print-config
# ======================================================================
echo "T2-T7: knob precedence under fleet-up's override list"
CONF="$TMPROOT/unit.conf"
printf 'FLEET_CONCURRENCY_WORKER=5\nFLEET_CONCURRENCY_MERGER=1\n' >"$CONF"
UNIT_STATE="$TMPROOT/unit-state"
mkdir -p "$UNIT_STATE"

# print_config [VAR=VALUE]... — the dispatcher's resolved config under exactly
# the given environment.
print_config() {
    env -i HOME="$TMPROOT" PATH="$PATH" FLEET_CONF="$CONF" \
        FLEET_STATE_DIR="$UNIT_STATE" "$@" "$BASH" "$DISPATCHER" --print-config
}

assert_contains "$(print_config FLEET_CONCURRENCY_WORKER=4)" " worker=4 " \
    "T2: with no override list, an inherited knob outranks the conf (standalone precedence)"
assert_contains "$(print_config FLEET_ENV_OVERRIDES= FLEET_CONCURRENCY_WORKER=4)" " worker=5 " \
    "T3: a knob fleet-up resolved, and did not list, yields to the conf"
out=$(print_config FLEET_ENV_OVERRIDES=FLEET_CONCURRENCY_MERGER \
    FLEET_CONCURRENCY_WORKER=4 FLEET_CONCURRENCY_MERGER=2)
assert_contains "$out" " merger=2 " "T4a: a listed override outranks the conf"
assert_contains "$out" " worker=5 " "T4b: ...without shielding the knobs it does not list"
assert_contains "$(print_config FLEET_ENV_OVERRIDES= FLEET_CONCURRENCY_SONNET_REVIEWER=2)" \
    " sonnet-reviewer=4 " \
    "T5: an unlisted knob the conf does not set falls to the built-in default, not fleet-up's copy"

out=$(print_config FLEET_ENV_OVERRIDES= FLEET_MODEL_FABLE='opus[1m]' \
    FLEET_MODEL_FABLE_PROBED='opus[1m]' FLEET_FABLE_FALLBACK=)
assert_contains "$out" "models fable=opus[1m] " \
    "T6a: fleet-up's probe of an unpinned fable class survives the knob being dropped"
assert_contains "$out" " fallback=none " \
    "T6b: ...and a probe that landed on the opus floor still disables the fallback"
printf 'FLEET_MODEL_FABLE=pinned-fable\n' >>"$CONF"
assert_contains "$(print_config FLEET_ENV_OVERRIDES= FLEET_MODEL_FABLE_PROBED='opus[1m]')" \
    "models fable=pinned-fable " "T6c: a conf pin outranks the probe"
printf 'FLEET_MODEL_FABLE_FALLBACK=""\n' >>"$CONF"
assert_contains "$(print_config FLEET_ENV_OVERRIDES= FLEET_FABLE_FALLBACK='opus[1m]')" \
    " fallback=none " \
    "T7: an explicit empty FLEET_MODEL_FABLE_FALLBACK in the conf disables the fallback fleet-up exported"

# ======================================================================
# Ratchet: the dropped-knob list covers what fleet-up exports
# ======================================================================
# A knob fleet-up exports but the list omits keeps outranking the conf on
# every reload, silently. So every knob-namespace name (the namespace
# fleet_env_override_names lists) on an `export` in fleet-up that the
# dispatcher reads must be listed. The exclusions are the two carriers that
# exist to cross into the dispatcher, not resolutions of a conf knob.
echo "T8: FLEET_UP_RESOLVED_KNOBS covers fleet-up's exports"
ratchet=$(python3 - "$FLEET_UP" "$DISPATCHER" <<'PY'
import re, sys
up = open(sys.argv[1]).read()
disp = open(sys.argv[2]).read()
carriers = {"FLEET_ENV_OVERRIDES", "FLEET_MODEL_FABLE_PROBED"}
body = re.search(r"^FLEET_UP_RESOLVED_KNOBS=\((.*?)^\)", disp, re.S | re.M)
listed = set(body.group(1).split()) if body else set()
exported = set()
for line in up.splitlines():
    code = line.split("#", 1)[0]
    for m in re.finditer(r"\bexport\s+([^;]+)", code):
        for token in m.group(1).split():
            name = token.split("=", 1)[0]
            if re.fullmatch(r"FLEET_[A-Z0-9_]+|OPUS_MODEL|SONNET_MODEL", name):
                exported.add(name)
checked = sorted(n for n in exported - carriers
                 if re.search(r"\b%s\b" % n, disp))
missing = [n for n in checked if n not in listed]
print(len(checked))
print(" ".join(missing))
PY
)
checked=$(printf '%s\n' "$ratchet" | sed -n 1p)
missing=$(printf '%s\n' "$ratchet" | sed -n 2p)
if (( checked >= 10 )); then
    ok "T8a: the ratchet reads fleet-up's exports ($checked the dispatcher consumes)"
else
    bad "T8a: the ratchet reads fleet-up's exports ($checked the dispatcher consumes)"
fi
assert_eq "$missing" "" "T8b: every fleet-up export the dispatcher reads is in FLEET_UP_RESOLVED_KNOBS"

# ======================================================================
# E2E harness
# ======================================================================
source "$SCRIPT_DIR/lib_daemon_reload.sh"
stage_daemon_sandbox "$FLEET_DIR"
SANDBOX_HOME="$TMPROOT/home"
mkdir -p "$SANDBOX_HOME/.fleet/state" "$SANDBOX_HOME/.fleet/logs"
E2E_CONF="$SANDBOX_HOME/.fleet/fleet-up.conf"
DLOG="$TMPROOT/dispatcher.log"

# Four waits: 6 + 3 x 10 = 36s worst case, well inside run_all.sh's 120s. A
# dispatcher reload is two one-second ticks plus a sub-second probe; a
# refused image is re-probed every FLEET_RELOAD_REPROBE_TICKS=2 ticks.
WAIT_LIMIT=6
RELOAD_WAIT_LIMIT=10

config_lines() { grep -o 'config: .*' "$1"; }

# The launch environment fleet-up would hand the daemon: its resolution of
# WORKER and SONNET_REVIEWER (both from the conf), and a caller's MERGER=3
# that fleet-up names as an override. The conf ALSO sets MERGER, so sourcing
# it overwrites the exported copy in the running image — which is exactly the
# value a reload that inherited that image's environment would carry forward.
LAUNCH_ENV=(HOME="$SANDBOX_HOME" PATH="$SANDBOX_PATH"
    FLEET_ENGINE_ROOT="$TMPROOT/no-such-engine"
    FLEET_DISPATCHER_INTERVAL=1 FLEET_RELOAD_MAX=10 FLEET_RELOAD_REPROBE_TICKS=2
    FLEET_ENV_OVERRIDES=FLEET_CONCURRENCY_MERGER
    FLEET_CONCURRENCY_WORKER=4 FLEET_CONCURRENCY_SONNET_REVIEWER=2
    FLEET_CONCURRENCY_MERGER=3)
printf 'FLEET_CONCURRENCY_WORKER=4\nFLEET_CONCURRENCY_SONNET_REVIEWER=2\nFLEET_CONCURRENCY_MERGER=1\n' \
    >"$E2E_CONF"

# ======================================================================
# E2E: a conf edit reaches the reloaded image
# ======================================================================
echo "T9-T10: a conf edit takes effect across a reload"
env -i "${LAUNCH_ENV[@]}" "$BASH" "$STAGE/fleet-dispatcher" >"$DLOG" 2>&1 &
DISP_PID=$!

if wait_for_boots "$DLOG" 1 "$WAIT_LIMIT"; then
    ok "T9a: sandboxed dispatcher booted"
else
    bad "T9a: sandboxed dispatcher booted"
    echo "        log (tail):"; tail -15 "$DLOG" | sed 's/^/          | /'
fi
boot_config=$(config_lines "$DLOG" | head -1)
# The control: the fixture resolves as designed before anything is edited.
assert_contains "$boot_config" " worker=4 sonnet-reviewer=2 " "T9b: boot image runs the launch conf"
assert_contains "$boot_config" " merger=3 " "T9c: boot image honors the caller's override"

# Edit one knob, delete another, and move the conf value under the override.
printf 'FLEET_CONCURRENCY_WORKER=5\nFLEET_CONCURRENCY_MERGER=2\n' >"$E2E_CONF"

if wait_for_boots "$DLOG" 2 "$RELOAD_WAIT_LIMIT"; then
    ok "T10a: the conf edit reloaded the dispatcher"
else
    bad "T10a: the conf edit reloaded the dispatcher"
    echo "        log (tail):"; tail -15 "$DLOG" | sed 's/^/          | /'
fi
assert_eq "$(started_pids "$DLOG" | sort -u | wc -l | tr -d ' ')" "1" \
    "T10b: the reload kept the pid"
reload_config=$(config_lines "$DLOG" | sed -n 2p)
assert_contains "$reload_config" " worker=5 " "T10c: an edited knob takes its new conf value"
assert_contains "$reload_config" " sonnet-reviewer=4 " \
    "T10d: a knob deleted from the conf returns to its default, not the launch conf's value"
assert_contains "$reload_config" " merger=3 " \
    "T10e: the caller's override survives a reload that re-sourced a conf setting the same knob"

# ======================================================================
# E2E: a `source` the new image adds is gated, then loaded once repaired
# ======================================================================
echo "T11-T12: a newly sourced file refuses while broken and loads once repaired"
NEW_LIB="$STAGE/fleet-reload-inputs-probe.sh"
# The broken file lands before the line that sources it, so no probe ever sees
# the addition without it.
printf 'if then\n' >"$NEW_LIB"
if python3 - "$STAGE/fleet-dispatcher" <<'PY'
import sys
from pathlib import Path
path = Path(sys.argv[1])
text = path.read_text()
anchor_source = "# --- Per-role concurrency cap config"
anchor_surface = "DAEMON_SOURCE_SURFACE=(\n"
if anchor_source not in text or anchor_surface not in text:
    sys.exit("anchor not found")
text = text.replace(anchor_source,
                    'source "$FLEET_LIB_DIR/fleet-reload-inputs-probe.sh"\n\n' + anchor_source, 1)
text = text.replace(anchor_surface,
                    anchor_surface + '    "$FLEET_LIB_DIR/fleet-reload-inputs-probe.sh"\n', 1)
path.write_text(text)
PY
then
    ok "T11a: staged a dispatcher that sources a new, broken file"
else
    bad "T11a: staged a dispatcher that sources a new, broken file"
fi

if wait_for "$DLOG" "reload refused: the image on disk does not boot" "$RELOAD_WAIT_LIMIT"; then
    ok "T11b: the broken new source is refused"
else
    bad "T11b: the broken new source is refused"
    echo "        log (tail):"; tail -15 "$DLOG" | sed 's/^/          | /'
fi
if kill -0 "$DISP_PID" 2>/dev/null && (( $(started_pids "$DLOG" | wc -l) == 2 )); then
    ok "T11c: the refusal left the loaded image running"
else
    bad "T11c: the refusal left the loaded image running"
    echo "        log (tail):"; tail -15 "$DLOG" | sed 's/^/          | /'
fi

# Repair ONLY the new file. The running image does not hash it, so its surface
# hash stands still and only the re-probe can notice.
printf 'fleet_reload_inputs_probe() { :; }\n' >"$NEW_LIB"

if wait_for_boots "$DLOG" 3 "$RELOAD_WAIT_LIMIT"; then
    ok "T12a: repairing only the new file reloaded the dispatcher"
else
    bad "T12a: repairing only the new file reloaded the dispatcher"
    echo "        log (tail):"; tail -15 "$DLOG" | sed 's/^/          | /'
fi
assert_eq "$(started_pids "$DLOG" | sort -u | wc -l | tr -d ' ')" "1" \
    "T12b: the reload kept the pid"
new_surface=$(env -i "${LAUNCH_ENV[@]}" "$BASH" "$STAGE/fleet-dispatcher" --print-surface)
assert_contains "$new_surface" "/fleet-reload-inputs-probe.sh" \
    "T12c: the image on disk lists the new source"
assert_eq "$(started_revs "$DLOG" | tail -1)" \
    "$(printf '%s\n' "$new_surface" | awk -F'\t' '/^aggregate/{print substr($2, 1, 12)}')" \
    "T12d: the reloaded image runs the revision that includes it"

summarize "dispatcher reload inputs"
