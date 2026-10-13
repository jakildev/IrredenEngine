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
#      a same-line edit: exactly 2 rows, both conflicts, block, exit 3
#   2  clean and conflicting intersections on docs/agents/VALIDATION.md:
#      clean is informational; conflicts block
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
#   9  snapshot coherence: head/body/labels or population moved; identical proceeds
#   10 a stacked sibling's own delta excludes paths inherited from its base;
#      its clean edit to the same docs path remains informational
#   11 a competitor whose base is gone or unrelated drops a path whose blob
#      already matches the caller's base tip, and keeps one that differs
#   12 an actively parked descendant is sequenced; fail-closed and mixed cases
#   13 the missing-subject guard skips with exit 3 and no tally
#   14 a conflict the competitor already has with the base the head carries
#      is a base-conflict row, overlap, exit 1; a head that adds a conflict,
#      edits inside or beside an inherited one, or cannot be proven clear of
#      it still blocks; same grades under every host merge.conflictStyle

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
    local body="${PR_ROW_BODY_JSON:-\"\"}"
    local labels="${PR_ROW_LABELS_JSON:-[]}"
    [[ "$changed" == "-" ]] && changed=$#
    local files="" path
    for path in "$@"; do
        files+="${files:+,}{\"path\":\"$path\"}"
    done
    printf '{"number":%s,"title":"pr %s","headRefName":"%s","headRefOid":"%s","baseRefName":"%s","isCrossRepository":%s,"changedFiles":%s,"files":[%s],"body":%s,"labels":%s}' \
        "$n" "$n" "$head" "$head_oid" "$base" "$cross" "$changed" "$files" "$body" "$labels"
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

# run_tool_style <default|diff3|zdiff3> [args] — run_tool under a host merge.conflictStyle.
run_tool_style() {
    local style="$1"
    shift
    if [[ "$style" == "default" ]]; then
        run_tool "$@"
    else
        GIT_CONFIG_COUNT=1 GIT_CONFIG_KEY_0=merge.conflictStyle GIT_CONFIG_VALUE_0="$style" \
            run_tool "$@"
    fi
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
write_file docs/agents/VALIDATION.md "$(numbered_file x)"
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
assert_rc 3 "T1 adjacent-line + same-line conflicts exit 3"
assert_eq "$(row_count)" "2" "T1 exactly 2 rows"
assert_contains "$OUT" "#101 src/y.txt conflicts" "T1 adjacent-line file is conflicts"
assert_contains "$OUT" "#101 src/z.txt conflicts" "T1 same-line file is conflicts"
assert_eq "$(verdict_line)" "VERDICT: block" "T1 verdict block"

echo "=== 2: docs/agents/VALIDATION.md blocks only on a trial-merge conflict ==="
branch_from feature-2 master
edit_line docs/agents/VALIDATION.md 25 "x line 25 ours"
commit_all ours-2
branch_from pr-102 master
edit_line docs/agents/VALIDATION.md 5 "x line 5 theirs"
commit_all theirs-102
publish_pr 102 pr-102
g checkout -q feature-2
pr_json "$TMP/prs-2.json" "$(pr_row 102 pr-102 master false - docs/agents/VALIDATION.md)"
run_tool --pr-json "$TMP/prs-2.json"
assert_rc 1 "T2a a clean docs overlap exits 1"
assert_contains "$OUT" "#102 docs/agents/VALIDATION.md clean" "T2a the row is clean"
assert_eq "$(verdict_line)" "VERDICT: overlap" "T2a verdict overlap"
branch_from pr-112 master
edit_line docs/agents/VALIDATION.md 25 "x line 25 conflicting"
commit_all theirs-112
publish_pr 112 pr-112
g checkout -q feature-2
pr_json "$TMP/prs-2b.json" "$(pr_row 112 pr-112 master false - docs/agents/VALIDATION.md)"
run_tool --pr-json "$TMP/prs-2b.json"
assert_rc 3 "T2b a conflicting docs overlap exits 3"
assert_contains "$OUT" "#112 docs/agents/VALIDATION.md conflicts" "T2b the row conflicts"
assert_eq "$(verdict_line)" "VERDICT: block" "T2b verdict block"

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
edit_line docs/agents/VALIDATION.md 25 "x line 25 ours"
commit_all ours-6
g push -q origin "refs/heads/feature-6:refs/pull/108/head" "refs/heads/feature-6:refs/pull/109/head" "refs/heads/feature-6:refs/pull/110/head"
pr_json "$TMP/prs-6.json" \
    "$(pr_row 108 feature-6 master false - src/y.txt)" \
    "$(pr_row 109 feature-6 master true - src/y.txt)" \
    "$(pr_row 110 twin-branch master false - src/y.txt docs/agents/VALIDATION.md)"
run_tool --pr-json "$TMP/prs-6.json"
assert_rc 1 "T6 a same-head docs twin exits 1"
assert_absent "$OUT" "#108" "T6 the same-repo row on the current branch is self, skipped"
assert_contains "$OUT" "#109 src/y.txt clean (same head)" "T6 the cross-repo row on the same branch name is retained"
assert_contains "$OUT" "#110 src/y.txt clean (same head)" "T6 the same-head twin prints its rows with the note"
assert_contains "$OUT" "#110 docs/agents/VALIDATION.md clean (same head)" "T6 the twin's docs row prints"
assert_eq "$(verdict_line)" "VERDICT: overlap" "T6 verdict overlap"
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
edit_line docs/agents/VALIDATION.md 1 "x line 1 parent"
commit_all parent
publish_pr 201 p
branch_from c p
edit_line docs/agents/VALIDATION.md 28 "x line 28 child"
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
    "$(pr_row 201 p g false - docs/agents/VALIDATION.md)"
    "$(pr_row 202 q master false - src/z.txt)"
    "$(pr_row 205 pr-205 r false - src/w.txt)"
)
pr_json "$TMP/prs-7.json" "${STACK_ROWS[@]}"
run_tool --pr-json "$TMP/prs-7.json" --base p
assert_rc 3 "T7 a conflicting stack competitor exits 3"
assert_contains "$OUT" "#201 docs/agents/VALIDATION.md upstream" "T7 the parent is upstream"
assert_contains "$OUT" "#203 src/y.txt upstream" "T7c the grandparent is upstream"
assert_contains "$OUT" "#202 src/z.txt clean" "T7 the unrelated competitor is clean"
assert_contains "$OUT" "#205 src/z.txt conflicts" "T7d the inherited-branch edit is found from git"
assert_contains "$ERR" "#205 path set widened from git (2 paths; GitHub files 1 of 1, base r)" "T7d the widening is noted"
assert_eq "$(verdict_line)" "VERDICT: block" "T7 the non-upstream conflict blocks"
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
assert_rc 3 "T7e a conflicting competitor on an absorbed branch blocks"
assert_contains "$OUT" "#206 src/w2.txt conflicts" "T7e the competitor's own delta prints"
assert_absent "$OUT" "#206 src/v.txt" "T7e the path already on the default branch is not reported"
assert_contains "$ERR" "#206 path set widened from git (1 paths" "T7e the widened set has one path"
echo "--- 7a: a stale stack base blocks ---"
g checkout -q c
branch_from p-amend p
edit_line docs/agents/VALIDATION.md 2 "x line 2 parent amended"
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
pr_json "$GH_STUB_DIR/prs.1.json" "$(pr_row 103 pr-103 master false - src/y.txt)" "$(pr_row 102 pr-102 master false - docs/agents/VALIDATION.md)"
pr_json "$GH_STUB_DIR/prs.2.json" "$(pr_row 103 pr-103 master false - src/y.txt)" "$(PR_ROW_OID=$(oid pr-104) pr_row 102 pr-102 master false - docs/agents/VALIDATION.md)"
run_tool
assert_rc 2 "T9b a non-overlapping PR amended mid-run exits 2"
assert_contains "$ERR" "#102 $(oid pr-102) → $(oid pr-104)" "T9b the delta names the amended head"
assert_absent "$OUT" "VERDICT:" "T9b no verdict"
reset_stub
pr_json "$GH_STUB_DIR/prs.1.json" "$(pr_row 103 pr-103 master false - src/y.txt)"
pr_json "$GH_STUB_DIR/prs.2.json" "$(pr_row 103 pr-103 master false - src/y.txt)" "$(pr_row 102 pr-102 master false - docs/agents/VALIDATION.md)"
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
reset_stub
pr_json "$GH_STUB_DIR/prs.1.json" "$(pr_row 103 pr-103 master false - src/y.txt)"
pr_json "$GH_STUB_DIR/prs.2.json" "$(PR_ROW_BODY_JSON='"changed"' pr_row 103 pr-103 master false - src/y.txt)"
run_tool
assert_rc 2 "T9e a body edit mid-run exits 2"
assert_contains "$ERR" "#103 body changed" "T9e the delta names the body"
assert_absent "$OUT" "VERDICT:" "T9e no verdict"
reset_stub
pr_json "$GH_STUB_DIR/prs.1.json" "$(pr_row 103 pr-103 master false - src/y.txt)"
pr_json "$GH_STUB_DIR/prs.2.json" "$(PR_ROW_LABELS_JSON='[{"name":"fleet:awaiting-infra"}]' pr_row 103 pr-103 master false - src/y.txt)"
run_tool
assert_rc 2 "T9f a label edit mid-run exits 2"
assert_contains "$ERR" "#103 labels changed" "T9f the delta names the labels"
assert_absent "$OUT" "VERDICT:" "T9f no verdict"

echo "=== 10: a stacked sibling's own delta excludes paths inherited from the shared base ==="
branch_from stack-base master
edit_line docs/agents/VALIDATION.md 1 "x line 1 stack-base"
commit_all stack-base-edit
g push -q origin refs/heads/stack-base:refs/heads/stack-base
branch_from self-10 stack-base
edit_line docs/agents/VALIDATION.md 15 "x line 15 self"
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
edit_line docs/agents/VALIDATION.md 20 "x line 20 sibling"
commit_all sib-10-edit-2
publish_pr 301 sib-10
g checkout -q self-10
pr_json "$TMP/prs-10b.json" "$(pr_row 301 sib-10 stack-base false - src/z.txt docs/agents/VALIDATION.md)"
run_tool --pr-json "$TMP/prs-10b.json" --base stack-base
assert_rc 1 "T10b the sibling's clean docs edit is informational"
assert_contains "$OUT" "#301 docs/agents/VALIDATION.md clean" "T10b the clean docs row prints"
assert_eq "$(verdict_line)" "VERDICT: overlap" "T10b verdict overlap"

echo "=== 11: a competitor off the ancestor arm drops a path already at the caller's base tip ==="
# pr-302 carries stack-base's docs/agents/VALIDATION.md blob but names a base origin
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
assert_absent "$OUT" "#302 docs/agents/VALIDATION.md" "T11a the path whose blob matches the base tip is dropped"
assert_contains "$ERR" "#302 path set widened from git (1 paths; GitHub files 1 of 1, base gone-base)" "T11a the widened set counts the drop"
assert_eq "$(verdict_line)" "VERDICT: overlap" "T11a verdict overlap, not block"
pr_json "$TMP/prs-11b.json" "$(pr_row 303 pr-303 other-base false - src/y.txt)"
run_tool --pr-json "$TMP/prs-11b.json" --base stack-base
assert_rc 1 "T11b a competitor on an unrelated base is graded from the default tip"
assert_contains "$OUT" "#303 src/y.txt clean" "T11b the path whose blob differs from the base tip is kept"
assert_absent "$OUT" "#303 docs/agents/VALIDATION.md" "T11b the path whose blob matches the base tip is dropped"
assert_contains "$ERR" "#303 path set widened from git (1 paths; GitHub files 1 of 1, base other-base)" "T11b the widened set counts the drop"
assert_eq "$(verdict_line)" "VERDICT: overlap" "T11b verdict overlap, not block"

echo "=== 12: descendants explicitly parked behind self are sequenced ==="
branch_from feature-12 origin/master
edit_line src/y.txt 3 "y line 3 self-12"
commit_all self-12
publish_pr 401 feature-12
branch_from pr-402 origin/master
edit_line src/y.txt 3 "y line 3 sequenced-402"
commit_all sequenced-402
publish_pr 402 pr-402
branch_from pr-403 origin/master
edit_line src/y.txt 3 "y line 3 ordinary-403"
commit_all ordinary-403
publish_pr 403 pr-403
branch_from pr-404 origin/master
edit_line src/y.txt 10 "y line 10 clean-404"
commit_all clean-404
publish_pr 404 pr-404
g checkout -q feature-12
BACKING_ISSUE_12=3988
EARLY_ISSUE_12=111
EXTRA_ISSUE_12=5000
WRONG_ISSUE_12=9999
SELF_12_BODY="\"Closes #${BACKING_ISSUE_12}\""
SEQUENCED_12_BODY="\"Parked-until: #${EARLY_ISSUE_12}\\nParked-until: #${BACKING_ISSUE_12}, #${EXTRA_ISSUE_12} (later)\""
SELF_12="$(PR_ROW_BODY_JSON="$SELF_12_BODY" pr_row 401 feature-12 master false - src/y.txt)"
SEQUENCED_12="$(PR_ROW_BODY_JSON="$SEQUENCED_12_BODY" PR_ROW_LABELS_JSON='[{"name":"fleet:awaiting-infra"}]' pr_row 402 pr-402 master false - src/y.txt)"
pr_json "$TMP/prs-12a.json" "$SELF_12" "$SEQUENCED_12"
run_tool --pr-json "$TMP/prs-12a.json"
assert_rc 1 "T12a a conflicting descendant parked behind self exits 1"
assert_contains "$OUT" "#402 src/y.txt sequenced (Parked-until: #3988)" "T12a the matching self issue is named"
assert_eq "$(verdict_line)" "VERDICT: sequenced" "T12a verdict sequenced"

NO_LABEL_12_BODY="\"Parked-until: #${BACKING_ISSUE_12}\""
NO_LABEL_12="$(PR_ROW_BODY_JSON="$NO_LABEL_12_BODY" pr_row 402 pr-402 master false - src/y.txt)"
pr_json "$TMP/prs-12b.json" "$SELF_12" "$NO_LABEL_12"
run_tool --pr-json "$TMP/prs-12b.json"
assert_rc 3 "T12b a stale marker without the active label blocks"
assert_contains "$OUT" "#402 src/y.txt conflicts" "T12b the stale marker stays an ordinary conflict"

# Every parsable marker is live: a park spelled one blocker per line waits on
# all of them, so an earlier line naming self still sequences the descendant.
EARLIER_SELF_12_BODY="\"Parked-until: #${BACKING_ISSUE_12}\\nParked-until: #${WRONG_ISSUE_12}\""
EARLIER_SELF_12="$(PR_ROW_BODY_JSON="$EARLIER_SELF_12_BODY" PR_ROW_LABELS_JSON='[{"name":"fleet:awaiting-infra"}]' pr_row 402 pr-402 master false - src/y.txt)"
pr_json "$TMP/prs-12c.json" "$SELF_12" "$EARLIER_SELF_12"
run_tool --pr-json "$TMP/prs-12c.json"
assert_rc 1 "T12c an earlier marker naming self still sequences"
assert_contains "$OUT" "#402 src/y.txt sequenced (Parked-until: #3988)" "T12c the self issue is named from the earlier line"

# A marker naming only the OTHER fleet repo's issue never matches a same-repo
# self issue, even when the numbers coincide.
FOREIGN_12_BODY="\"Parked-until: irreden#${BACKING_ISSUE_12}\""
FOREIGN_12="$(PR_ROW_BODY_JSON="$FOREIGN_12_BODY" PR_ROW_LABELS_JSON='[{"name":"fleet:awaiting-infra"}]' pr_row 402 pr-402 master false - src/y.txt)"
pr_json "$TMP/prs-12e.json" "$SELF_12" "$FOREIGN_12"
run_tool --pr-json "$TMP/prs-12e.json"
assert_rc 3 "T12e a cross-repo marker with the same number does not sequence"

NO_CLOSE_12_BODY="\"Refs #${BACKING_ISSUE_12}\""
NO_CLOSE_12="$(PR_ROW_BODY_JSON="$NO_CLOSE_12_BODY" pr_row 401 feature-12 master false - src/y.txt)"
pr_json "$TMP/prs-12d.json" "$NO_CLOSE_12" "$SEQUENCED_12"
run_tool --pr-json "$TMP/prs-12d.json"
assert_rc 3 "T12d self without a closing reference blocks"

MALFORMED_12_BODY="\"Parked-until: later #${BACKING_ISSUE_12}\""
MALFORMED_12="$(PR_ROW_BODY_JSON="$MALFORMED_12_BODY" PR_ROW_LABELS_JSON='[{"name":"fleet:awaiting-infra"}]' pr_row 402 pr-402 master false - src/y.txt)"
pr_json "$TMP/prs-12e.json" "$SELF_12" "$MALFORMED_12"
run_tool --pr-json "$TMP/prs-12e.json"
assert_rc 3 "T12e a malformed marker blocks"

ORDINARY_12="$(pr_row 403 pr-403 master false - src/y.txt)"
pr_json "$TMP/prs-12f.json" "$SELF_12" "$SEQUENCED_12" "$ORDINARY_12"
run_tool --pr-json "$TMP/prs-12f.json"
assert_rc 3 "T12f an unrelated conflict retains block priority"
assert_contains "$OUT" "#402 src/y.txt sequenced" "T12f the parked descendant remains visible"
assert_contains "$OUT" "#403 src/y.txt conflicts" "T12f the unrelated conflict remains visible"
assert_eq "$(verdict_line)" "VERDICT: block" "T12f verdict block"

CLEAN_12="$(pr_row 404 pr-404 master false - src/y.txt)"
pr_json "$TMP/prs-12g.json" "$SELF_12" "$SEQUENCED_12" "$CLEAN_12"
run_tool --pr-json "$TMP/prs-12g.json"
assert_rc 1 "T12g sequenced plus ordinary clean overlap exits 1"
assert_contains "$OUT" "#404 src/y.txt clean" "T12g the ordinary overlap remains visible"
assert_eq "$(verdict_line)" "VERDICT: overlap" "T12g overlap outranks sequenced"

# Placed above 13 so the missing-subject guard stays the suite's last section.
echo "=== 14: a conflict the competitor already has with the base the head carries ==="
stanza() { seq 1 12 | sed "s/^/stanza line /" | sed '6s/.*/value=0/'; }
short_stanza() { printf '%s\n' s0 s1 value=0 s2 s3; }
g fetch -q origin
branch_from fork-14 origin/master
write_file src/w.txt "$(numbered_file w)"
write_file src/v.txt "$(numbered_file v)"
write_file src/gone.txt "$(numbered_file gone)"
write_file src/dup.txt "$(stanza; stanza)"
write_file src/locked.txt "$(numbered_file locked)"
# git merge-tree takes attributes from the clone it runs in.
mkdir -p "$WORK/.git/info"
printf '%s\n' "src/locked.txt -merge" > "$WORK/.git/info/attributes"
write_file src/pipe.txt "$(numbered_file pipe)"
write_file src/old.txt "$(numbered_file old)"
write_file src/carry.txt "$(numbered_file carry)"
write_file src/rep.txt "$(short_stanza; short_stanza; short_stanza)"
commit_all fork-14
g push -q origin HEAD:master
FORK_14=$(oid HEAD)

# Every competitor forks before the base edits the contested lines.
branch_from pr-501 "$FORK_14"
edit_line src/w.txt 10 "w line 10 competitor"
commit_all theirs-501
publish_pr 501 pr-501
branch_from pr-502 "$FORK_14"
edit_line src/w.txt 10 "w line 10 competitor"
edit_line src/w.txt 25 "w line 25 competitor"
commit_all theirs-502
publish_pr 502 pr-502
branch_from pr-503 "$FORK_14"
g rm -q src/gone.txt
commit_all theirs-503
publish_pr 503 pr-503
branch_from pr-504 "$FORK_14"
edit_line src/w.txt 10 "w line 10 competitor"
edit_line src/v.txt 5 "v line 5 competitor"
commit_all theirs-504
publish_pr 504 pr-504
branch_from pr-505 "$FORK_14"
edit_line src/dup.txt 6 "value=competitor"
edit_line src/dup.txt 18 "value=competitor"
commit_all theirs-505
publish_pr 505 pr-505
branch_from pr-506 "$FORK_14"
edit_line src/w.txt 10 "w line 10 competitor"
edit_line src/w.txt 25 "w line 25 ours"
commit_all theirs-506
publish_pr 506 pr-506
branch_from pr-507 "$FORK_14"
edit_line src/w.txt 10 "w line 10 competitor"
edit_line src/w.txt 15 "w line 15 agreed"
commit_all theirs-507
publish_pr 507 pr-507
branch_from pr-508 "$FORK_14"
for n in 18 19 20 21 22; do edit_line src/v.txt "$n" "v line $n competitor"; done
commit_all theirs-508
publish_pr 508 pr-508
branch_from pr-509 "$FORK_14"
edit_line src/locked.txt 10 "locked line 10 competitor"
commit_all theirs-509
publish_pr 509 pr-509
branch_from pr-510 "$FORK_14"
for n in 10 11 12 20 21 22; do edit_line src/pipe.txt "$n" "pipe line $n competitor"; done
commit_all theirs-510
publish_pr 510 pr-510
branch_from pr-511 "$FORK_14"
edit_line src/old.txt 10 "old line 10 competitor"
write_file src/new.txt "$(numbered_file other)"
commit_all theirs-511
publish_pr 511 pr-511
branch_from pr-512 "$FORK_14"
edit_line src/carry.txt 3 "carry line 3 competitor"
commit_all theirs-512-early
EARLY_512=$(oid HEAD)
edit_line src/carry.txt 10 "carry line 10 competitor"
commit_all theirs-512
publish_pr 512 pr-512
branch_from pr-513 "$FORK_14"
edit_line src/rep.txt 3 "x"
commit_all theirs-513
publish_pr 513 pr-513

branch_from base-14 "$FORK_14"
edit_line src/w.txt 10 "w line 10 base"
edit_line src/w.txt 15 "w line 15 agreed"
edit_line src/v.txt 18 "v line 18 base"
edit_line src/v.txt 22 "v line 22 base"
edit_line src/gone.txt 10 "gone line 10 base"
edit_line src/dup.txt 6 "value=base"
edit_line src/locked.txt 10 "locked line 10 base"
for n in 10 12 20 22; do edit_line src/pipe.txt "$n" "pipe line $n base"; done
edit_line src/pipe.txt 11 "||||||| literal-old"
edit_line src/pipe.txt 21 "======= literal-old"
g mv src/old.txt src/new.txt
edit_line src/new.txt 10 "old line 10 base"
edit_line src/carry.txt 10 "carry line 10 base"
edit_line src/rep.txt 3 "value=base"
edit_line src/rep.txt 14 "value=base"
commit_all base-14
g push -q origin HEAD:master
CARRIED_14=$(oid HEAD)
CARRIED_14_SHORT=${CARRIED_14:0:9}

# feature-14 stays clear of every contested region; each sibling touches one.
branch_from feature-14 "$CARRIED_14"
edit_line src/w.txt 25 "w line 25 ours"
edit_line src/v.txt 5 "v line 5 ours"
edit_line src/gone.txt 25 "gone line 25 ours"
edit_line src/locked.txt 25 "locked line 25 ours"
edit_line src/pipe.txt 28 "pipe line 28 ours"
edit_line src/new.txt 25 "old line 25 ours"
commit_all ours-14
branch_from feature-14-same "$CARRIED_14"
edit_line src/w.txt 10 "w line 10 ours"
commit_all ours-14-same
branch_from feature-14-adjacent "$CARRIED_14"
edit_line src/w.txt 11 "w line 11 ours"
commit_all ours-14-adjacent
branch_from feature-14-agreed "$CARRIED_14"
edit_line src/w.txt 15 "w line 15 ours"
commit_all ours-14-agreed
branch_from feature-14-inside "$CARRIED_14"
edit_line src/v.txt 20 "v line 20 ours"
commit_all ours-14-inside
branch_from feature-14-literal "$CARRIED_14"
edit_line src/w.txt 25 "<<<<<<< not a merge marker"
commit_all ours-14-literal
branch_from feature-14-dup "$CARRIED_14"
edit_line src/dup.txt 6 "value=competitor"
edit_line src/dup.txt 18 "value=base"
commit_all ours-14-dup
branch_from feature-14-pipe "$CARRIED_14"
edit_line src/pipe.txt 11 "||||||| literal-new"
commit_all ours-14-pipe
branch_from feature-14-equals "$CARRIED_14"
edit_line src/pipe.txt 21 "======= literal-new"
commit_all ours-14-equals
branch_from feature-14-carry "$CARRIED_14"
g merge -q --no-edit "$EARLY_512"
edit_line src/carry.txt 25 "carry line 25 ours"
commit_all ours-14-carry
# One inserted line in the first stanza and one edit in the second. The trial
# merge then reports its one conflict in the second stanza, while replaying
# the head's change onto the base's merged file leaves it in the first.
branch_from feature-14-repeat "$CARRIED_14"
write_file src/rep.txt "$(printf '%s\n' \
    s0 value=0 s1 value=base s2 s3 \
    s0 s1 value=base s2 s3 \
    s0 s1 value=0 value=base s3)"
commit_all ours-14-repeat

pr_json "$TMP/prs-14-501.json" "$(pr_row 501 pr-501 master false - src/w.txt)"
pr_json "$TMP/prs-14-502.json" "$(pr_row 502 pr-502 master false - src/w.txt)"
pr_json "$TMP/prs-14-503.json" "$(pr_row 503 pr-503 master false - src/gone.txt)"
pr_json "$TMP/prs-14-504.json" "$(pr_row 504 pr-504 master false - src/w.txt src/v.txt)"
pr_json "$TMP/prs-14-505.json" "$(pr_row 505 pr-505 master false - src/dup.txt)"
pr_json "$TMP/prs-14-506.json" "$(pr_row 506 pr-506 master false - src/w.txt)"
pr_json "$TMP/prs-14-507.json" "$(pr_row 507 pr-507 master false - src/w.txt)"
pr_json "$TMP/prs-14-508.json" "$(pr_row 508 pr-508 master false - src/v.txt)"
pr_json "$TMP/prs-14-509.json" "$(pr_row 509 pr-509 master false - src/locked.txt)"
pr_json "$TMP/prs-14-510.json" "$(pr_row 510 pr-510 master false - src/pipe.txt)"
pr_json "$TMP/prs-14-511.json" "$(pr_row 511 pr-511 master false - src/old.txt src/new.txt)"
pr_json "$TMP/prs-14-512.json" "$(pr_row 512 pr-512 master false - src/carry.txt)"
pr_json "$TMP/prs-14-513.json" "$(pr_row 513 pr-513 master false - src/rep.txt)"
BASE_ROW_14="#501 src/w.txt base-conflict (conflicts with master $CARRIED_14_SHORT alone)"

for style in default diff3 zdiff3; do
    g checkout -q feature-14
    run_tool_style "$style" --pr-json "$TMP/prs-14-501.json"
    assert_rc 1 "T14a $style a conflict the carried base already has exits 1"
    assert_contains "$OUT" "$BASE_ROW_14" "T14a $style the row names the carried base"
    assert_absent "$OUT" "#501 src/w.txt conflicts" "T14a $style no conflicts row for the path"
    assert_eq "$(verdict_line)" "VERDICT: overlap" "T14a $style verdict overlap"
done

run_tool --pr-json "$TMP/prs-14-508.json"
assert_rc 1 "T14b a five-line inherited block the head stays clear of exits 1"
assert_contains "$OUT" "#508 src/v.txt base-conflict" "T14b the row is base-conflict"

run_tool --pr-json "$TMP/prs-14-506.json"
assert_rc 1 "T14c the competitor making the head's own far edit exits 1"
assert_contains "$OUT" "#506 src/w.txt base-conflict" "T14c the row is base-conflict"

for style in default diff3 zdiff3; do
    g checkout -q feature-14
    run_tool_style "$style" --pr-json "$TMP/prs-14-510.json"
    assert_rc 1 "T14d $style literal pipe and equals lines the head leaves alone exit 1"
    assert_contains "$OUT" "#510 src/pipe.txt base-conflict" "T14d $style the row is base-conflict"
    g checkout -q feature-14-carry
    run_tool_style "$style" --pr-json "$TMP/prs-14-512.json"
    assert_rc 1 "T14e $style a head carrying an earlier competitor commit exits 1"
    assert_contains "$OUT" "#512 src/carry.txt base-conflict" "T14e $style the row is base-conflict"
done

g checkout -q feature-14-same
run_tool --pr-json "$TMP/prs-14-501.json"
assert_rc 3 "T14f the head editing the contested line blocks"
assert_contains "$OUT" "#501 src/w.txt conflicts" "T14f the row conflicts"
assert_absent "$OUT" "base-conflict" "T14f no base-conflict row"
assert_eq "$(verdict_line)" "VERDICT: block" "T14f verdict block"

g checkout -q feature-14-adjacent
run_tool --pr-json "$TMP/prs-14-501.json"
assert_rc 3 "T14g the head editing the adjacent line blocks"
assert_contains "$OUT" "#501 src/w.txt conflicts" "T14g the row conflicts"

g checkout -q feature-14
run_tool --pr-json "$TMP/prs-14-502.json"
assert_rc 3 "T14h one inherited block plus one new block blocks"
assert_contains "$OUT" "#502 src/w.txt conflicts" "T14h the row conflicts"

run_tool --pr-json "$TMP/prs-14-503.json"
assert_rc 3 "T14i a modify/delete conflict blocks"
assert_contains "$OUT" "#503 src/gone.txt conflicts" "T14i the row conflicts"

for style in default diff3 zdiff3; do
    g checkout -q feature-14-dup
    run_tool_style "$style" --pr-json "$TMP/prs-14-505.json"
    assert_rc 3 "T14j $style resolving the inherited block and adding a byte-identical one blocks"
    assert_contains "$OUT" "#505 src/dup.txt conflicts" "T14j $style the row conflicts"
    assert_eq "$(verdict_line)" "VERDICT: block" "T14j $style verdict block"
done

g checkout -q feature-14-inside
run_tool --pr-json "$TMP/prs-14-508.json"
assert_rc 3 "T14k the head editing a line inside an inherited block blocks"
assert_contains "$OUT" "#508 src/v.txt conflicts" "T14k the row conflicts"

g checkout -q feature-14-agreed
run_tool --pr-json "$TMP/prs-14-507.json"
assert_rc 3 "T14l the head re-editing a line the base and competitor agreed on blocks"
assert_contains "$OUT" "#507 src/w.txt conflicts" "T14l the row conflicts"

g checkout -q feature-14-literal
run_tool --pr-json "$TMP/prs-14-501.json"
assert_rc 3 "T14m a head whose file carries a literal marker line blocks"
assert_contains "$OUT" "#501 src/w.txt conflicts" "T14m the row conflicts"

g checkout -q feature-14
run_tool --pr-json "$TMP/prs-14-509.json"
assert_rc 3 "T14n a conflict git writes no block for blocks"
assert_contains "$OUT" "#509 src/locked.txt conflicts" "T14n the row conflicts"

for style in default diff3 zdiff3; do
    g checkout -q feature-14-pipe
    run_tool_style "$style" --pr-json "$TMP/prs-14-510.json"
    assert_rc 3 "T14o $style a head edit of a literal seven-pipe line inside an inherited block blocks"
    assert_contains "$OUT" "#510 src/pipe.txt conflicts" "T14o $style the row conflicts"
    g checkout -q feature-14-equals
    run_tool_style "$style" --pr-json "$TMP/prs-14-510.json"
    assert_rc 3 "T14p $style a head edit of a literal seven-equals line inside an inherited block blocks"
    assert_contains "$OUT" "#510 src/pipe.txt conflicts" "T14p $style the row conflicts"
done

g checkout -q feature-14
run_tool --pr-json "$TMP/prs-14-511.json"
assert_rc 3 "T14q a head edit inside a rename/add conflict blocks"
assert_contains "$OUT" "#511 src/new.txt conflicts" "T14q the row conflicts"

g checkout -q feature-14-repeat
run_tool --pr-json "$TMP/prs-14-513.json"
assert_rc 3 "T14r a clean replay that differs from the trial merge blocks"
assert_contains "$OUT" "#513 src/rep.txt conflicts" "T14r the row conflicts"

g checkout -q feature-14
run_tool --pr-json "$TMP/prs-14-504.json"
assert_rc 3 "T14s a base conflict beside a head conflict blocks"
assert_contains "$OUT" "#504 src/w.txt base-conflict" "T14s the inherited path keeps its grade"
assert_contains "$OUT" "#504 src/v.txt conflicts" "T14s the head's conflict keeps its grade"
assert_eq "$(verdict_line)" "VERDICT: block" "T14s verdict block"

branch_from base-14-again "$CARRIED_14"
edit_line src/w.txt 10 "w line 10 base again"
commit_all base-14-again
g push -q origin HEAD:master
g checkout -q feature-14
run_tool --pr-json "$TMP/prs-14-501.json"
assert_rc 1 "T14t a base that moved again after the head took it exits 1"
assert_contains "$OUT" "$BASE_ROW_14" "T14t the row names the carried commit, not the tip"

echo "=== 13: the missing-subject guard ==="
STAGE="$TMP/stage/tests"
mkdir -p "$STAGE"
cp "$0" "$STAGE/$(basename "$0")"
cp "$(dirname "$0")/lib_assert.sh" "$(dirname "$0")/lib_preflight.sh" "$STAGE/"
guard_out=$(bash "$STAGE/$(basename "$0")" 2> "$TMP/guard-err.txt"); guard_rc=$?
guard_err=$(cat "$TMP/guard-err.txt")
assert_eq "$guard_rc" "3" "T13 a stage with no subject exits 3"
assert_contains "$guard_err" "SKIP: subject under test missing at $TMP/stage/fleet-pr-overlap" "T13 the SKIP line names the staged path"
assert_absent "$guard_out" "passed:" "T13 no tally is printed"

summarize
