#!/usr/bin/env bash
# Tests for fleet-claim's check_blockers gate (issue-based).
#
# regression: the blocker parser extracts every `#N` reference from the
# **Blocked by:** field, including a parenthetical PR reference alongside the
# blocking issue number. A parenthetical PR number (which `gh issue view`
# reports as state == MERGED) must be accepted as satisfied, not rejected
# for failing to be an issue in state == CLOSED — the gate accepts CLOSED
# or MERGED.
#
# These tests stub `gh` so check_blockers reads canned JSON instead of
# hitting GitHub. The stub dispatches on the subcommand + presence of
# `--jq` to distinguish:
#   - fetch_issue_info: `gh issue view N --json state,labels,body`
#   - check_blockers ref lookup: `gh issue view N --json state --jq .state`
#   - check_blockers PR-URL lookup: `gh pr view URL --json state --jq .state`
#
# Covers:
#   - parenthetical PR ref, issue CLOSED + PR MERGED → claim succeeds (the bug)
#   - parenthetical PR ref, PR still OPEN → claim fails
#   - "(none — commentary)" form → no-op pass
#   - PR-URL form MERGED → pass
#   - PR-URL form OPEN → fail
#   - bare `#N` issue OPEN → fail
#   - no Blocked-by line → pass
#   - parenthetical PR ref, PR CLOSED-abandoned → claim succeeds (documents
#     current `#N`-form behavior: gate accepts CLOSED|MERGED, so an abandoned
#     PR matched via `#N` passes. Note this differs from the explicit URL
#     form, which accepts MERGED only — if abandoned-PR refs ever need to
#     fail the gate, the `#N` branch in fleet-claim.check_blockers needs
#     to differentiate PR-state CLOSED from issue-state CLOSED.)
#   - cross-repo ref `[owner/]Repo#N`, CLOSED in the referenced repo → pass;
#     still OPEN in the referenced repo → fail (the gate routes the
#     state probe to the referenced repo, not the issue's own).

set -euo pipefail

# This suite exercises cmd_claim against the real (possibly-stale) main clone but
# does not care about clone freshness — disable the freshness gate.
export FLEET_SKIP_CLONE_FRESHNESS=1

SCRIPT_DIR=$(cd "$(dirname "$0")/.." && pwd)
source "$(dirname "$0")/lib_preflight.sh"
FLEET_CLAIM="$SCRIPT_DIR/fleet-claim"

if [[ ! -x "$FLEET_CLAIM" ]]; then
    echo "test setup: fleet-claim not found at $FLEET_CLAIM" >&2
    exit 1
fi

PASS=0
FAIL=0
TMPROOT=""

cleanup() {
    [[ -n "$TMPROOT" && -d "$TMPROOT" ]] && rm -rf "$TMPROOT"
}
trap cleanup EXIT

assert_exit() {
    local actual_exit="$1" expected_exit="$2" msg="$3"
    if [[ "$actual_exit" -eq "$expected_exit" ]]; then
        PASS=$((PASS + 1))
        echo "  ok: $msg"
    else
        FAIL=$((FAIL + 1))
        echo "  FAIL: $msg"
        echo "        expected exit: $expected_exit"
        echo "        actual exit:   $actual_exit"
    fi
}

TMPROOT=$(mktemp -d)
source "$(dirname "$0")/lib_hermetic.sh"
hermetic_poison_gh_env "$TMPROOT"
export FLEET_CLAIMS_DIR="$TMPROOT/claims"
export FLEET_RESERVATIONS_DIR="$TMPROOT/reservations"
mkdir -p "$FLEET_CLAIMS_DIR" "$FLEET_RESERVATIONS_DIR"

STUB_DIR="$TMPROOT/bin"
mkdir -p "$STUB_DIR"

cat >"$STUB_DIR/gh" <<'GHSTUB'
#!/usr/bin/env python3
# Stub for check_blockers tests.
#
# Recognised invocations:
#   gh issue view <N> --repo R --json state,labels,body         -> full info
#   gh issue view <N> --repo R --json state --jq .state         -> state only
#   gh pr view <N> --repo R --json state --jq .state            -> state only
#   gh pr list --repo R --state open --json ... --jq ...        -> no output
#   gh api repos/.../issues/N/labels --method POST -f labels[]= -> echo back
#   gh issue edit ... / gh label ...                            -> no-op
#
# Argument parsing scans for the first bare positive integer rather than
# trusting positional args. fleet-claim today calls `gh issue view N --repo
# R --json ...`, but the stub stays valid if the call form ever shifts (e.g.
# `gh issue view --repo R N ...`) - otherwise the stub silently falls
# through to the default `OPEN` branch and the tests pass for the wrong
# reason.
import os
import re
import sys

args = sys.argv[1:]
has_jq = False
issue_num = ""
pr_url = ""
repo = ""  # captured from `--repo R` so cross-repo refs can be
prev = ""  # routed-checked: the same #N resolves differently per repo.
for arg in args:
    if arg == "--jq":
        has_jq = True
    if prev == "--repo":
        repo = arg
    if not issue_num and re.fullmatch(r"[0-9]+", arg):
        issue_num = arg
    if not pr_url and re.fullmatch(r"https://github\.com/.*/pull/.*", arg):
        pr_url = arg
    prev = arg


def emit(text):
    sys.stdout.buffer.write(text.encode("utf-8"))
    sys.stdout.flush()


STATE_ONLY = {
    # check_blockers state-only lookup for `#N` references.
    "100": "CLOSED",  # issue, resolved
    "101": "OPEN",  # issue, still open
    "200": "MERGED",  # PR, merged
    "201": "OPEN",  # PR, still open
    "202": "CLOSED",  # PR, abandoned (closed without merge)
}

ISSUE_INFO = {
    # Parenthetical PR ref, both resolved.
    "2001": r'{"state":"OPEN","labels":[{"name":"fleet:queued"}],"body":"## Scope\n\n**Blocked by:** #100 (PR #200 must merge — context)\n"}',
    # Same shape but the PR is still open.
    "2002": r'{"state":"OPEN","labels":[{"name":"fleet:queued"}],"body":"**Blocked by:** #100 (PR #201 must merge — context)\n"}',
    # `(none …)` form — no blocker.
    "2003": r'{"state":"OPEN","labels":[{"name":"fleet:queued"}],"body":"**Blocked by:** (none — independent task)\n"}',
    # PR-URL form, MERGED.
    "2004": r'{"state":"OPEN","labels":[{"name":"fleet:queued"}],"body":"**Blocked by:** https://github.com/jakildev/IrredenEngine/pull/200\n"}',
    # PR-URL form, OPEN.
    "2005": r'{"state":"OPEN","labels":[{"name":"fleet:queued"}],"body":"**Blocked by:** https://github.com/jakildev/IrredenEngine/pull/201\n"}',
    # PR-URL form, CLOSED without merge.
    "2022": r'{"state":"OPEN","labels":[{"name":"fleet:queued"}],"body":"**Blocked by:** https://github.com/jakildev/IrredenEngine/pull/202\n"}',
    # Bare `#N` issue ref, still open.
    "2006": r'{"state":"OPEN","labels":[{"name":"fleet:queued"}],"body":"**Blocked by:** #101\n"}',
    # No Blocked-by line at all.
    "2007": r'{"state":"OPEN","labels":[{"name":"fleet:queued"}],"body":"## Scope\n\nIndependent task.\n"}',
    # Parenthetical PR ref, PR closed without merging.
    "2008": r'{"state":"OPEN","labels":[{"name":"fleet:queued"}],"body":"**Blocked by:** #100 (PR #202 abandoned)\n"}',
    # Dependency declared only as `## Blocked on #N` header
    # prose (no **Blocked by:** field), the referenced issue OPEN.
    "2009": r'{"state":"OPEN","labels":[{"name":"fleet:queued"}],"body":"## Scope\n\n## Blocked on #101\n\nWork.\n"}',
    # Header prose, referenced issue CLOSED → no longer a blocker.
    "2010": r'{"state":"OPEN","labels":[{"name":"fleet:queued"}],"body":"Blocked on #100\n"}',
    # Header prose with no #N / PR reference → not a real blocker.
    "2011": r'{"state":"OPEN","labels":[{"name":"fleet:queued"}],"body":"## Blocked on the redesign\n"}',
    # Canonical field wins over header prose: field says (none),
    # so the `## Blocked on` header must be ignored.
    "2012": r'{"state":"OPEN","labels":[{"name":"fleet:queued"}],"body":"**Blocked by:** (none — independent)\n\n## Blocked on #101\n"}',
    # Two separate **Blocked by:** lines — the gate unions them;
    # the second issue is still OPEN so the claim must be blocked.
    "2013": r'{"state":"OPEN","labels":[{"name":"fleet:queued"}],"body":"**Blocked by:** #100\n**Blocked by:** #101\n"}',
    # Inline-bold form — **Blocked by: #N (label)** mid-line,
    # the referenced issue still OPEN → claim must be blocked.
    "2014": r'{"state":"OPEN","labels":[{"name":"fleet:queued"}],"body":"**Part of epic:** #104 · **Phase 3 of 4** · **Blocked by: #101 (Phase 2)**\n"}',
    # Inline-bold form, referenced issue CLOSED → pass.
    "2015": r'{"state":"OPEN","labels":[{"name":"fleet:queued"}],"body":"**Part of epic:** #104 · **Phase 3 of 4** · **Blocked by: #100 (Phase 2)**\n"}',
    # Inline-bold form with no #N/PR ref — must not gate.
    "2016": r'{"state":"OPEN","labels":[{"name":"fleet:queued"}],"body":"**Part of epic:** #104 · **Blocked by: the redesign**\n"}',
    # Cross-repo blocker in another repo (owner-qualified),
    # CLOSED there → claim succeeds once routed to the right repo.
    "2017": r'{"state":"OPEN","labels":[{"name":"fleet:queued"}],"body":"**Blocked by:** jakildev/irreden#125\n"}',
    # Cross-repo blocker (bare repo qualifier), still OPEN in
    # the referenced repo → claim fails.
    "2018": r'{"state":"OPEN","labels":[{"name":"fleet:queued"}],"body":"**Blocked by:** irreden#126\n"}',
    # Plain mid-line `Blocked by: #N` form, the referenced issue
    # OPEN → claim fails. Also guards that the epic ref in the
    # prose is NOT mistaken for a blocker — only the
    # `Blocked by:` ref-list is captured.
    "2019": r'{"state":"OPEN","labels":[{"name":"fleet:queued"}],"body":"Part of epic #174 (Phase D). [opus] Blocked by: #101.\n"}',
    # Plain form, referenced issue CLOSED → pass.
    "2020": r'{"state":"OPEN","labels":[{"name":"fleet:queued"}],"body":"Part of epic #174. Blocked by: #100.\n"}',
    # False-positive guard: prose "not blocked by anything"
    # has no `#N` after the colon → not a blocker → claim succeeds.
    "2021": r'{"state":"OPEN","labels":[{"name":"fleet:queued"}],"body":"## Scope\n\nThis task is not blocked by anything yet.\n"}',
}
ISSUE_INFO_DEFAULT = r'{"state":"OPEN","labels":[],"body":""}'

PR_STATE = {"200": "MERGED", "201": "OPEN", "202": "CLOSED"}

verb = " ".join(args[:2])
if verb == "issue view":
    if has_jq:
        if issue_num == "125":
            # cross-repo routing probe: CLOSED only when the check is routed
            # to the *game* repo (the referenced repo); OPEN if it is
            # mis-routed to the issue's own (engine) repo.
            emit("CLOSED\n" if repo == "jakildev/irreden" else "OPEN\n")
        else:
            emit(STATE_ONLY.get(issue_num, "OPEN") + "\n")
        sys.exit(0)
    # fetch_issue_info: full state+labels+body for the target issue.
    emit(ISSUE_INFO.get(issue_num, ISSUE_INFO_DEFAULT))
    sys.exit(0)
if verb == "pr view":
    number = (pr_url or issue_num).rsplit("/", 1)[-1]
    emit(PR_STATE.get(number, "OPEN") + "\n")
    sys.exit(0)
if verb == "pr list":
    # Open-PR sanity check after the blocker gate clears. The caller passes
    # `--jq ".[] | select(...) | .number" | head -1`; an empty array
    # filtered by that jq produces no output, so emit nothing.
    sys.exit(0)
if args[:1] == ["api"]:
    # Cross-host claim-label acquire - echo back the requested label so the
    # lex-min tie-break sees us as the sole holder.
    label = ""
    i = 0
    while i < len(args):
        if args[i] == "-f" and i + 1 < len(args):
            i += 1
            if args[i].startswith("labels[]="):
                label = args[i][len("labels[]="):]
        i += 1
    if label:
        emit('[{"name":"%s"}]\n' % label)
    else:
        emit('[[{"name":"%s"}]]\n' % os.environ.get("FLEET_CLAIM_CANDIDATE", ""))
    sys.exit(0)
sys.exit(0)
GHSTUB
chmod +x "$STUB_DIR/gh"
# Native-Windows twin: fleet-claim's Blocked-by resolvers reach `gh` from
# PYTHON (subprocess), which resolves it through shutil.which() and finds this
# `.bat` (a bare extensionless script is skipped). Inert on POSIX hosts.
# See scripts/fleet/CLAUDE.md's native-Windows PATHEXT rule.
cat > "$STUB_DIR/gh.bat" <<'BATEOF'
@echo off
python3 "%~dp0gh" %*
BATEOF

export PATH="$STUB_DIR:$PATH"

release_quiet() {
    "$FLEET_CLAIM" release "$1" >/dev/null 2>&1 || true
}

# --- T1: parenthetical PR ref — #issue CLOSED + #pr MERGED ------------------
echo "T1: parenthetical PR ref — #issue CLOSED + #pr MERGED → claim succeeds"
actual=0; "$FLEET_CLAIM" claim 2001 test-agent 2>/dev/null || actual=$?
assert_exit "$actual" 0 "#100 CLOSED + #200 MERGED via parenthetical → exit 0"
release_quiet 2001

# --- T2: same shape but the PR is still OPEN → fail -------------------------
echo "T2: parenthetical PR ref — #pr still OPEN → claim fails"
actual=0; "$FLEET_CLAIM" claim 2002 test-agent 2>/dev/null || actual=$?
assert_exit "$actual" 1 "#100 CLOSED + #201 OPEN via parenthetical → exit 1"

# --- T3: (none — commentary) → pass ----------------------------------------
echo "T3: '(none — commentary)' → claim succeeds"
actual=0; "$FLEET_CLAIM" claim 2003 test-agent 2>/dev/null || actual=$?
assert_exit "$actual" 0 "(none …) form bypasses gate → exit 0"
release_quiet 2003

# --- T4: PR URL form, MERGED → pass ----------------------------------------
echo "T4: PR-URL form, MERGED → claim succeeds"
actual=0; "$FLEET_CLAIM" claim 2004 test-agent 2>/dev/null || actual=$?
assert_exit "$actual" 0 "PR-URL MERGED → exit 0"
release_quiet 2004

# --- T5: PR URL form, OPEN → fail ------------------------------------------
echo "T5: PR-URL form, OPEN → claim fails"
actual=0; "$FLEET_CLAIM" claim 2005 test-agent 2>/dev/null || actual=$?
assert_exit "$actual" 1 "PR-URL OPEN → exit 1"

# --- T5b: PR URL form, CLOSED without merge → fail -------------------------
echo "T5b: PR-URL form, CLOSED without merge → claim fails"
actual=0; "$FLEET_CLAIM" claim 2022 test-agent 2>/dev/null || actual=$?
assert_exit "$actual" 1 "PR-URL CLOSED without merge → exit 1"

# --- T6: bare `#N` issue OPEN → fail ----------------------------------------
echo "T6: bare '#N' issue OPEN → claim fails"
actual=0; "$FLEET_CLAIM" claim 2006 test-agent 2>/dev/null || actual=$?
assert_exit "$actual" 1 "#101 OPEN → exit 1"

# --- T7: no Blocked-by line → pass -----------------------------------------
echo "T7: no Blocked-by line → claim succeeds"
actual=0; "$FLEET_CLAIM" claim 2007 test-agent 2>/dev/null || actual=$?
assert_exit "$actual" 0 "missing field bypasses gate → exit 0"
release_quiet 2007

# --- T8: parenthetical PR ref — PR CLOSED-abandoned → claim succeeds --------
# Documents current behavior — the `#N` branch accepts CLOSED|MERGED, so an
# abandoned PR matched via `#N` passes the gate. Contrast with the URL form
# (T5, OPEN → fail) which only accepts MERGED. If fleet ever needs to reject
# closed-abandoned PR refs uniformly, the #N branch in check_blockers needs
# to detect PR-state CLOSED separately from issue-state CLOSED — at which
# point this test's expectation flips to exit 1.
echo "T8: parenthetical PR ref — PR CLOSED-abandoned → claim succeeds (current behavior)"
actual=0; "$FLEET_CLAIM" claim 2008 test-agent 2>/dev/null || actual=$?
assert_exit "$actual" 0 "#100 CLOSED + #202 CLOSED (abandoned) → exit 0"
release_quiet 2008

# --- T9: header-prose blocker — `## Blocked on #N`, #N OPEN → fail ----------
# A dependency declared only in a `Blocked on #N` header (no **Blocked by:**
# field) is read by the gate too, not just the field form.
echo "T9: header prose '## Blocked on #101' — #101 OPEN → claim fails"
actual=0; "$FLEET_CLAIM" claim 2009 test-agent 2>/dev/null || actual=$?
assert_exit "$actual" 1 "header-prose #101 OPEN → exit 1"

# --- T10: header-prose blocker — referenced issue CLOSED → pass --------------
echo "T10: header prose 'Blocked on #100' — #100 CLOSED → claim succeeds"
actual=0; "$FLEET_CLAIM" claim 2010 test-agent 2>/dev/null || actual=$?
assert_exit "$actual" 0 "header-prose #100 CLOSED → exit 0"
release_quiet 2010

# --- T11: header prose without a #N / PR ref → not a gate --------------------
echo "T11: header prose 'Blocked on the redesign' (no ref) → claim succeeds"
actual=0; "$FLEET_CLAIM" claim 2011 test-agent 2>/dev/null || actual=$?
assert_exit "$actual" 0 "ref-less header prose bypasses gate → exit 0"
release_quiet 2011

# --- T12: canonical field wins over header prose ----------------------------
echo "T12: field '(none)' overrides a '## Blocked on #101' header → succeeds"
actual=0; "$FLEET_CLAIM" claim 2012 test-agent 2>/dev/null || actual=$?
assert_exit "$actual" 0 "field (none) takes precedence over header prose → exit 0"
release_quiet 2012

# --- T13: multi-line **Blocked by:** — a later ref still OPEN → fail --------
echo "T13: two **Blocked by:** lines (#100 CLOSED, #101 OPEN) → claim fails"
actual=0; "$FLEET_CLAIM" claim 2013 test-agent 2>/dev/null || actual=$?
assert_exit "$actual" 1 "multi-line blocked-by, #101 OPEN → exit 1"

# --- T14: inline-bold form — referenced issue OPEN → fail -------------------
echo "T14: inline-bold '**Blocked by: #101 (Phase 2)**' — #101 OPEN → claim fails"
actual=0; "$FLEET_CLAIM" claim 2014 test-agent 2>/dev/null || actual=$?
assert_exit "$actual" 1 "inline-bold #101 OPEN → exit 1"

# --- T15: inline-bold form — referenced issue CLOSED → pass -----------------
echo "T15: inline-bold '**Blocked by: #100 (Phase 2)**' — #100 CLOSED → claim succeeds"
actual=0; "$FLEET_CLAIM" claim 2015 test-agent 2>/dev/null || actual=$?
assert_exit "$actual" 0 "inline-bold #100 CLOSED → exit 0"
release_quiet 2015

# --- T16: inline-bold with no #N/PR ref — not a gate -------------------------
echo "T16: inline-bold 'Blocked by: the redesign' (no ref) → claim succeeds"
actual=0; "$FLEET_CLAIM" claim 2016 test-agent 2>/dev/null || actual=$?
assert_exit "$actual" 0 "inline-bold no-ref bypasses gate → exit 0"
release_quiet 2016

# --- T17: cross-repo blocker, CLOSED in the referenced repo → succeeds ------
# The same issue number reads OPEN in engine but CLOSED in game; the gate
# must route the owner-qualified ref to game (where it's CLOSED) and let the
# claim through.
echo "T17: cross-repo 'jakildev/irreden#125' CLOSED in game → claim succeeds"
actual=0; "$FLEET_CLAIM" claim 2017 test-agent 2>/dev/null || actual=$?
assert_exit "$actual" 0 "cross-repo game#125 CLOSED → exit 0 (routed to game, not engine)"
release_quiet 2017

# --- T18: cross-repo blocker, still OPEN in the referenced repo → fails -----
echo "T18: cross-repo 'irreden#126' OPEN in game → claim fails"
actual=0; "$FLEET_CLAIM" claim 2018 test-agent 2>/dev/null || actual=$?
assert_exit "$actual" 1 "cross-repo game#126 OPEN → exit 1"

# --- T19: plain mid-line `Blocked by: #N` — referenced issue OPEN → fail ----
# A non-bold `Blocked by: #N` declaration is gated same as the bold form; the
# leading epic ref in the prose must NOT be mistaken for it.
echo "T19: plain 'Blocked by: #101' (epic #174 prose) — #101 OPEN → claim fails"
actual=0; "$FLEET_CLAIM" claim 2019 test-agent 2>/dev/null || actual=$?
assert_exit "$actual" 1 "plain-form #101 OPEN → exit 1 (epic #174 not mistaken for blocker)"

# --- T20: plain form — referenced issue CLOSED → pass ------------------------
echo "T20: plain 'Blocked by: #100' — #100 CLOSED → claim succeeds"
actual=0; "$FLEET_CLAIM" claim 2020 test-agent 2>/dev/null || actual=$?
assert_exit "$actual" 0 "plain-form #100 CLOSED → exit 0"
release_quiet 2020

# --- T21: false-positive guard — "not blocked by anything" → pass -----------
echo "T21: prose 'not blocked by anything yet' (no #N) → claim succeeds"
actual=0; "$FLEET_CLAIM" claim 2021 test-agent 2>/dev/null || actual=$?
assert_exit "$actual" 0 "ref-less 'blocked by' prose bypasses gate → exit 0"
release_quiet 2021

echo ""
echo "PASS: $PASS  FAIL: $FAIL"
[[ "$FAIL" -eq 0 ]]
