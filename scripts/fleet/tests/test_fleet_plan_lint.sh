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
F = {
  "100": {"title": "sound task", "comments": [{"body": GOOD}]},
  "101": {"title": "defer task", "comments": [{"body": DEFER}]},
  "102": {"title": "no plan", "comments": [{"body": "just a normal comment, no plan here"}]},
  "103": {"title": "skeletal", "comments": [{"body": "## Plan: skeletal\n\nwe should do it somehow"}]},
  "104": {"title": "investigation spike for X", "comments": [{"body": SPIKE}]},
  "105": {"title": "tbd task", "comments": [{"body": GOOD + "\n\nopen question: TBD"}]},
  "106": {"title": "lever task", "comments": [{"body": LEVER}]},
  "107": {"title": "lever cited task", "comments": [{"body": LEVER_CITED}]},
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
set -e

echo "================================"
echo "  PASS: $PASS    FAIL: $FAIL"
echo "================================"
[[ "$FAIL" -eq 0 ]]
