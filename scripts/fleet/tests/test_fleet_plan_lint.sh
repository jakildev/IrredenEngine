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
set -e

echo "================================"
echo "  PASS: $PASS    FAIL: $FAIL"
echo "================================"
[[ "$FAIL" -eq 0 ]]
