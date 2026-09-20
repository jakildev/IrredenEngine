#!/usr/bin/env bash
# Tests for fleet-up's self re-exec across the main-clone advance:
#
#   fleet_up_reexec_if_stale — once restore_main_clone_to_master
#   fast-forwards the clone that ~/bin/fleet-up resolves into, the running
#   bash is still executing the pre-advance bytes, so a block merged since
#   the last boot never runs. The function hashes the script and every helper
#   it sourced before the advance (fleet_surface_hash) and re-execs the
#   merged script exactly once when the hash moved, under the launch
#   environment (knobs the first pass exported must not come back as
#   operator overrides). An up-to-date clone re-execs zero times; a change
#   seen on the second pass is reported, never looped on.
#
# The function is sed-extracted from fleet-up into a fixture script that
# mirrors fleet-up's own head (sentinel capture, launch-env capture, loaded
# list, surface hash), sources the real fleet-common.sh and
# fleet-clone-freshness.sh, and runs the real restore_main_clone_to_master
# against a throwaway clone of a bare origin. The fixture is invoked through
# a bin/ symlink, the way the installed fleet-up is.

set -euo pipefail

SCRIPT_DIR=$(cd "$(dirname "$0")/.." && pwd)
source "$(dirname "$0")/lib_preflight.sh"
FLEET_UP="$SCRIPT_DIR/fleet-up"
FRESHNESS_SH="$SCRIPT_DIR/fleet-clone-freshness.sh"
COMMON_SH="$SCRIPT_DIR/fleet-common.sh"
[[ -x "$FLEET_UP" ]] || { echo "test setup: fleet-up not found at $FLEET_UP" >&2; exit 1; }
[[ -f "$FRESHNESS_SH" ]] || { echo "test setup: fleet-clone-freshness.sh not found at $FRESHNESS_SH" >&2; exit 1; }
[[ -f "$COMMON_SH" ]] || { echo "test setup: fleet-common.sh not found at $COMMON_SH" >&2; exit 1; }

# shellcheck source=/dev/null
source "$SCRIPT_DIR/tests/lib_assert.sh"

TMPROOT=""
cleanup() { [[ -n "$TMPROOT" && -d "$TMPROOT" ]] && rm -rf "$TMPROOT"; }
trap cleanup EXIT
TMPROOT=$(mktemp -d "${TMPDIR:-/tmp}/fleet-up-reexec.XXXXXX")
export HOME="$TMPROOT/home"
export FLEET_STATE_DIR="$TMPROOT/state"
export FLEET_ALERTS_DIR="$TMPROOT/alerts"
mkdir -p "$HOME" "$FLEET_STATE_DIR" "$FLEET_ALERTS_DIR"

extract_fn() {  # extract_fn <name> — print the function body from fleet-up
    sed -n "/^$1() {/,/^}/p" "$FLEET_UP"
}
FNS="$(extract_fn fleet_up_reexec_if_stale)"
[[ "$FNS" == *"fleet_up_reexec_if_stale() {"* ]] \
    || { echo "test setup: fleet_up_reexec_if_stale not extracted from fleet-up" >&2; exit 1; }

# --- git fixtures ---------------------------------------------------------
ORIGIN="$TMPROOT/origin.git"
SEED="$TMPROOT/seed"
CLONE="$TMPROOT/clone"
BIN="$TMPROOT/bin"
git init --quiet --bare "$ORIGIN"
git clone --quiet "$ORIGIN" "$SEED" 2>/dev/null
mkdir -p "$SEED/scripts/fleet" "$BIN"

# write_fixture <helper-tag> [marker-line] — the fixture fleet-up + the helper
# it sources, into the seed checkout. The script head mirrors fleet-up's own.
write_fixture() {
    local helper_tag="$1" marker="${2:-}"
    {
        cat <<'HEAD'
#!/usr/bin/env bash
set -euo pipefail
_FLEET_UP_REEXECED="${FLEET_UP_REEXEC:-}"
unset FLEET_UP_REEXEC
_FLEET_UP_LAUNCH_ENV=()
for _launch_name in $(compgen -e); do
    if [[ -n "${!_launch_name+x}" ]]; then
        _FLEET_UP_LAUNCH_ENV+=("$_launch_name=${!_launch_name}")
    fi
done
unset _launch_name
knob_at_entry="${FLEET_UP_TEST_KNOB:-unset}"
export FLEET_UP_TEST_KNOB=resolved
ENGINE="$FLEET_TEST_ENGINE"
_FLEET_UP_LOADED=("${BASH_SOURCE[0]}")
_h="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/fleet-fixture-helper.sh"
source "$_h"
_FLEET_UP_LOADED+=("$_h")
source "$FLEET_TEST_COMMON_SH"
source "$FLEET_TEST_FRESHNESS_SH"
HEAD
        printf '%s\n' "$FNS"
        cat <<'BODY'
_FLEET_UP_SURFACE_HASH="$(fleet_surface_hash "${_FLEET_UP_LOADED[@]}")" || _FLEET_UP_SURFACE_HASH=""
echo "banner reexeced=${_FLEET_UP_REEXECED:-0} knob=$knob_at_entry caller=${FLEET_UP_TEST_CALLER:-unset} helper=$(fixture_helper) sentinel=${FLEET_UP_REEXEC:-unset}"
restore_main_clone_to_master "$ENGINE"
fleet_up_reexec_if_stale "$_FLEET_UP_SURFACE_HASH" "${BASH_SOURCE[0]}" "$@"
echo "post-advance args=$*"
BODY
        [[ -n "$marker" ]] && printf '%s\n' "$marker"
    } > "$SEED/scripts/fleet/fleet-up"
    chmod +x "$SEED/scripts/fleet/fleet-up"
    printf 'fixture_helper() { echo %s; }\n' "$helper_tag" > "$SEED/scripts/fleet/fleet-fixture-helper.sh"
}
push_fixture() {  # push_fixture <message>
    (cd "$SEED" \
        && git add -A \
        && git -c user.email=t@t -c user.name=t commit -m "$1" --quiet \
        && git branch -M master \
        && git push --quiet origin master 2>/dev/null)
}

write_fixture helper-v1
push_fixture v1
git clone --quiet "$ORIGIN" "$CLONE" 2>/dev/null
ln -s "$CLONE/scripts/fleet/fleet-up" "$BIN/fleet-up"
ln -s "$CLONE/scripts/fleet/fleet-fixture-helper.sh" "$BIN/fleet-fixture-helper.sh"

export FLEET_TEST_ENGINE="$CLONE"
export FLEET_TEST_COMMON_SH="$COMMON_SH"
export FLEET_TEST_FRESHNESS_SH="$FRESHNESS_SH"
export FLEET_UP_TEST_CALLER=keep
unset FLEET_UP_TEST_KNOB FLEET_UP_REEXEC

run_fixture() {  # run_fixture [arg...] -> combined stdout+stderr, via the symlink
    "$BIN/fleet-up" "$@" 2>&1 || echo "fixture exit=$?"
}
count_lines() { printf '%s\n' "$1" | grep -cF -- "$2" || true; }
clone_head() { git -C "$CLONE" rev-parse HEAD; }
origin_head() { git -C "$CLONE" rev-parse origin/master; }

echo "T1: a fleet-up merged since the last boot re-execs once and its new block runs"
write_fixture helper-v2 'echo MARKER'
push_fixture v2
out=$(run_fixture alpha beta)
assert_absent "$out" "fixture exit=" "fixture exits 0"
assert_eq "$(count_lines "$out" "re-running the merged script")" 1 "re-exec fired once"
assert_eq "$(count_lines "$out" "MARKER")" 1 "merged block ran exactly once"
assert_eq "$(count_lines "$out" "banner ")" 2 "head ran twice (pre-advance pass + merged pass)"
assert_contains "$out" "banner reexeced=0 knob=unset caller=keep helper=helper-v1 sentinel=unset" "first pass loaded the pre-advance helper"
assert_contains "$out" "banner reexeced=1 knob=unset caller=keep helper=helper-v2 sentinel=unset" \
    "merged pass: caller env kept, first-pass export dropped, sentinel consumed, helper refreshed"
assert_eq "$(count_lines "$out" "post-advance args=alpha beta")" 1 "arguments carried through the re-exec"
assert_eq "$(clone_head)" "$(origin_head)" "clone advanced to origin/master"

echo "T2: an up-to-date clone re-execs zero times"
out=$(run_fixture alpha)
assert_absent "$out" "fixture exit=" "fixture exits 0"
assert_absent "$out" "re-running the merged script" "no re-exec"
assert_eq "$(count_lines "$out" "banner ")" 1 "head ran once"
assert_contains "$out" "banner reexeced=0 knob=unset caller=keep helper=helper-v2 sentinel=unset" "single pass, current helper"
assert_eq "$(count_lines "$out" "MARKER")" 1 "current block ran once"

echo "T3: a helper that changed under a still-identical fleet-up also re-execs"
write_fixture helper-v3 'echo MARKER'
push_fixture v3
out=$(run_fixture)
assert_absent "$out" "fixture exit=" "fixture exits 0"
assert_eq "$(count_lines "$out" "re-running the merged script")" 1 "re-exec fired once"
assert_contains "$out" "helper=helper-v2 " "first pass ran the stale helper"
assert_contains "$out" "banner reexeced=1 knob=unset caller=keep helper=helper-v3 sentinel=unset" "merged pass runs the fresh helper"
assert_eq "$(count_lines "$out" "MARKER")" 1 "block ran exactly once"

echo "T4: a change seen on the second pass is reported, never re-exec'd again"
write_fixture helper-v4 'echo MARKER'
push_fixture v4
out=$(FLEET_UP_REEXEC=1 run_fixture)
assert_absent "$out" "fixture exit=" "fixture exits 0"
assert_absent "$out" "re-running the merged script" "no second re-exec"
assert_contains "$out" "changed again after the re-exec" "second-pass change reported"
assert_eq "$(count_lines "$out" "banner ")" 1 "head ran once"
assert_contains "$out" "banner reexeced=1 knob=unset caller=keep helper=helper-v3 sentinel=unset" "sentinel consumed, pass continues on the loaded copy"
assert_eq "$(count_lines "$out" "post-advance args=")" 1 "pre-advance script ran to completion"
assert_eq "$(clone_head)" "$(origin_head)" "clone still advanced"

summarize "fleet-up self re-exec across the main-clone advance"
