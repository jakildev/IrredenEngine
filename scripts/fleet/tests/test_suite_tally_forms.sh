#!/usr/bin/env bash
# Ratchet: every bash suite under tests/ prints a tally fleet-positive-control
# can read, and the set of suites allowed to do it the legacy way only shrinks
# (#2917).
#
# Why this exists
# ---------------
# fleet-positive-control reads a suite's own summary line for the counts. 41 of
# 92 suites kept private PASS/FAIL counters and printed them in a spelling the
# matcher never accepted, so the tool exited 2 as a *setup failure* and no
# positive control was obtainable for any of them — through the one path
# scripts/fleet/CLAUDE.md mandates ("never by hand"). Widening the grammar fixed
# the population that existed; this suite is what stops a sixth bespoke form
# from silently reopening it. The failure is caught at the suite that introduces
# it, not at the next control that mysteriously cannot run.
#
# The rule
# --------
# A NEW bash suite ends with `summarize` from tests/lib_assert.sh. The names in
# OWN_TALLY_BASELINE are the suites that predate that rule: an entry may be
# REMOVED when its suite migrates to summarize, never added. A baseline that
# only grows is not a ratchet.
#
# The three checks
# ----------------
#   1. every on-disk test_*.sh that does not call `summarize` is named in the
#      baseline — a new own-tally suite is flagged
#   2. every baseline entry exists on disk and still keeps its own tally (a
#      renamed, deleted, or migrated suite prunes its entry, so the list stays
#      honest and shrinks as suites move over), and its tally emission renders
#      to a form `fleet-positive-control --parse-tally` accepts
#   3. no test_*.sh defines its own `summarize()` — a local redefinition
#      printing a bespoke form would bypass both the grammar and check 1
#
# Check 2 calls the wrapper rather than re-stating the grammar, so there is one
# executor and nothing to drift from (`.claude/rules/cpp-globals.md`: a detection
# spec nothing runs drifts silently).
#
# The renderer is deliberately narrow
# -----------------------------------
# `echo "<form>"` (or '<form>') with $PASS/${PASS} and $FAIL/${FAIL} on ONE line
# is the entire accepted emission shape — all 47 baseline suites today. A printf,
# a split echo, or a computed format string is flagged by design: the fix for a
# flagged suite is `summarize`, not a patch to this renderer.
#
# Purely local: no network, no ~/.fleet, no gh.

set -euo pipefail

SCRIPT_DIR=$(cd "$(dirname "$0")/.." && pwd)
TESTS_DIR="$SCRIPT_DIR/tests"

# Sourced BEFORE the first line that builds a path to a fleet-* wrapper, so the
# mis-staged-control preflight it carries gets to fire first.
# shellcheck source=lib_assert.sh
source "$(dirname "$0")/lib_assert.sh"

WRAPPER="$SCRIPT_DIR/fleet-positive-control"
# The subject, not an environment dependency — exit 3 so run_all.sh tallies this
# as skipped rather than folding a zero-assertion run into "passed" (#2786).
if [[ ! -f "$WRAPPER" ]]; then
    echo "SKIP: fleet-positive-control not found at $WRAPPER" >&2
    exit 3
fi

TMPROOT=$(mktemp -d)
cleanup() { [[ -n "${TMPROOT:-}" && -d "$TMPROOT" ]] && rm -rf "$TMPROOT"; return 0; }
trap cleanup EXIT

# --- the baseline -----------------------------------------------------------
# Re-derive with:
#   grep -L -E '^[[:space:]]*summarize\b' scripts/fleet/tests/test_*.sh
# 45 entries at the commit that introduced this suite. Shrink only.
OWN_TALLY_BASELINE=(
    test_babysit_effort.sh
    test_classify_auto_rereview.sh
    test_clone_freshness.sh
    test_derive_host.sh
    test_dispatcher_claimable_cap.sh
    test_dispatcher_concurrency_cap.sh
    test_dispatcher_effort.sh
    test_dispatcher_empty_exit_backoff.sh
    test_dispatcher_github_gate.sh
    test_dispatcher_stagger.sh
    test_dispatcher_usage_gate.sh
    test_fleet_claim_acquire.sh
    test_fleet_claim_amending_sweep.sh
    test_fleet_claim_blockers.sh
    test_fleet_claim_model_gate.sh
    test_fleet_claim_orphan_sweep.sh
    test_fleet_claim_parked_release.sh
    test_fleet_claim_planning.sh
    test_fleet_claim_prlabel_orphan_sweep.sh
    test_fleet_claim_reconcile.sh
    test_fleet_claim_reconcile_c2.sh
    test_fleet_claim_reconcile_heal_design_unblock.sh
    test_fleet_claim_stackable_live_resolve.sh
    test_fleet_claim_steward.sh
    test_fleet_claim_symlink_lib_dir.sh
    test_fleet_net_guard.sh
    test_fleet_plan_lint.sh
    test_fleet_pr_claim_feedback.sh
    test_fleet_queue_ingest_blocked_by.sh
    test_fleet_queue_ingest_gated.sh
    test_fleet_queue_ingest_human_owned.sh
    test_fleet_queue_ingest_late_no_plan.sh
    test_fleet_queue_ingest_plan_gate.sh
    test_fleet_queue_ingest_review_plan.sh
    test_fleet_queue_ingest_revise_plan.sh
    test_fleet_rebase_hung_lock.sh
    test_fleet_rebase_plan_automerge.sh
    test_fleet_review_verdict.sh
    test_fleet_transition.sh
    test_fleet_up_conf_bootstrap.sh
    test_install_refresh.sh
    test_ir_build_dir_resolution.sh
    test_reconcile_amendments.sh
    test_run_targets_path_scope.sh
    test_worktree_settings_hooks.sh
)

# --- the checks -------------------------------------------------------------

# The last `echo` line naming both counters, or nothing. Two greps rather than
# one alternation: the line must carry BOTH, and an ERE cannot say "and".
find_tally_echo() {
    grep -E '^[[:space:]]*echo[[:space:]]' "$1" \
        | grep -E '\$\{?PASS\}?' \
        | grep -E '\$\{?FAIL\}?' \
        | tail -1 || true
}

# Render an `echo "<form>"` line with PASS=7, FAIL=3. Returns 1 when the line is
# not the narrow shape above — a flag, not a renderer bug.
render_tally_echo() {
    local body="$1"
    body="${body#"${body%%[![:space:]]*}"}"          # leading indent
    [[ "$body" == echo[[:space:]]* ]] || return 1
    body="${body#echo}"
    body="${body#"${body%%[![:space:]]*}"}"          # space after `echo`
    if   [[ "$body" == \"*\" ]]; then body="${body:1:${#body}-2}"
    elif [[ "$body" == \'*\' ]]; then body="${body:1:${#body}-2}"
    else return 1
    fi
    body="${body//\$\{PASS\}/7}"; body="${body//\$PASS/7}"
    body="${body//\$\{FAIL\}/3}"; body="${body//\$FAIL/3}"
    # Anything still unexpanded is a computed form the grammar cannot pin.
    [[ "$body" != *'$'* ]] || return 1
    printf '%s\n' "$body"
}

# Emits one finding per line on stdout; returns 0 when clean, 1 when not.
# Takes the tests dir and the baseline so the fixture controls below can drive
# it against a synthetic population.
check_tally_forms() {
    local tests_dir="$1"; shift
    local -a baseline=("$@")
    local findings=0 f name line rendered got
    local probe="$TMPROOT/rendered.txt"

    for f in "$tests_dir"/test_*.sh; do
        [[ -e "$f" ]] || continue
        name=$(basename "$f")
        # check 3 — a local summarize() would satisfy check 1 while printing
        # anything it likes.
        if grep -qE '^[[:space:]]*summarize[[:space:]]*\(\)' "$f"; then
            echo "$name: defines its own summarize() — use tests/lib_assert.sh's"
            findings=$((findings + 1))
            continue
        fi
        grep -qE '^[[:space:]]*summarize\b' "$f" && continue
        # check 1 — own-tally, so it must be a known legacy suite.
        if ! printf '%s\n' "${baseline[@]+"${baseline[@]}"}" | grep -qxF "$name"; then
            echo "$name: keeps its own tally but is not in OWN_TALLY_BASELINE — end it with \`summarize\` (tests/lib_assert.sh)"
            findings=$((findings + 1))
        fi
    done

    for name in "${baseline[@]+"${baseline[@]}"}"; do
        f="$tests_dir/$name"
        # check 2a — the list stays honest about what exists.
        if [[ ! -f "$f" ]]; then
            echo "$name: named in OWN_TALLY_BASELINE but not on disk — drop the entry"
            findings=$((findings + 1))
            continue
        fi
        # A migrated suite is not a violation; it is the entry that is stale.
        if grep -qE '^[[:space:]]*summarize\b' "$f"; then
            echo "$name: now calls summarize — drop it from OWN_TALLY_BASELINE (the baseline shrinks)"
            findings=$((findings + 1))
            continue
        fi
        # check 2b — the spelling itself, read through the one executor.
        line=$(find_tally_echo "$f")
        if [[ -z "$line" ]]; then
            echo "$name: no single-line \`echo\` naming both \$PASS and \$FAIL — the tally emission is un-pinnable"
            findings=$((findings + 1))
            continue
        fi
        if ! rendered=$(render_tally_echo "$line"); then
            echo "$name: tally emission is not \`echo \"<form>\"\` with both counters — $(echo "$line" | sed 's/^[[:space:]]*//')"
            findings=$((findings + 1))
            continue
        fi
        printf '%s\n' "$rendered" > "$probe"
        if ! got=$("$WRAPPER" --parse-tally "$probe" 2>/dev/null); then
            echo "$name: tally form is not in fleet-positive-control's grammar — $rendered"
            findings=$((findings + 1))
            continue
        fi
        if [[ "$got" != "7 3" ]]; then
            echo "$name: tally form parses to '$got', not '7 3' — $rendered"
            findings=$((findings + 1))
        fi
    done

    return $(( findings > 0 ))
}

# --- fixture controls -------------------------------------------------------
# The ratchet needs its own positive control, or it is a check that has never
# been shown to fire (#2876).
FIX="$TMPROOT/fixtures"
mkdir -p "$FIX"

write_fixture() { printf '%s\n' "$2" > "$FIX/$1"; }

echo "--- a clean population reports nothing ---"
write_fixture test_a_summarize.sh '#!/usr/bin/env bash
source lib_assert.sh
ok "x"
summarize'
write_fixture test_b_legacy.sh '#!/usr/bin/env bash
PASS=1; FAIL=0
echo "PASS: $PASS  FAIL: $FAIL"'
OUT=$(check_tally_forms "$FIX" test_b_legacy.sh) && RC=0 || RC=$?
assert_eq "$RC" "0" "T1: a summarize caller + a baselined legacy suite pass"
assert_eq "$OUT" "" "T1: a clean population emits no findings"

echo "--- an unbaselined own-tally suite is flagged (check 1) ---"
write_fixture test_c_new_owntally.sh '#!/usr/bin/env bash
PASS=1; FAIL=0
echo "PASS: $PASS  FAIL: $FAIL"'
OUT=$(check_tally_forms "$FIX" test_b_legacy.sh) && RC=0 || RC=$?
assert_eq "$RC" "1" "T2: a new own-tally suite fails the ratchet"
assert_contains "$OUT" "test_c_new_owntally.sh: keeps its own tally" "T2: the finding names the offending suite"
assert_contains "$OUT" "summarize" "T2: the finding names the fix"
rm -f "$FIX/test_c_new_owntally.sh"

echo "--- a baselined suite whose spelling is outside the grammar is flagged (check 2) ---"
write_fixture test_d_bespoke.sh '#!/usr/bin/env bash
PASS=1; FAIL=0
echo "Passed=$PASS Failed=$FAIL"'
OUT=$(check_tally_forms "$FIX" test_b_legacy.sh test_d_bespoke.sh) && RC=0 || RC=$?
assert_eq "$RC" "1" "T3: a bespoke spelling fails the ratchet even when baselined"
assert_contains "$OUT" "test_d_bespoke.sh: tally form is not in fleet-positive-control's grammar" "T3: the finding names the grammar as the authority"
assert_contains "$OUT" "Passed=7 Failed=3" "T3: the finding quotes the rendered form"
rm -f "$FIX/test_d_bespoke.sh"

echo "--- a baselined suite with no single-line echo is flagged (check 2) ---"
write_fixture test_e_printf.sh '#!/usr/bin/env bash
PASS=1; FAIL=0
printf "PASS: %d  FAIL: %d\n" "$PASS" "$FAIL"'
OUT=$(check_tally_forms "$FIX" test_b_legacy.sh test_e_printf.sh) && RC=0 || RC=$?
assert_eq "$RC" "1" "T4: a printf tally is flagged, by design"
assert_contains "$OUT" "test_e_printf.sh: no single-line" "T4: the finding names the un-pinnable emission"
rm -f "$FIX/test_e_printf.sh"

echo "--- a baseline entry with no file is flagged (check 2) ---"
OUT=$(check_tally_forms "$FIX" test_b_legacy.sh test_f_deleted.sh) && RC=0 || RC=$?
assert_eq "$RC" "1" "T5: a baseline entry naming no file fails the ratchet"
assert_contains "$OUT" "test_f_deleted.sh: named in OWN_TALLY_BASELINE but not on disk" "T5: the finding says to drop the entry"

echo "--- a baselined suite that has migrated is flagged (the baseline shrinks) ---"
write_fixture test_g_migrated.sh '#!/usr/bin/env bash
source lib_assert.sh
ok "x"
summarize'
OUT=$(check_tally_forms "$FIX" test_b_legacy.sh test_g_migrated.sh) && RC=0 || RC=$?
assert_eq "$RC" "1" "T6: a migrated suite still in the baseline fails the ratchet"
assert_contains "$OUT" "test_g_migrated.sh: now calls summarize" "T6: the finding says to drop the entry"
rm -f "$FIX/test_g_migrated.sh"

echo "--- a local summarize() definition is flagged (check 3) ---"
# Composed through a variable: check 3 scans every test_*.sh under tests/,
# including this one, so a literal line-anchored `summarize()` in the fixture
# text would make this suite flag itself.
SUMM=summarize
write_fixture test_h_local_summarize.sh "#!/usr/bin/env bash
PASS=1; FAIL=0
$SUMM() { echo \"totally fine: \$PASS/\$FAIL\"; }
$SUMM"
OUT=$(check_tally_forms "$FIX" test_b_legacy.sh) && RC=0 || RC=$?
assert_eq "$RC" "1" "T7: a suite defining its own summarize() fails the ratchet"
assert_contains "$OUT" "test_h_local_summarize.sh: defines its own summarize()" "T7: the finding names the shadowing"
rm -f "$FIX/test_h_local_summarize.sh"

echo "--- every accepted legacy spelling renders and parses ---"
# Guards the renderer's quote-stripping and ${PASS} spelling against the forms
# actually present in the tree, so a renderer regression is caught here rather
# than as a mass check-2 failure on the real population.
write_fixture test_i_indent.sh '#!/usr/bin/env bash
PASS=1; FAIL=0
  echo "  PASS: $PASS    FAIL: $FAIL"'
write_fixture test_j_braces.sh '#!/usr/bin/env bash
PASS=1; FAIL=0
echo "PASS=${PASS} FAIL=${FAIL}"'
write_fixture test_k_lower.sh '#!/usr/bin/env bash
PASS=1; FAIL=0
echo "pass: $PASS  fail: $FAIL"'
write_fixture test_l_labelled.sh '#!/usr/bin/env bash
PASS=1; FAIL=0
echo "some suite: $PASS passed, $FAIL failed"'
OUT=$(check_tally_forms "$FIX" test_b_legacy.sh test_i_indent.sh test_j_braces.sh test_k_lower.sh test_l_labelled.sh) && RC=0 || RC=$?
assert_eq "$RC" "0" "T8: indented, \${braced}, lowercase and labelled forms all pass"
assert_eq "$OUT" "" "T8: none of the accepted spellings produce a finding"

# --- the real population ----------------------------------------------------
echo "--- the tree's own suites ---"
if [[ ! -d "$TESTS_DIR" ]]; then
    echo "SKIP: tests dir not found at $TESTS_DIR" >&2
    exit 3
fi
OUT=$(check_tally_forms "$TESTS_DIR" "${OWN_TALLY_BASELINE[@]}") && RC=0 || RC=$?
if [[ "$RC" -ne 0 ]]; then
    printf '%s\n' "$OUT" | sed 's/^/    /'
fi
assert_eq "$RC" "0" "T9: every test_*.sh under tests/ prints a tally the wrapper can read"

summarize "suite tally forms"
