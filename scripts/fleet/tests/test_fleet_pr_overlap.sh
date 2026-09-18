#!/usr/bin/env bash
# Tests for fleet-pr-overlap (the pre-publication open-PR overlap check).
#
# Hermetic: every scenario runs inside a sandbox clone whose `origin` is a
# bare repository in $TMP, with `refs/pull/<N>/head` refs pushed by hand and
# the origin URL rewritten through `url.<path>.insteadOf` so the slug parses
# as a GitHub repository. `gh` is a PATH stub that models `pr list` and
# `api graphql` argument parsing, honours --limit the way the binary does
# (newest 30 by number when absent), serves successive `pr list` calls from
# numbered fixtures through a call counter, and exits 99 on anything else.
#
# Covers:
#   1  two shared files whose only conflicting regions are adjacent lines and
#      a same-line edit: exactly 2 rows, both conflicts, overlap, exit 1
#   2  a clean intersection on a block tree: VERDICT block, exit 3
#   3  overlap without conflict (unchanged line between, and identical edits)
#      is a clean row, overlap, exit 1 — never 0
#   4  an unresolvable head and unrelated histories both land on exit 2
#   5  enumeration: a 34-row fixture whose only overlap is the oldest row;
#      --limit hit; totalCount disagreement; a truncated `files` row widened
#      from git; the stub's own fidelity
#   6  self-identification by branch + cross-repo flag; same-head competitor
#   7  stacks: upstream by ancestry, stale base, accidental fork, grandparent,
#      inherited-branch competitor, stale local default ref
#   8  --repo slug mismatch fails closed
#   9  snapshot coherence: head moved, population moved, identical proceeds
#   10 a stacked sibling's own delta excludes paths inherited from its base;
#      the sibling's own edit to the same block-tree path still blocks
#   11 a competitor whose base is gone or unrelated drops a path whose blob
#      already matches the caller's base tip, and keeps one that differs
#   12 the missing-subject guard skips with exit 3 and no tally

set -uo pipefail

SCRIPT_DIR=$(cd "$(dirname "$0")/.." && pwd)
source "$(dirname "$0")/lib_preflight.sh"
SUBJECT="$SCRIPT_DIR/fleet-pr-overlap"
if [[ ! -x "$SUBJECT" ]]; then
    echo "SKIP: subject under test missing at $SUBJECT" >&2
    exit 3
fi
source "$(dirname "$0")/lib_assert.sh"

TMP=$(mktemp -d "${TMPDIR:-/tmp}/test-pr-overlap.XXXXXX")
TMP=$(cd "$TMP" && pwd)
trap 'rm -rf "$TMP"' EXIT

ORIGIN="$TMP/origin.git"
WORK="$TMP/work"
SLUG="example/sandbox"
GH_STUB_DIR="$TMP/gh-fixtures"
mkdir -p "$GH_STUB_DIR" "$TMP/bin"

# --- gh stub (fails closed) -------------------------------------------------

cat > "$TMP/bin/gh" << 'EOF'
#!/usr/bin/env python3
import json
import os
import sys

FIX = os.environ["GH_STUB_DIR"]
SLUG = os.environ["GH_STUB_SLUG"]
DEFAULT_LIMIT = 30

# Transcribed from `gh pr list --help` / `gh api --help`: flag -> takes a value.
PR_LIST_FLAGS = {
    "--app": True, "-a": True, "--assignee": True, "-A": True, "--author": True,
    "-B": True, "--base": True, "-d": False, "--draft": False, "-H": True, "--head": True,
    "-q": True, "--jq": True, "--json": True, "-l": True, "--label": True,
    "-L": True, "--limit": True, "-S": True, "--search": True, "-s": True, "--state": True,
    "-t": True, "--template": True, "-w": False, "--web": False, "-R": True, "--repo": True,
}
API_FLAGS = {
    "--cache": True, "-F": True, "--field": True, "-H": True, "--header": True,
    "--hostname": True, "-i": False, "--include": False, "--input": True, "-q": True,
    "--jq": True, "-X": True, "--method": True, "--paginate": False, "-p": True,
    "--preview": True, "-f": True, "--raw-field": True, "--silent": False,
    "--slurp": False, "-t": True, "--template": True, "--verbose": False,
}
PR_LIST_JSON_FIELDS = set(
    "additions assignees author autoMergeRequest baseRefName baseRefOid body changedFiles "
    "closed closedAt closingIssuesReferences comments commits createdAt deletions files "
    "fullDatabaseId headRefName headRefOid headRepository headRepositoryOwner id "
    "isCrossRepository isDraft labels latestReviews maintainerCanModify mergeCommit "
    "mergeStateStatus mergeable mergedAt mergedBy milestone number potentialMergeCommit "
    "projectCards projectItems reactionGroups reviewDecision reviewRequests reviews state "
    "statusCheckRollup title updatedAt url".split()
)


def parse(argv, spec, command):
    opts = {}
    positional = []
    i = 0
    while i < len(argv):
        arg = argv[i]
        if arg.startswith("-"):
            name, eq, value = arg.partition("=")
            if name not in spec:
                print(f"unknown flag: {name}\n\nUsage:  gh {command} [flags]", file=sys.stderr)
                sys.exit(1)
            if spec[name]:
                if not eq:
                    i += 1
                    if i >= len(argv):
                        print(f"flag needs an argument: {name}", file=sys.stderr)
                        sys.exit(1)
                    value = argv[i]
                opts.setdefault(name, []).append(value)
            else:
                opts[name] = [True]
        else:
            positional.append(arg)
        i += 1
    return opts, positional


def bump_counter():
    path = os.path.join(FIX, "calls")
    count = int(open(path).read()) if os.path.exists(path) else 0
    count += 1
    with open(path, "w") as handle:
        handle.write(str(count))
    return count


def fixture_rows(call):
    numbered = os.path.join(FIX, f"prs.{call}.json")
    path = numbered if os.path.exists(numbered) else os.path.join(FIX, "prs.json")
    with open(path) as handle:
        return json.load(handle)


def pr_list(argv):
    opts, positional = parse(argv, PR_LIST_FLAGS, "pr list")
    if positional:
        print(f"gh stub: pr list takes no positional argument: {positional}", file=sys.stderr)
        sys.exit(99)
    repo = (opts.get("--repo") or opts.get("-R") or [None])[-1]
    if repo != SLUG:
        print(f"gh stub: unexpected repo {repo!r}", file=sys.stderr)
        sys.exit(99)
    for fields in opts.get("--json", []):
        for field in fields.split(","):
            if field not in PR_LIST_JSON_FIELDS:
                print(f'Unknown JSON field: "{field}"', file=sys.stderr)
                sys.exit(1)
    limit = int((opts.get("--limit") or opts.get("-L") or [DEFAULT_LIMIT])[-1])
    if limit < 1:
        print("invalid limit: " + str(limit), file=sys.stderr)
        sys.exit(1)
    rows = sorted(fixture_rows(bump_counter()), key=lambda row: -row["number"])
    json.dump(rows[:limit], sys.stdout)


def api(argv):
    if not argv or argv[0] != "graphql":
        print(f"gh stub: unexpected invocation: api {argv}", file=sys.stderr)
        sys.exit(99)
    opts, positional = parse(argv[1:], API_FLAGS, "api")
    if positional:
        print(f"gh stub: api graphql takes no positional argument: {positional}", file=sys.stderr)
        sys.exit(99)
    raw = opts.get("-f", []) + opts.get("--raw-field", [])
    query = [value for value in raw if value.startswith("query=")]
    if len(query) != 1 or "totalCount" not in query[0]:
        print(f"gh stub: unmodelled graphql query: {raw}", file=sys.stderr)
        sys.exit(99)
    override = os.path.join(FIX, "totalcount")
    calls = os.path.join(FIX, "calls")
    call = int(open(calls).read()) if os.path.exists(calls) else 1
    total = int(open(override).read()) if os.path.exists(override) else len(fixture_rows(call))
    json.dump({"data": {"repository": {"pullRequests": {"totalCount": total}}}}, sys.stdout)


argv = sys.argv[1:]
if argv[:2] == ["pr", "list"]:
    pr_list(argv[2:])
elif argv[:1] == ["api"]:
    api(argv[1:])
else:
    print(f"gh stub: unexpected invocation: {' '.join(argv)}", file=sys.stderr)
    sys.exit(99)
EOF
chmod +x "$TMP/bin/gh"

# --- sandbox helpers --------------------------------------------------------

g() { git -C "$WORK" "$@"; }
oid() { g rev-parse "$1"; }

# Replace line <n> of <file> (under $WORK) with <text>.
edit_line() {
    local file="$WORK/$1" n="$2" text="$3"
    awk -v n="$n" -v t="$text" 'NR == n { print t; next } { print }' "$file" > "$file.tmp"
    mv "$file.tmp" "$file"
}

write_file() { mkdir -p "$(dirname "$WORK/$1")"; printf '%s\n' "$2" > "$WORK/$1"; }
numbered_file() { seq 1 30 | sed "s/^/$1 line /"; }

branch_from() { g checkout -q -B "$1" "$2"; }
commit_all() { g add -A; g commit -q -m "$1"; }

# Push <branch> to origin as itself and as refs/pull/<N>/head.
publish_pr() {
    local n="$1" branch="$2"
    g push -q origin "refs/heads/$branch:refs/heads/$branch" "refs/heads/$branch:refs/pull/$n/head"
}

# pr_row <N> <headRefName> <baseRefName> <isCrossRepository> <changedFiles|-> <path>...
# headRefOid comes from origin's refs/pull/<N>/head unless PR_ROW_OID is set.
pr_row() {
    local n="$1" head="$2" base="$3" cross="$4" changed="$5"
    shift 5
    local head_oid="${PR_ROW_OID:-$(git -C "$ORIGIN" rev-parse "refs/pull/$n/head")}"
    [[ "$changed" == "-" ]] && changed=$#
    local files="" path
    for path in "$@"; do
        files+="${files:+,}{\"path\":\"$path\"}"
    done
    printf '{"number":%s,"title":"pr %s","headRefName":"%s","headRefOid":"%s","baseRefName":"%s","isCrossRepository":%s,"changedFiles":%s,"files":[%s]}' \
        "$n" "$n" "$head" "$head_oid" "$base" "$cross" "$changed" "$files"
}

# pr_json <file> <row>... — writes a JSON array of the given rows.
pr_json() {
    local file="$1"
    shift
    local body="" row
    for row in "$@"; do
        body+="${body:+,}$row"
    done
    printf '[%s]\n' "$body" > "$file"
}

reset_stub() {
    rm -f "$GH_STUB_DIR"/prs*.json "$GH_STUB_DIR/calls" "$GH_STUB_DIR/totalcount"
}

# run_tool [args] — runs the subject inside the sandbox clone; OUT/ERR/RC.
run_tool() {
    OUT=$(cd "$WORK" && PATH="$TMP/bin:$PATH" GH_STUB_DIR="$GH_STUB_DIR" GH_STUB_SLUG="$SLUG" \
        "$SUBJECT" "$@" 2> "$TMP/err.txt")
    RC=$?
    ERR=$(cat "$TMP/err.txt")
}

assert_rc() { assert_eq "$RC" "$1" "$2 (rc)"; }
verdict_line() { printf '%s\n' "$OUT" | tail -n 1; }
row_count() { printf '%s\n' "$OUT" | grep -c '^#' || true; }

# --- sandbox -----------------------------------------------------------------

git init -q --bare -b master "$ORIGIN"
git clone -q "$ORIGIN" "$WORK" 2>/dev/null
g config user.email sandbox@example.invalid
g config user.name sandbox
g config remote.origin.url "https://github.com/$SLUG.git"
g config "url.$ORIGIN.insteadOf" "https://github.com/$SLUG.git"

write_file src/y.txt "$(numbered_file y)"
write_file src/z.txt "$(numbered_file z)"
write_file docs/agents/x.md "$(numbered_file x)"
commit_all base
g push -q origin HEAD:master
MASTER=$(oid master)

echo "=== 1: the two-file shape: far-apart first regions, adjacent second regions ==="
branch_from feature-1 master
edit_line src/y.txt 3 "y line 3 ours"
edit_line src/y.txt 20 "y line 20 ours"
edit_line src/z.txt 5 "z line 5 ours"
commit_all ours-1
branch_from pr-101 master
edit_line src/y.txt 27 "y line 27 theirs"
edit_line src/y.txt 21 "y line 21 theirs"
edit_line src/z.txt 5 "z line 5 theirs"
commit_all theirs-101
publish_pr 101 pr-101
g checkout -q feature-1
pr_json "$TMP/prs-1.json" "$(pr_row 101 pr-101 master false - src/y.txt src/z.txt)"
run_tool --pr-json "$TMP/prs-1.json"
assert_rc 1 "T1 adjacent-line + same-line conflicts exit 1"
assert_eq "$(row_count)" "2" "T1 exactly 2 rows"
assert_contains "$OUT" "#101 src/y.txt conflicts" "T1 adjacent-line file is conflicts"
assert_contains "$OUT" "#101 src/z.txt conflicts" "T1 same-line file is conflicts"
assert_eq "$(verdict_line)" "VERDICT: overlap" "T1 verdict overlap"

echo "=== 2: a clean intersection on a block tree ==="
branch_from feature-2 master
edit_line docs/agents/x.md 25 "x line 25 ours"
commit_all ours-2
branch_from pr-102 master
edit_line docs/agents/x.md 5 "x line 5 theirs"
commit_all theirs-102
publish_pr 102 pr-102
g checkout -q feature-2
pr_json "$TMP/prs-2.json" "$(pr_row 102 pr-102 master false - docs/agents/x.md)"
run_tool --pr-json "$TMP/prs-2.json"
assert_rc 3 "T2 block-tree overlap exits 3"
assert_contains "$OUT" "#102 docs/agents/x.md clean" "T2 the row is clean"
assert_eq "$(verdict_line)" "VERDICT: block" "T2 verdict block"

echo "=== 3: overlap without conflict is reported, never exit 0 ==="
branch_from feature-3 master
edit_line src/y.txt 3 "y line 3 ours"
commit_all ours-3
branch_from pr-103 master
edit_line src/y.txt 10 "y line 10 theirs"
commit_all theirs-103
publish_pr 103 pr-103
branch_from pr-104 master
edit_line src/y.txt 3 "y line 3 ours"
commit_all theirs-104-identical
publish_pr 104 pr-104
g checkout -q feature-3
pr_json "$TMP/prs-3a.json" "$(pr_row 103 pr-103 master false - src/y.txt)"
run_tool --pr-json "$TMP/prs-3a.json"
assert_rc 1 "T3a disjoint regions in one file exit 1"
assert_contains "$OUT" "#103 src/y.txt clean" "T3a row is clean"
assert_eq "$(verdict_line)" "VERDICT: overlap" "T3a verdict overlap"
pr_json "$TMP/prs-3b.json" "$(pr_row 104 pr-104 master false - src/y.txt)"
run_tool --pr-json "$TMP/prs-3b.json"
assert_rc 1 "T3b identical edits exit 1"
assert_contains "$OUT" "#104 src/y.txt clean" "T3b identical edit on the same line is clean"

echo "=== 4: the aliased error arms are discriminated ==="
g checkout -q feature-3
pr_json "$TMP/prs-4a.json" "$(PR_ROW_OID=0123456789abcdef0123456789abcdef01234567 pr_row 105 pr-105 master false - src/y.txt)"
run_tool --pr-json "$TMP/prs-4a.json"
assert_rc 2 "T4a unresolvable head exits 2, not 1"
assert_contains "$ERR" "ERROR: #105" "T4a the error names the PR"
assert_contains "$ERR" "couldn't find remote ref refs/pull/105/head" "T4a the error quotes git's stderr"
assert_absent "$OUT" "VERDICT:" "T4a no verdict on an error"
g checkout -q --orphan orphan-106
g rm -q -r --cached .
write_file src/y.txt "unrelated"
commit_all orphan-106
publish_pr 106 orphan-106
g checkout -q feature-3
pr_json "$TMP/prs-4b.json" "$(pr_row 106 orphan-106 master false - src/y.txt)"
run_tool --pr-json "$TMP/prs-4b.json"
assert_rc 2 "T4b unrelated histories exit 2"
assert_contains "$ERR" "ERROR: #106" "T4b the error names the PR"
assert_contains "$ERR" "unrelated histories" "T4b the error quotes git's stderr"
assert_absent "$OUT" "VERDICT:" "T4b no verdict on an error"

echo "=== 5: enumeration past the 30-row default and past 100 files ==="
g checkout -q feature-3
reset_stub
# 34 rows; the only overlap (src/y.txt, pr-103's head) is the oldest, 1001.
rows=()
rows+=("$(PR_ROW_OID=$(oid pr-103) pr_row 1001 pr-1001 master false - src/y.txt)")
for n in $(seq 1002 1034); do
    rows+=("$(PR_ROW_OID=$(oid pr-104) pr_row "$n" "pr-$n" master false - "src/unrelated-$n.txt")")
done
pr_json "$GH_STUB_DIR/prs.json" "${rows[@]}"
git -C "$ORIGIN" update-ref refs/pull/1001/head "$(oid pr-103)"
for n in $(seq 1002 1034); do git -C "$ORIGIN" update-ref "refs/pull/$n/head" "$(oid pr-104)"; done
run_tool --repo "$SLUG"
assert_rc 1 "T5a the oldest of 34 rows is graded"
assert_contains "$OUT" "#1001 src/y.txt clean" "T5a the row outside the newest-30 window prints"
assert_eq "$(verdict_line)" "VERDICT: overlap" "T5a verdict overlap"
reset_stub
pr_json "$GH_STUB_DIR/prs.json" "${rows[@]:0:3}"
run_tool --limit 3
assert_rc 2 "T5b --limit equal to the row count exits 2"
assert_contains "$ERR" "returned 3 rows at --limit 3" "T5b the cap message names the limit"
assert_absent "$OUT" "VERDICT:" "T5b no verdict at the cap"
reset_stub
pr_json "$GH_STUB_DIR/prs.json" "${rows[@]:0:3}"
echo 4 > "$GH_STUB_DIR/totalcount"
run_tool
assert_rc 2 "T5c totalCount disagreement exits 2"
assert_contains "$ERR" "3 rows listed, totalCount 4" "T5c the error names both numbers"
assert_absent "$OUT" "VERDICT:" "T5c no verdict on a short enumeration"
# A master-based row whose files omit the overlapping path: changedFiles says
# 2, files carries 1, refs/pull/107/head carries both edits.
branch_from pr-107 master
edit_line src/y.txt 12 "y line 12 theirs"
write_file src/other-107.txt "other"
commit_all theirs-107
publish_pr 107 pr-107
g checkout -q feature-3
reset_stub
pr_json "$GH_STUB_DIR/prs.json" "$(pr_row 107 pr-107 master false 2 src/other-107.txt)"
run_tool
assert_rc 1 "T5d a truncated files row is widened from git and graded"
assert_contains "$OUT" "#107 src/y.txt clean" "T5d the path GitHub's files omitted prints"
assert_contains "$ERR" "#107 path set widened from git (2 paths; GitHub files 1 of 2" "T5d the widening is noted"
assert_eq "$(verdict_line)" "VERDICT: overlap" "T5d verdict overlap"
echo "--- stub self-tests ---"
reset_stub
pr_json "$GH_STUB_DIR/prs.json" "${rows[@]}"
stub_out=$(GH_STUB_DIR="$GH_STUB_DIR" GH_STUB_SLUG="$SLUG" "$TMP/bin/gh" pr list --repo "$SLUG" --state open --json number)
assert_eq "$(printf '%s' "$stub_out" | python3 -c 'import json,sys; print(len(json.load(sys.stdin)))')" "30" "stub: no --limit returns 30 of 34"
assert_absent "$stub_out" '"number": 1001' "stub: the oldest row falls outside the default window"
stub_err=$(GH_STUB_DIR="$GH_STUB_DIR" GH_STUB_SLUG="$SLUG" "$TMP/bin/gh" pr list --repo "$SLUG" --nosuch 2>&1 >/dev/null); stub_rc=$?
assert_eq "$stub_rc" "1" "stub: an unmodelled flag fails the way gh fails"
assert_contains "$stub_err" "unknown flag: --nosuch" "stub: the rejection names the flag"
stub_err=$(GH_STUB_DIR="$GH_STUB_DIR" GH_STUB_SLUG="$SLUG" "$TMP/bin/gh" pr view 1 2>&1 >/dev/null); stub_rc=$?
assert_eq "$stub_rc" "99" "stub: an unmodelled command fails closed"
stub_err=$(GH_STUB_DIR="$GH_STUB_DIR" GH_STUB_SLUG="$SLUG" "$TMP/bin/gh" pr list --repo "$SLUG" --json nosuchfield 2>&1 >/dev/null); stub_rc=$?
assert_eq "$stub_rc" "1" "stub: an unknown --json field fails the way gh fails"

echo "=== 6: self-identification and the same-head inversion ==="
branch_from feature-6 master
edit_line src/y.txt 3 "y line 3 ours"
edit_line docs/agents/x.md 25 "x line 25 ours"
commit_all ours-6
g push -q origin "refs/heads/feature-6:refs/pull/108/head" "refs/heads/feature-6:refs/pull/109/head" "refs/heads/feature-6:refs/pull/110/head"
pr_json "$TMP/prs-6.json" \
    "$(pr_row 108 feature-6 master false - src/y.txt)" \
    "$(pr_row 109 feature-6 master true - src/y.txt)" \
    "$(pr_row 110 twin-branch master false - src/y.txt docs/agents/x.md)"
run_tool --pr-json "$TMP/prs-6.json"
assert_rc 3 "T6 a same-head twin on a block tree exits 3"
assert_absent "$OUT" "#108" "T6 the same-repo row on the current branch is self, skipped"
assert_contains "$OUT" "#109 src/y.txt clean (same head)" "T6 the cross-repo row on the same branch name is retained"
assert_contains "$OUT" "#110 src/y.txt clean (same head)" "T6 the same-head twin prints its rows with the note"
assert_contains "$OUT" "#110 docs/agents/x.md clean (same head)" "T6 the twin's block-tree row prints"
assert_eq "$(verdict_line)" "VERDICT: block" "T6 verdict block"
pr_json "$TMP/prs-6b.json" "$(pr_row 108 feature-6 master false - src/y.txt)" "$(pr_row 110 feature-6 master false - src/y.txt)"
run_tool --pr-json "$TMP/prs-6b.json"
assert_rc 2 "T6b two same-repo rows on the current branch are ambiguous self"
assert_contains "$ERR" "ambiguous self: #108, #110" "T6b the error names both"
run_tool --pr-json "$TMP/prs-6b.json" --self-pr 108
assert_rc 1 "T6c --self-pr disambiguates"
assert_contains "$OUT" "#110 src/y.txt clean (same head)" "T6c the other row is a competitor"

echo "=== 7: stacks ==="
branch_from g master
edit_line src/y.txt 1 "y line 1 grandparent"
commit_all grandparent
publish_pr 203 g
branch_from p g
edit_line docs/agents/x.md 1 "x line 1 parent"
commit_all parent
publish_pr 201 p
branch_from c p
edit_line docs/agents/x.md 28 "x line 28 child"
edit_line src/z.txt 10 "z line 10 child"
edit_line src/y.txt 30 "y line 30 child"
commit_all child
branch_from q master
edit_line src/z.txt 20 "z line 20 q"
commit_all q
publish_pr 202 q
branch_from r master
edit_line src/z.txt 10 "z line 10 r-branch"
commit_all r-branch
g push -q origin refs/heads/r:refs/heads/r
branch_from pr-205 r
write_file src/w.txt "w"
commit_all r-child
publish_pr 205 pr-205
g checkout -q c
STACK_ROWS=(
    "$(pr_row 203 g master false - src/y.txt)"
    "$(pr_row 201 p g false - docs/agents/x.md)"
    "$(pr_row 202 q master false - src/z.txt)"
    "$(pr_row 205 pr-205 r false - src/w.txt)"
)
pr_json "$TMP/prs-7.json" "${STACK_ROWS[@]}"
run_tool --pr-json "$TMP/prs-7.json" --base p
assert_rc 1 "T7 a stack graded against its base exits 1"
assert_contains "$OUT" "#201 docs/agents/x.md upstream" "T7 the parent is upstream"
assert_contains "$OUT" "#203 src/y.txt upstream" "T7c the grandparent is upstream"
assert_contains "$OUT" "#202 src/z.txt clean" "T7 the unrelated competitor is clean"
assert_contains "$OUT" "#205 src/z.txt conflicts" "T7d the inherited-branch edit is found from git"
assert_contains "$ERR" "#205 path set widened from git (2 paths; GitHub files 1 of 1, base r)" "T7d the widening is noted"
assert_eq "$(verdict_line)" "VERDICT: overlap" "T7 upstream block-tree rows do not block"
run_tool --pr-json "$TMP/prs-7.json" --base master
assert_rc 2 "T7b the accidental-fork condition exits 2"
assert_contains "$ERR" "#201 (p) is in HEAD but not in --base master; pass --base p or rebase" "T7b the error names the parent and its branch"
assert_absent "$OUT" "VERDICT:" "T7b no verdict"
echo "--- 7 (stale default ref): the widening merge-base uses the fetched default tip ---"
branch_from r2 master
write_file src/v.txt "v line 1 r2"
commit_all r2
R2_OID=$(oid r2)
branch_from pr-206 r2
write_file src/w2.txt "w2 r2-child"
commit_all r2-child
publish_pr 206 pr-206
branch_from feature-7e "$MASTER"
write_file src/v.txt "v line 1 ours"
write_file src/w2.txt "w2 ours"
commit_all ours-7e
# Advance origin's master to absorb r2 from a second clone, leaving this
# clone's refs/remotes/origin/master stale.
git clone -q "$ORIGIN" "$TMP/other" 2>/dev/null
git -C "$TMP/other" push -q origin "$R2_OID:refs/heads/master"
assert_eq "$(oid origin/master)" "$MASTER" "T7e the local default ref is stale before the run"
pr_json "$TMP/prs-7e.json" "$(pr_row 206 pr-206 r2 false - src/w2.txt)"
run_tool --pr-json "$TMP/prs-7e.json" --base master
assert_rc 1 "T7e a competitor on an absorbed branch is graded"
assert_contains "$OUT" "#206 src/w2.txt conflicts" "T7e the competitor's own delta prints"
assert_absent "$OUT" "#206 src/v.txt" "T7e the path already on the default branch is not reported"
assert_contains "$ERR" "#206 path set widened from git (1 paths" "T7e the widened set has one path"
echo "--- 7a: a stale stack base blocks ---"
g checkout -q c
branch_from p-amend p
edit_line docs/agents/x.md 2 "x line 2 parent amended"
commit_all parent-amended
g push -q origin refs/heads/p-amend:refs/heads/p
g checkout -q c
run_tool --pr-json "$TMP/prs-7.json" --base p
assert_rc 3 "T7a a stale stack base exits 3"
assert_contains "$OUT" "#201 p stale (origin/p $(oid p-amend | cut -c1-9) not in HEAD)" "T7a the stale row names the parent PR and sha"
assert_eq "$(verdict_line)" "VERDICT: block" "T7a verdict block"
pr_json "$TMP/prs-7a.json" "${STACK_ROWS[@]:2}"
run_tool --pr-json "$TMP/prs-7a.json" --base p
assert_rc 3 "T7a stale with no open parent PR exits 3"
assert_contains "$OUT" "(no open PR) p stale (origin/p" "T7a the parent slot reads (no open PR)"

echo "=== 8: --repo must match origin ==="
g checkout -q feature-3
run_tool --pr-json "$TMP/prs-3a.json" --repo game
assert_rc 2 "T8 a mismatched --repo slug exits 2"
assert_contains "$ERR" "--repo jakildev/irreden does not match origin $SLUG" "T8 the error names both slugs"
run_tool --pr-json "$TMP/prs-3a.json" --repo "$SLUG"
assert_rc 1 "T8 the matching slug proceeds"
run_tool --pr-json "$TMP/prs-3a.json"
assert_rc 1 "T8 the omitted flag proceeds"

echo "=== 9: snapshot coherence ==="
g checkout -q feature-3
pr_json "$TMP/prs-9a.json" "$(PR_ROW_OID=$(oid pr-104) pr_row 103 pr-103 master false - src/y.txt)"
run_tool --pr-json "$TMP/prs-9a.json"
assert_rc 2 "T9a a head that differs from the snapshot exits 2"
assert_contains "$ERR" "#103 head moved during the run ($(oid pr-104) → $(oid pr-103)); re-run" "T9a the error names both OIDs"
assert_absent "$OUT" "VERDICT:" "T9a no verdict"
reset_stub
pr_json "$GH_STUB_DIR/prs.1.json" "$(pr_row 103 pr-103 master false - src/y.txt)" "$(pr_row 102 pr-102 master false - docs/agents/x.md)"
pr_json "$GH_STUB_DIR/prs.2.json" "$(pr_row 103 pr-103 master false - src/y.txt)" "$(PR_ROW_OID=$(oid pr-104) pr_row 102 pr-102 master false - docs/agents/x.md)"
run_tool
assert_rc 2 "T9b a non-overlapping PR amended mid-run exits 2"
assert_contains "$ERR" "#102 $(oid pr-102) → $(oid pr-104)" "T9b the delta names the amended head"
assert_absent "$OUT" "VERDICT:" "T9b no verdict"
reset_stub
pr_json "$GH_STUB_DIR/prs.1.json" "$(pr_row 103 pr-103 master false - src/y.txt)"
pr_json "$GH_STUB_DIR/prs.2.json" "$(pr_row 103 pr-103 master false - src/y.txt)" "$(pr_row 102 pr-102 master false - docs/agents/x.md)"
run_tool
assert_rc 2 "T9c a PR opened mid-run exits 2"
assert_contains "$ERR" "+#102" "T9c the delta names the new PR"
assert_absent "$OUT" "VERDICT:" "T9c no verdict"
reset_stub
pr_json "$GH_STUB_DIR/prs.json" "$(pr_row 103 pr-103 master false - src/y.txt)"
run_tool
assert_rc 1 "T9d identical snapshots proceed to a verdict"
assert_eq "$(verdict_line)" "VERDICT: overlap" "T9d verdict overlap"
assert_eq "$(cat "$GH_STUB_DIR/calls")" "2" "T9d the run took exactly two pr list snapshots"

echo "=== 10: a stacked sibling's own delta excludes paths inherited from the shared base ==="
branch_from stack-base master
edit_line docs/agents/x.md 1 "x line 1 stack-base"
commit_all stack-base-edit
g push -q origin refs/heads/stack-base:refs/heads/stack-base
branch_from self-10 stack-base
edit_line docs/agents/x.md 15 "x line 15 self"
edit_line src/y.txt 7 "y line 7 self"
commit_all self-10-edit
branch_from sib-10 stack-base
edit_line src/z.txt 3 "z line 3 sibling"
commit_all sib-10-edit
publish_pr 301 sib-10
g checkout -q self-10
pr_json "$TMP/prs-10a.json" "$(pr_row 301 sib-10 stack-base false - src/z.txt)"
run_tool --pr-json "$TMP/prs-10a.json" --base stack-base
assert_rc 0 "T10a disjoint own deltas on a stacked base exit clean"
assert_eq "$(row_count)" "0" "T10a no competitor rows"
assert_eq "$(verdict_line)" "VERDICT: clean" "T10a verdict clean"

g checkout -q sib-10
edit_line docs/agents/x.md 20 "x line 20 sibling"
commit_all sib-10-edit-2
publish_pr 301 sib-10
g checkout -q self-10
pr_json "$TMP/prs-10b.json" "$(pr_row 301 sib-10 stack-base false - src/z.txt docs/agents/x.md)"
run_tool --pr-json "$TMP/prs-10b.json" --base stack-base
assert_rc 3 "T10b the sibling's own edit to the block tree still blocks"
assert_contains "$OUT" "#301 docs/agents/x.md" "T10b the block-tree row prints"
assert_eq "$(verdict_line)" "VERDICT: block" "T10b verdict block"

echo "=== 11: a competitor off the ancestor arm drops a path already at the caller's base tip ==="
# pr-302 carries stack-base's docs/agents/x.md blob but names a base origin
# never had; pr-303 names other-base, which is not upstream of stack-base.
branch_from pr-302 stack-base
edit_line src/y.txt 28 "y line 28 gone-base child"
commit_all gone-base-child
publish_pr 302 pr-302
branch_from other-base master
edit_line src/z.txt 25 "z line 25 other-base"
commit_all other-base-edit
g push -q origin refs/heads/other-base:refs/heads/other-base
branch_from pr-303 stack-base
edit_line src/y.txt 29 "y line 29 other-base child"
commit_all other-base-child
publish_pr 303 pr-303
g checkout -q self-10
pr_json "$TMP/prs-11a.json" "$(pr_row 302 pr-302 gone-base false - src/y.txt)"
run_tool --pr-json "$TMP/prs-11a.json" --base stack-base
assert_rc 1 "T11a a competitor on a base origin lacks is graded from the default tip"
assert_contains "$OUT" "#302 src/y.txt clean" "T11a the path whose blob differs from the base tip is kept"
assert_absent "$OUT" "#302 docs/agents/x.md" "T11a the path whose blob matches the base tip is dropped"
assert_contains "$ERR" "#302 path set widened from git (1 paths; GitHub files 1 of 1, base gone-base)" "T11a the widened set counts the drop"
assert_eq "$(verdict_line)" "VERDICT: overlap" "T11a verdict overlap, not block"
pr_json "$TMP/prs-11b.json" "$(pr_row 303 pr-303 other-base false - src/y.txt)"
run_tool --pr-json "$TMP/prs-11b.json" --base stack-base
assert_rc 1 "T11b a competitor on an unrelated base is graded from the default tip"
assert_contains "$OUT" "#303 src/y.txt clean" "T11b the path whose blob differs from the base tip is kept"
assert_absent "$OUT" "#303 docs/agents/x.md" "T11b the path whose blob matches the base tip is dropped"
assert_contains "$ERR" "#303 path set widened from git (1 paths; GitHub files 1 of 1, base other-base)" "T11b the widened set counts the drop"
assert_eq "$(verdict_line)" "VERDICT: overlap" "T11b verdict overlap, not block"

echo "=== 12: the missing-subject guard ==="
STAGE="$TMP/stage/tests"
mkdir -p "$STAGE"
cp "$0" "$STAGE/$(basename "$0")"
cp "$(dirname "$0")/lib_assert.sh" "$(dirname "$0")/lib_preflight.sh" "$STAGE/"
guard_out=$(bash "$STAGE/$(basename "$0")" 2> "$TMP/guard-err.txt"); guard_rc=$?
guard_err=$(cat "$TMP/guard-err.txt")
assert_eq "$guard_rc" "3" "T12 a stage with no subject exits 3"
assert_contains "$guard_err" "SKIP: subject under test missing at $TMP/stage/fleet-pr-overlap" "T12 the SKIP line names the staged path"
assert_absent "$guard_out" "passed:" "T12 no tally is printed"

summarize
