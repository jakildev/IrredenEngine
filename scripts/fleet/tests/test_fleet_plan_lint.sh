#!/usr/bin/env bash
# Tests for fleet-plan-lint — the deterministic structural lint of a `## Plan`
# comment (the cheap first half of plan-review). fleet-plan-lint shells out to
# `gh` from a python subprocess, so gh is mocked via a PATH-shim fake (a bash
# gh() function would not be seen by the subprocess).
#
# Pins the hard-fail vs warn-vs-pass contract:
#   exit 0 = structure sound (warnings allowed)   exit 1 = hard fail (bounce)

set -euo pipefail

SCRIPT_DIR=$(cd "$(dirname "$0")/.." && pwd)
LINT="$SCRIPT_DIR/fleet-plan-lint"
[[ -x "$LINT" ]] || { echo "test setup: fleet-plan-lint not executable at $LINT" >&2; exit 1; }

PASS=0; FAIL=0
TMPROOT=$(mktemp -d)
cleanup() { [[ -n "$TMPROOT" && -d "$TMPROOT" ]] && rm -rf "$TMPROOT"; }
trap cleanup EXIT
ok()  { PASS=$((PASS + 1)); echo "  ok: $1"; }
bad() { FAIL=$((FAIL + 1)); echo "  FAIL: $1"; }
assert_exit() {
    local actual="$1" expected="$2" msg="$3"
    if [[ "$actual" -eq "$expected" ]]; then ok "$msg"; else bad "$msg (expected $expected, got $actual)"; fi
}

# Canned fixtures: one ## Plan comment per issue number, keyed in the fake gh.
GOOD_PLAN='## Plan: good

- **Model:** opus

### Scope
do the thing

### Approach
verified current state via grep; one approach: edit foo.cpp then bar.cpp

### Affected files
- foo.cpp

### Acceptance criteria
builds + tests

### Gotchas
none'

# PATH-shim fake gh: emits {"title":...,"comments":[{"body":...}]} per issue.
# The fixture is read from $GOOD_PLAN at runtime (quoted heredoc — no expansion).
export GOOD_PLAN
mkdir -p "$TMPROOT/bin"
cat > "$TMPROOT/bin/gh" <<'PYEOF'
#!/usr/bin/env python3
import json, os, sys
args = sys.argv[1:]
num = None
for i, a in enumerate(args):
    if a == "view" and i + 1 < len(args):
        num = args[i + 1]
GOOD = os.environ["GOOD_PLAN"]
DEFER = GOOD.replace("one approach: edit foo.cpp then bar.cpp",
                     "decide during implementation whether to edit foo or bar")
SPIKE = GOOD.replace("one approach: edit foo.cpp then bar.cpp",
                     "investigation spike - decide during investigation")
# #2401: a mechanism-lever premise (cost/path-dominance claim) with no
# measurement citation should warn; the same plan citing a disarm probe should not.
LEVER = GOOD.replace("one approach: edit foo.cpp then bar.cpp",
                     "the cost is dominated by the resolve loop; one approach: edit foo.cpp")
LEVER_CITED = LEVER.replace("dominated by the resolve loop",
                            "dominated by the resolve loop, confirmed by a disarm probe")
# #2443: synonym-headed plan (the #2442-shaped false-positive regression) — core
# sections worded naturally instead of leading with the literal token. Must pass.
SYNONYM = '''## Plan: synonym headings

- **Model:** sonnet

### Files / modules
foo.cpp, bar.cpp

### Committed approach - one approach, picked
verified current state via grep; edit foo.cpp then bar.cpp

### Acceptance tests (positive-fire)
builds + tests

### Gotchas
none'''
# #2443 negative control: a real Approach section but no Scope-concept and no
# Acceptance-concept heading anywhere -- must still hard-fail (missing_core >= 2).
NO_SCOPE_NO_ACCEPTANCE = '''## Plan: negative control

- **Model:** sonnet

### Approach
verified current state via grep; one approach: edit foo.cpp

### Notes
none'''
# #2824: imperative-mood fork — a fork phrased as an instruction to the
# implementer, matching none of the self-describing DEFER phrases. Pinned
# verbatim from issue #2820's Gotchas section (the live instance that slipped
# past the pre-#2824 matcher and had to be caught by the Opus plan-review pass
# instead) — do not re-fetch #2820 live, it has since been replanned.
FORK_WHETHER = GOOD.replace(
    "### Gotchas\nnone",
    "### Gotchas\n"
    "- **`fleet:needs-gl-host` is valid on PRs too** (`fleet-claim` amending-claim\n"
    "  path, #2524). Check whether the narrowed predicate should apply there as well\n"
    "  or only to issue claims; decide explicitly rather than by omission.")
# Negative fixture (acceptance criteria #3): a plain confirmation ("check
# whether the build is green") names no alternative ("or Y") and must not fire.
FORK_WHETHER_NEGATIVE = GOOD.replace(
    "one approach: edit foo.cpp then bar.cpp",
    "check whether the build is green; one approach: edit foo.cpp then bar.cpp")
# investigation-spike downgrade must still apply to the new fork form (acceptance
# criteria #4).
FORK_WHETHER_SPIKE = FORK_WHETHER.replace(
    "## Plan: good", "## Plan: investigation spike - fork")
# either/or fork named in Approach, with a hedging modal ("could") signaling an
# undecided choice -- must hard-fail.
EITHER_OR_MODAL = GOOD.replace(
    "one approach: edit foo.cpp then bar.cpp",
    "the fix could either rewrite foo.cpp or patch bar.cpp instead")
# either/or describing already-settled, declarative branches (no hedging modal)
# -- corpus-measured false-positive shape (.fleet/plans/issue-2197.md,
# issue-2540.md both hit a naive either/or check with no modal present) --
# must NOT fire.
EITHER_OR_DECLARATIVE = GOOD.replace(
    "one approach: edit foo.cpp then bar.cpp",
    "one approach: edit foo.cpp then bar.cpp. Either the reader retries or the "
    "writer backs off, per the existing protocol")
# either/or + modal outside Approach/Gotchas (in Acceptance) -- scoped out,
# must NOT fire.
EITHER_OR_ACCEPTANCE = GOOD.replace(
    "### Acceptance criteria\nbuilds + tests",
    "### Acceptance criteria\ntest could pass either the fast path or the slow path assertion")
# The imperative-mood fork is scoped to Approach/Gotchas for the same reason,
# pinned by the two false-positive shapes an unscoped scan hit:
#   (1) a QA-style acceptance criterion naming a pass/fail alternative, the
#       direct mirror of EITHER_OR_ACCEPTANCE above;
FORK_WHETHER_ACCEPTANCE = GOOD.replace(
    "### Acceptance criteria\nbuilds + tests",
    "### Acceptance criteria\n"
    "1. Check whether the fix resolves the crash or introduces a new regression.\n"
    "2. Build is green.")
#   (2) a settled decision tree stated outside Approach/Gotchas, pinned verbatim
#       from `.fleet/plans/issue-1596.md`'s "Architect decision" section -- the
#       "or" is a parenthetical sub-clause of the thing being checked and BOTH
#       branches are already decided, so it is declarative, not a live fork.
#       This is the corpus regression the pre-scoping matcher drifted PASS->FAIL
#       on. It is also what makes the scoping an ALLOWLIST (fire only in
#       Approach/Gotchas) rather than an Acceptance-only exclusion, which would
#       leave this shape firing.
FORK_WHETHER_OTHER_SECTION = GOOD.replace(
    "### Gotchas\nnone",
    "### Gotchas\nnone\n\n"
    "### Architect decision\n"
    "FIRST check whether a main-layout texture containing detached caster depth\n"
    "already exists at BAKE time (or can be cheaply made available there). If yes,\n"
    "bake that -- zero new resolve passes. If not, add ONE dedicated resolve.")
# In-scope positive for the OTHER arm of the scope predicate: the #2820 fixture
# above sits in Gotchas, so without this one the "approach" keyword arm ships
# unexercised and a future narrowing of the keyword set goes uncaught.
FORK_WHETHER_APPROACH = GOOD.replace(
    "one approach: edit foo.cpp then bar.cpp",
    "check whether the predicate should apply to foo.cpp as well or only to bar.cpp")
# #2443 plan-exclusion guard: the mandatory "## Plan: <title>" heading is the
# ONLY heading here that could match Approach -- Scope + Acceptance concepts are
# present, no real Approach-shaped heading. "plan" is deliberately NOT an
# Approach synonym, so Approach must report missing (single missing core -> warn,
# exit 0), never vacuously match. Guards a future synonym-set edit that re-adds
# "plan" to Approach (which would silently satisfy it for every plan comment).
PLAN_ONLY_NO_APPROACH = '''## Plan: plan-heading only

- **Model:** sonnet

### Scope
verified current state via grep; foo.cpp

### Affected files
foo.cpp

### Acceptance criteria
builds + tests

### Gotchas
none'''
# #2707 fixtures: a "## Plan review" verdict comment shares the same
# startswith("## Plan") prefix as the plan itself but has no core sections,
# so a naive plans[-1] selection would hard-fail a plan that already PASSed.
REVIEW_PASS = "## Plan review — #111 (opus-reviewer)\n\nfleet-plan-lint PASS. Looks sound."
REVIEW_BOUNCE = "## Plan review — not sound, back to `fleet:needs-plan`\n\nMissing acceptance criteria."
SKELETAL_REPLAN_SEED = "## Plan: skeletal re-plan seed\n\nwe should do it somehow"
REVIEW_ONLY = "## Plan review — #113 (opus-reviewer)\n\nfleet-plan-lint PASS. Looks sound."
F = {
  "100": {"title": "sound task", "comments": [{"body": GOOD}]},
  "101": {"title": "defer task", "comments": [{"body": DEFER}]},
  "102": {"title": "no plan", "comments": [{"body": "just a normal comment, no plan here"}]},
  "103": {"title": "skeletal", "comments": [{"body": "## Plan: skeletal\n\nwe should do it somehow"}]},
  "104": {"title": "investigation spike for X", "comments": [{"body": SPIKE}]},
  "105": {"title": "tbd task", "comments": [{"body": GOOD + "\n\nopen question: TBD"}]},
  "106": {"title": "lever task", "comments": [{"body": LEVER}]},
  "107": {"title": "lever cited task", "comments": [{"body": LEVER_CITED}]},
  "108": {"title": "synonym headings task", "comments": [{"body": SYNONYM}]},
  "109": {"title": "negative control task", "comments": [{"body": NO_SCOPE_NO_ACCEPTANCE}]},
  "110": {"title": "plan-exclusion guard task", "comments": [{"body": PLAN_ONLY_NO_APPROACH}]},
  "111": {"title": "reviewed plan task", "comments": [{"body": GOOD}, {"body": REVIEW_PASS}]},
  "112": {"title": "re-planned after bounce task", "comments": [
      {"body": SKELETAL_REPLAN_SEED}, {"body": REVIEW_BOUNCE}, {"body": GOOD}]},
  "113": {"title": "review only, no plan task", "comments": [{"body": REVIEW_ONLY}]},
  "114": {"title": "imperative-mood fork task", "comments": [{"body": FORK_WHETHER}]},
  "115": {"title": "whether-no-or negative control task", "comments": [{"body": FORK_WHETHER_NEGATIVE}]},
  "116": {"title": "investigation spike for fork", "comments": [{"body": FORK_WHETHER_SPIKE}]},
  "117": {"title": "either/or modal task", "comments": [{"body": EITHER_OR_MODAL}]},
  "118": {"title": "either/or declarative task", "comments": [{"body": EITHER_OR_DECLARATIVE}]},
  "119": {"title": "either/or in acceptance task", "comments": [{"body": EITHER_OR_ACCEPTANCE}]},
  "120": {"title": "fork-whether in acceptance task", "comments": [{"body": FORK_WHETHER_ACCEPTANCE}]},
  "121": {"title": "fork-whether outside approach/gotchas task", "comments": [{"body": FORK_WHETHER_OTHER_SECTION}]},
  "122": {"title": "fork-whether in approach task", "comments": [{"body": FORK_WHETHER_APPROACH}]},
}
print(json.dumps(F.get(num, {"title": "missing", "comments": []})))
PYEOF
chmod +x "$TMPROOT/bin/gh"
export PATH="$TMPROOT/bin:$PATH"

echo "fleet-plan-lint tests"

set +e
"$LINT" 100 >/dev/null 2>&1; assert_exit $? 0 "sound plan -> exit 0"
"$LINT" 101 >/dev/null 2>&1; assert_exit $? 1 "deferred-approach phrase -> hard fail"
"$LINT" 102 >/dev/null 2>&1; assert_exit $? 1 "no ## Plan comment -> hard fail"
"$LINT" 103 >/dev/null 2>&1; assert_exit $? 1 "skeletal (missing core sections) -> hard fail"
"$LINT" 104 >/dev/null 2>&1; assert_exit $? 0 "investigation spike: defer phrase downgraded to warn -> exit 0"
"$LINT" 105 >/dev/null 2>&1; assert_exit $? 1 "TBD in plan -> hard fail"
pass_out=$("$LINT" 100 2>&1 || true)
case "$pass_out" in *"PASS #100"*) ok "sound plan prints PASS line";; *) bad "sound PASS line missing: [$pass_out]";; esac
defer_out=$("$LINT" 101 2>&1 || true)
case "$defer_out" in *deferred-approach*) ok "defer fail names the phrase";; *) bad "defer phrase not named: [$defer_out]";; esac
# #2401 — mechanism-lever premise without a measurement citation: warn fires, exit still 0.
"$LINT" 106 >/dev/null 2>&1; assert_exit $? 0 "mechanism-lever w/o citation -> exit 0 (warn only)"
lever_out=$("$LINT" 106 2>&1 || true)
case "$lever_out" in *"mechanism-lever language"*) ok "mechanism-lever warn fires";; *) bad "mechanism-lever warn missing: [$lever_out]";; esac
# Same plan citing a disarm probe -> warn suppressed.
"$LINT" 107 >/dev/null 2>&1; assert_exit $? 0 "mechanism-lever w/ citation -> exit 0"
cited_out=$("$LINT" 107 2>&1 || true)
case "$cited_out" in *"mechanism-lever language"*) bad "mechanism-lever warn should be absent when premise cited: [$cited_out]";; *) ok "mechanism-lever warn absent when premise cited";; esac
"$LINT" --repo bogus 100 >/dev/null 2>&1; assert_exit $? 2 "bad --repo -> usage exit 2"
# #2443 — concept-based core-section matching: synonym-worded headings pass...
"$LINT" 108 >/dev/null 2>&1; assert_exit $? 0 "synonym-headed plan (#2442-shaped) -> exit 0 (no longer a false positive)"
# ...but a plan genuinely missing two core concepts still hard-fails.
"$LINT" 109 >/dev/null 2>&1; assert_exit $? 1 "missing scope + acceptance concepts -> hard fail (negative control)"
# #2443 plan-exclusion guard — the mandatory "## Plan:" heading must NOT
# vacuously satisfy Approach (that is why "plan" is excluded from its synonym
# set). Scope + Acceptance present, no Approach-shaped heading -> single missing
# core -> warn (exit 0) that names Approach. If a future edit re-adds "plan" to
# the Approach synonyms, that heading would match, missing_core would go empty,
# and the warn below would vanish -> this test fails.
"$LINT" 110 >/dev/null 2>&1; assert_exit $? 0 "plan-only (no Approach heading) -> exit 0 (single missing core = warn)"
plan_excl_out=$("$LINT" 110 2>&1 || true)
case "$plan_excl_out" in *"core section absent"*"Approach"*) ok "Approach reported missing (## Plan: heading does not vacuously satisfy it)";; *) bad "Approach not reported missing — did 'plan' leak into the Approach synonym set? [$plan_excl_out]";; esac
# #2707 — a "## Plan review" comment must never shadow the plan it reviews.
"$LINT" 111 >/dev/null 2>&1; assert_exit $? 0 "reviewed plan (plan + review comment) -> exit 0 (review not selected)"
review_out=$("$LINT" 111 2>&1 || true)
case "$review_out" in *"missing core sections"*) bad "reviewed plan hard-failed — the review comment shadowed the plan: [$review_out]";; *) ok "reviewed plan does not hard-fail on the review's shape";; esac
# A re-plan posted after a bounce review must still select the NEWEST ## Plan
# comment, skipping over the review in between (acceptance criteria #2).
"$LINT" 112 >/dev/null 2>&1; assert_exit $? 0 "re-plan after bounce review -> exit 0 (newest plan selected, not the skeletal seed or the review)"
# Only a review comment, no ## Plan at all -> must report "no comment found",
# never lint the review's own (non-plan-shaped) body (acceptance criteria #3).
"$LINT" 113 >/dev/null 2>&1; assert_exit $? 1 "review present, no plan -> hard fail"
review_only_out=$("$LINT" 113 2>&1 || true)
case "$review_only_out" in *"no \`## Plan\` comment found"*) ok "review-only reports 'no ## Plan comment found', does not lint the review";; *) bad "review-only did not report the expected message: [$review_only_out]";; esac

# #2824 — imperative-mood fork ("check whether X ... or Y" in one sentence),
# pinned verbatim from the live #2820 instance that slipped past the
# pre-#2824 matcher (acceptance criteria #1).
"$LINT" 114 >/dev/null 2>&1; assert_exit $? 1 "imperative-mood fork (#2820-shaped) -> hard fail"
fork_out=$("$LINT" 114 2>&1 || true)
case "$fork_out" in *"imperative-mood fork"*) ok "imperative-mood fork names itself in the failure";; *) bad "imperative-mood fork message missing: [$fork_out]";; esac
# Negative fixture (acceptance criteria #3): a plain confirmation with no
# named alternative ("or Y") must not fire.
"$LINT" 115 >/dev/null 2>&1; assert_exit $? 0 "'check whether the build is green' (no or-alternative) -> exit 0"
neg_out=$("$LINT" 115 2>&1 || true)
case "$neg_out" in *"imperative-mood fork"*) bad "whether-no-or negative control false-fired: [$neg_out]";; *) ok "whether-no-or negative control does not fire";; esac
# investigation-spike downgrade still applies to the new fork form (acceptance
# criteria #4).
"$LINT" 116 >/dev/null 2>&1; assert_exit $? 0 "investigation spike: imperative-mood fork downgraded to warn -> exit 0"
spike_fork_out=$("$LINT" 116 2>&1 || true)
case "$spike_fork_out" in warn*"imperative-mood fork"*"investigation spike"*) ok "spike fork downgraded to warn, not silently dropped";; *) bad "spike fork warn missing or malformed: [$spike_fork_out]";; esac
# either/or fork named in Approach with a hedging modal -> hard fail.
"$LINT" 117 >/dev/null 2>&1; assert_exit $? 1 "either/or fork w/ modal in Approach -> hard fail"
either_out=$("$LINT" 117 2>&1 || true)
case "$either_out" in *"either/or fork"*) ok "either/or fork names itself in the failure";; *) bad "either/or fork message missing: [$either_out]";; esac
# either/or describing already-settled, declarative branches (no hedging
# modal) -- corpus-measured false-positive shape (issue-2197.md, issue-2540.md
# both hit a naive either/or check with no modal present) -- must not fire.
"$LINT" 118 >/dev/null 2>&1; assert_exit $? 0 "either/or declarative (no modal) -> exit 0 (corpus false-positive shape)"
either_decl_out=$("$LINT" 118 2>&1 || true)
case "$either_decl_out" in *"either/or fork"*) bad "either/or declarative false-fired: [$either_decl_out]";; *) ok "either/or declarative does not fire";; esac
# either/or + modal outside Approach/Gotchas (in Acceptance) -- scoped out.
"$LINT" 119 >/dev/null 2>&1; assert_exit $? 0 "either/or + modal in Acceptance -> exit 0 (scoped to Approach/Gotchas only)"
either_acc_out=$("$LINT" 119 2>&1 || true)
case "$either_acc_out" in *"either/or fork"*) bad "either/or in Acceptance false-fired: [$either_acc_out]";; *) ok "either/or in Acceptance correctly out of scope";; esac
# The imperative-mood fork carries the same Approach/Gotchas scoping. Both
# negatives below hard-failed before the scoping (measured), so each is a live
# regression pin, not a restatement of the check's shape.
"$LINT" 120 >/dev/null 2>&1; assert_exit $? 0 "imperative-mood fork in Acceptance -> exit 0 (scoped out; mirror of #119)"
fork_acc_out=$("$LINT" 120 2>&1 || true)
case "$fork_acc_out" in *"imperative-mood fork"*) bad "imperative-mood fork in Acceptance false-fired: [$fork_acc_out]";; *) ok "imperative-mood fork in Acceptance correctly out of scope";; esac
# Corpus regression: the settled decision tree in issue-1596.md's "Architect
# decision" section is neither Approach/Gotchas nor Acceptance, so only an
# allowlist scoping clears it.
"$LINT" 121 >/dev/null 2>&1; assert_exit $? 0 "imperative-mood fork outside Approach/Gotchas (issue-1596 corpus shape) -> exit 0"
fork_other_out=$("$LINT" 121 2>&1 || true)
case "$fork_other_out" in *"imperative-mood fork"*) bad "imperative-mood fork outside Approach/Gotchas false-fired (corpus regression): [$fork_other_out]";; *) ok "imperative-mood fork outside Approach/Gotchas correctly out of scope";; esac
# ...but the scoping must not go so narrow it stops firing where forks belong:
# in-scope via the "approach" keyword arm (the #114 fixture covers "gotcha").
"$LINT" 122 >/dev/null 2>&1; assert_exit $? 1 "imperative-mood fork in Approach -> hard fail (scope predicate's approach arm)"
fork_appr_out=$("$LINT" 122 2>&1 || true)
case "$fork_appr_out" in *"imperative-mood fork"*) ok "imperative-mood fork still fires in Approach after scoping";; *) bad "imperative-mood fork stopped firing in Approach — scoping too narrow: [$fork_appr_out]";; esac
set -e

echo "================================"
echo "  PASS: $PASS    FAIL: $FAIL"
echo "================================"
[[ "$FAIL" -eq 0 ]]
