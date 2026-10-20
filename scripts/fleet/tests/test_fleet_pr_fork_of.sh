#!/usr/bin/env bash
# fleet-pr fork-of: the merger's fork verdict and merged-parent boundary over a
# fixture remote, with a gh stub that derives commit-to-PR association from the
# fixture's own git history.

set -euo pipefail

SCRIPT_DIR=$(cd "$(dirname "$0")/.." && pwd)
source "$(dirname "$0")/lib_preflight.sh"
source "$(dirname "$0")/lib_assert.sh"
source "$(dirname "$0")/lib_hermetic.sh"

SUBJECT="$SCRIPT_DIR/fleet-pr"
if [[ ! -f "$SUBJECT" ]]; then
    echo "SKIP: subject not found at $SUBJECT" >&2
    exit 3
fi

TMPROOT=$(mktemp -d "${TMPDIR:-/tmp}/forkof.XXXXXX")
cleanup() { [[ -d "${TMPROOT:-}" ]] && rm -rf "$TMPROOT"; return 0; }
trap cleanup EXIT
hermetic_poison_gh_env "$TMPROOT"

export HOME="$TMPROOT/home"
export FLEET_ENGINE_ROOT="$TMPROOT/src/IrredenEngine"
mkdir -p "$HOME/.fleet/state" "$TMPROOT/bin"
printf '{"engine":"acme/widgets","game":"acme/gadgets"}\n' > "$HOME/.fleet/state/repos.json"

export GIT_AUTHOR_NAME=test GIT_AUTHOR_EMAIL=test@test
export GIT_COMMITTER_NAME=test GIT_COMMITTER_EMAIL=test@test

REMOTE="$TMPROOT/remote.git"
AUTH="$TMPROOT/auth"
git init -q --bare -b master "$REMOTE"
git clone -q "$REMOTE" "$AUTH" 2>/dev/null
git -C "$AUTH" config commit.gpgsign false

commit() {
    printf '%s\n' "$1" > "$AUTH/$1.txt"
    git -C "$AUTH" add "$1.txt"
    git -C "$AUTH" commit -q -m "$1"
    git -C "$AUTH" rev-parse HEAD
}
branch() { git -C "$AUTH" checkout -q -b "$1" "$2"; }
push() { git -C "$AUTH" push -q origin "$@" 2>/dev/null; }

commit base >/dev/null
push master

# (a) a real fork: feat-a is cut from open PR 11's current head.
branch feat-x master
X1=$(commit x1)
branch feat-a feat-x
commit a1 >/dev/null

# (b) the incident: feat-b is cut from merged parent 21's INTERMEDIATE head;
# (b') feat-bp carries its final head. 21 then squash-merges.
branch parent-p master
commit p1 >/dev/null
P2=$(commit p2)
branch feat-b parent-p
commit b1 >/dev/null
commit b2 >/dev/null
git -C "$AUTH" checkout -q parent-p
P3=$(commit p3)
branch feat-bp parent-p
commit bp1 >/dev/null
push feat-x feat-a parent-p feat-b feat-bp

git -C "$AUTH" checkout -q master
MASTER_AT_MERGE=$(git -C "$AUTH" rev-parse HEAD)
git -C "$AUTH" merge -q --squash parent-p >/dev/null
git -C "$AUTH" commit -q -m "squash parent-p"

# (b) also has an unrelated open PR beside it, like the incident.
branch feat-u master
commit u1 >/dev/null
# (c) no relation.
branch feat-c master
commit c1 >/dev/null
# (d) a native stack: grandparent 41 <- parent 42 <- child 43.
branch stack-g master
commit g1 >/dev/null
branch stack-p stack-g
commit sp1 >/dev/null
branch stack-c stack-p
commit sc1 >/dev/null
# A branch one commit longer than the association-lookup cap.
branch long master
LONG=$(git -C "$AUTH" rev-parse HEAD)
TREE=$(git -C "$AUTH" rev-parse 'HEAD^{tree}')
for i in $(seq 101); do LONG=$(git -C "$AUTH" commit-tree "$TREE" -p "$LONG" -m "long $i"); done
git -C "$AUTH" update-ref refs/heads/long "$LONG"
push master feat-u feat-c stack-g stack-p stack-c long

sha() { git -C "$AUTH" rev-parse "$1"; }

# PR records plus the commit set GitHub associates with each: an open PR's
# current base..head, a merged PR's base-at-merge..final-head.
python3 - "$TMPROOT/prs.json" "$REMOTE" <<PY
import json, subprocess, sys
def pr(n, head, base, merged=None, base_tip=None, sha=None):
    return {"number": n, "head": head, "sha": sha, "base": base,
            "merged_at": merged, "base_tip": base_tip or base}
prs = [
    pr(11, "feat-x", "master", sha="$(sha feat-x)"),
    pr(12, "feat-a", "master", sha="$(sha feat-a)"),
    # Same head as 12 under another base: an identical head is not an upstream.
    pr(13, "feat-a", "feat-x", sha="$(sha feat-a)"),
    pr(21, "parent-p", "master", merged="2026-01-01T00:00:00Z",
       base_tip="$MASTER_AT_MERGE", sha="$P3"),
    pr(22, "feat-b", "master", sha="$(sha feat-b)"),
    pr(23, "feat-u", "master", sha="$(sha feat-u)"),
    pr(24, "feat-bp", "master", sha="$(sha feat-bp)"),
    pr(31, "feat-c", "master", sha="$(sha feat-c)"),
    pr(41, "stack-g", "master", sha="$(sha stack-g)"),
    pr(42, "stack-p", "stack-g", sha="$(sha stack-p)"),
    pr(43, "stack-c", "stack-p", sha="$(sha stack-c)"),
    pr(51, "long", "master", sha="$LONG"),
]
for p in prs:
    p["commits"] = subprocess.run(
        ["git", "-C", sys.argv[2], "rev-list", p["sha"], "^" + p["base_tip"]],
        capture_output=True, text=True, check=True).stdout.split()
json.dump(prs, open(sys.argv[1], "w"))
PY

cat > "$TMPROOT/bin/gh" <<'GHSTUB'
#!/usr/bin/env python3
import json
import os
import re
import sys

args = sys.argv[1:]
prs = json.load(open(os.environ["STUB_PRS"]))
slug = "acme/widgets"


def fail(msg, rc=2):
    sys.stderr.write(f"stub gh: {msg}: {' '.join(args)}\n")
    sys.exit(rc)


def rest(p):
    return {"number": p["number"], "state": "closed" if p["merged_at"] else "open",
            "merged_at": p["merged_at"],
            "head": {"ref": p["head"], "sha": p["sha"]}, "base": {"ref": p["base"]}}


if args[:1] != ["api"]:
    fail("unmodeled subcommand")
path, flags = args[1], args[2:]
if path == f"repos/{slug}/pulls?state=open&per_page=100" and flags == ["--paginate"]:
    if os.environ.get("STUB_FAIL") == "list":
        fail("HTTP 502", 1)
    print(json.dumps([rest(p) for p in prs if not p["merged_at"]]))
    sys.exit(0)
m = re.fullmatch(rf"repos/{slug}/commits/([0-9a-f]{{40}})/pulls\?per_page=100", path)
if m and not flags:
    if os.environ.get("STUB_FAIL") == "association":
        fail("HTTP 502", 1)
    with open(os.environ["STUB_LOG"], "a") as log:
        log.write(m.group(1) + "\n")
    print(json.dumps([rest(p) for p in prs if m.group(1) in p["commits"]]))
    sys.exit(0)
fail("unmodeled api call")
GHSTUB
chmod +x "$TMPROOT/bin/gh"
printf '@python3 "%%~dp0gh" %%*\r\n' > "$TMPROOT/bin/gh.bat"
export PATH="$TMPROOT/bin:$PATH"
export STUB_PRS="$TMPROOT/prs.json" STUB_LOG="$TMPROOT/stub.log"

CLONE="$FLEET_ENGINE_ROOT"
git clone -q "$REMOTE" "$CLONE" 2>/dev/null

# run_fork_of <clone> <N> — sets OUT, ERR, RC.
run_fork_of() {
    local errf="$TMPROOT/err"
    RC=0
    OUT=$(cd "$1" && python3 "$SUBJECT" fork-of "$2" --repo engine 2>"$errf") || RC=$?
    ERR=$(cat "$errf")
}

expect() {
    local label="$1" n="$2" want="$3"
    run_fork_of "$CLONE" "$n"
    assert_eq "$RC" 0 "$label: exit 0"
    assert_eq "$OUT" "$want" "$label: two-line verdict"
}

echo "T1: (a) a real fork names the open upstream; an identical head is skipped"
expect "(a)" 12 "fork-of: 11 feat-x $X1
merged-parent: none"

echo "T2: (b) incident shape — merged parent's intermediate head is the boundary"
: > "$STUB_LOG"
expect "(b)" 22 "fork-of: none
merged-parent: 21 $P2"
assert_eq "$(wc -l < "$STUB_LOG" | tr -d ' ')" 3 "(b): walk stops at the boundary (b2, b1, p2)"

echo "T3: (b') branch carries the merged parent's exact final head"
expect "(b')" 24 "fork-of: none
merged-parent: 21 $P3"

echo "T4: (c) no relation"
expect "(c)" 31 "fork-of: none
merged-parent: none"

echo "T5: (d) native-stack child is not a fork of its own stack ancestors"
expect "(d)" 43 "fork-of: none
merged-parent: none"
expect "(d) parent" 42 "fork-of: none
merged-parent: none"

echo "T6: (e) association endpoint failure fails closed"
STUB_FAIL=association run_fork_of "$CLONE" 22
assert_eq "$([[ $RC -ne 0 ]] && echo nonzero)" nonzero "(e): non-zero exit"
assert_eq "$OUT" "" "(e): empty stdout"
assert_contains "$ERR" "PR association" "(e): reason on stderr"
STUB_FAIL=list run_fork_of "$CLONE" 22
assert_eq "$RC:$OUT" "1:" "(e) open-list failure: exit 1, empty stdout"

echo "T7: (f) a PR that is not open fails"
run_fork_of "$CLONE" 21
assert_eq "$RC:$OUT" "1:" "(f): exit 1, empty stdout"
assert_contains "$ERR" "not an open PR" "(f): reason on stderr"

echo "T8: (g) a clone of another origin with same-named branches fails"
OTHER_REMOTE="$TMPROOT/other.git"
OTHER_AUTH="$TMPROOT/other-auth"
git init -q --bare -b master "$OTHER_REMOTE"
git init -q -b master "$OTHER_AUTH"
git -C "$OTHER_AUTH" config commit.gpgsign false
printf 'unrelated\n' > "$OTHER_AUTH/u.txt"
git -C "$OTHER_AUTH" add u.txt
git -C "$OTHER_AUTH" commit -q -m unrelated
git -C "$OTHER_AUTH" branch feat-c
git -C "$OTHER_AUTH" push -q "$OTHER_REMOTE" master feat-c 2>/dev/null
OTHER_CLONE="$TMPROOT/other-clone"
git clone -q "$OTHER_REMOTE" "$OTHER_CLONE" 2>/dev/null
run_fork_of "$OTHER_CLONE" 31
assert_eq "$RC:$OUT" "1:" "(g): exit 1, empty stdout"
assert_contains "$ERR" "wrong clone" "(g): reason on stderr"

echo "T9: a walk past the association-lookup cap fails, never a truncated none"
: > "$STUB_LOG"
run_fork_of "$CLONE" 51
assert_eq "$RC:$OUT" "1:" "cap: exit 1, empty stdout"
assert_contains "$ERR" "cap" "cap: reason on stderr"
assert_eq "$(wc -l < "$STUB_LOG" | tr -d ' ')" 100 "cap: exactly the cap's lookups ran"

echo "T10: an unknown argv shape fails closed in the stub"
STUB_RC=0
gh api repos/acme/widgets/pulls >/dev/null 2>&1 || STUB_RC=$?
assert_eq "$STUB_RC" 2 "stub rejects an unmodeled call"

summarize
