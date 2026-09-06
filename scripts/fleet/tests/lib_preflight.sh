# Mis-staged-control preflight for the bash tests in this directory. Sourced,
# never executed — the name deliberately avoids the test_*.sh pattern so
# anything that globs for tests skips it:
#
#     source "$(dirname "$0")/lib_preflight.sh"
#
# Source it immediately after the SCRIPT_DIR assignment, before the first line
# that builds a path to a fleet-* wrapper. It holds ONLY the preflight — no
# counters, no assert_* family — so a suite with its own inline ok()/bad() can
# adopt it without inheriting helpers it did not ask for. lib_assert.sh sources
# this file too, so a suite that uses both ends up sourcing it twice; that is
# harmless (a function redefinition plus a second cheap directory check) and
# deliberate, since the two sources happen at different points in the file.
#
# Firing on source rather than on an explicit call is the point: an opt-in call
# is one a new suite can forget, and a guard scoped to whichever file it happens
# to live in reaches only that file's users rather than everything exposed to
# the hazard. tests/test_positive_control.sh ratchets the adoption — a suite
# that resolves a fleet-* wrapper with no preflight source line above it fails
# there — so the reach is enforced rather than remembered (#2845).

# require_fleet_lib_dir <dir> — abort the suite when <dir> is a partially
# staged scripts/fleet/ tree, instead of letting it score as assertion
# failures.
#
# The shell wrappers dispatch to the fleet_*.py modules beside them, so a
# stage holding only the script under test plus this file aborts every
# invocation on its own lib-dir preflight with rc=1 and empty stdout. Absent
# this guard the suite records those as ordinary expected/actual mismatches and
# prints a normal-looking tally: a mis-staged control reported 2 passed / 21
# failed where the truth was 14 / 9, with nothing to signal it was bogus
# (#2713). An inflated count overstates the fix's coverage, which is the one
# claim a positive control exists to make trustworthy.
#
# The discriminator is ref-agnostic on purpose: a real scripts/fleet/ always
# carries both halves, so fleet-* wrappers present with zero fleet_*.py
# modules beside them means the stage is partial. Anything else — a dir that
# isn't a fleet script dir at all, or a complete tree — returns 0.
require_fleet_lib_dir() {
    local dir="${1:-}"
    [[ -n "$dir" && -d "$dir" ]] || return 0
    compgen -G "$dir/fleet-*" >/dev/null 2>&1 || return 0
    compgen -G "$dir/fleet_*.py" >/dev/null 2>&1 && return 0

    echo "test setup: incomplete fleet script tree at $dir" >&2
    echo "            fleet-* wrappers are present but no fleet_*.py modules are," >&2
    echo "            so every invocation would abort on its lib-dir preflight and" >&2
    echo "            score as assertion failures. Stage the whole directory:" >&2
    echo "              git archive <ref> scripts/fleet | tar -x -C <tmpdir>" >&2
    echo "            or just use: fleet-positive-control <test-file> <ref>" >&2
    exit 2
}

# Self-location, not SCRIPT_DIR: this file's own parent's parent IS the staged
# scripts/fleet dir in every layout, including the mis-stage repro. Deriving the
# directory to check from ${BASH_SOURCE[0]} rather than from whatever the suite
# happened to assign is what makes the ordering bug unrepresentable — a suite
# that sources this before setting SCRIPT_DIR, or that never sets it, is still
# covered. It also covers the suites that point SCRIPT_DIR at the tests dir or
# the repo root: a SCRIPT_DIR-derived check finds no fleet-* glob there and
# returns 0 without having validated anything.
_ir_preflight_self=$(cd "$(dirname "${BASH_SOURCE[0]:-$0}")/.." 2>/dev/null && pwd) || _ir_preflight_self=""
require_fleet_lib_dir "$_ir_preflight_self"
unset _ir_preflight_self

# Belt for the suite that points SCRIPT_DIR at a tree other than its own
# location — a hand-driven control that sources the repo's helpers while
# running against a staged copy (tests/test_positive_control.sh drives exactly
# this shape).
if [[ -n "${SCRIPT_DIR:-}" ]]; then
    require_fleet_lib_dir "$SCRIPT_DIR"
fi
