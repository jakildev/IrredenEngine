#!/usr/bin/env bash
# Tests for fleet-dispatch-wrap's session-id persistence + interrupted-session
# resume (workers + reviewers).
#
# The launch DECISION (fresh vs resume, argv, stored-config) is checked via the
# FLEET_DISPATCH_PRINT_LAUNCH inspection hook (prints + exits before claude).
# The sidecar LIFECYCLE (keep on in-flight / clear on done / clear on failed
# resume) is checked by running to completion with stubbed claude/git/fleet-claim.
#
# Covers:
#   - fresh dispatch: --session-id, /role-<role>, sidecar written (worker,
#     reviewer, merger — every dispatched role resumes now)
#   - non-dispatched role (queue-manager): no sidecar, no resume
#   - resume: sidecar present -> --resume <id> with the STORED model/effort
#     (not the dispatcher-passed class)
#   - cleanup: failed resume keeps the sidecar ONCE (transient tolerance),
#     clears on the second straight failure; quota (rc=2) and short-window
#     429 failures never count against the streak
#   - cleanup: in-flight work (claude/* branch + dirty) keeps the sidecar
#   - cleanup: in-flight work (reservation present, branch otherwise clean) keeps the sidecar
#   - cleanup: in-flight work (claude/* branch, clean but ahead of master) keeps the sidecar
#   - cleanup: finished/no-op (clean master) clears the sidecar
#   - planning assignment (#2197): 7th plan= arg -> FLEET_PLAN_ISSUE export;
#     absent/bare arg stays unset; a resume releases the pre-claim instead
#   - hermeticity (#2836): the suite is immune to an inherited FLEET_PLAN_ISSUE
#     (every planning-assigned worker iteration exports it, so any such
#     iteration that runs this suite — e.g. while build-verifying a
#     scripts/fleet PR — must not see a spurious 2-assertion red)

set -uo pipefail
SCRIPT_DIR=$(cd "$(dirname "$0")/.." && pwd)
source "$(dirname "$0")/lib_preflight.sh"
WRAP="$SCRIPT_DIR/fleet-dispatch-wrap"
[[ -x "$WRAP" ]] || { echo "test setup: $WRAP not found"; exit 1; }

# Hermeticity (#2836): fleet-dispatch-wrap only ever SETS FLEET_PLAN_ISSUE
# (from its own 7th argv, when present) — it never unsets it when the arg is
# absent. Every planning-assigned dispatch exports FLEET_PLAN_ISSUE, so this
# suite's own process inherits it whenever it's run from inside such an
# iteration, and T9's two absent-arg cases below would then see it leak
# straight through into the wrap's launch. Scrub it once here, in this
# process's own environment, before any dispatch call runs — the fix belongs
# in the test harness, not the wrap, which has no way to distinguish "caller
# wants no plan" from "caller's shell happens to have one lying around".
# (Audited siblings: FLEET_ROLE_MODEL is always freshly exported by the wrap
# itself regardless of argv, so it has no leak path; FLEET_ASSIGNED_WORKTREE
# is not read by fleet-dispatch-wrap at all.)
unset FLEET_PLAN_ISSUE

# PASS/FAIL, ok/bad and `summarize` come from the shared helper: its
# "passed: N  failed: M" line is what fleet-positive-control scores, and the
# hand-rolled "PASS: n FAIL: m" tally this replaces read as an aborted run.
# shellcheck source=scripts/fleet/tests/lib_assert.sh
source "$(dirname "$0")/lib_assert.sh"
TMPROOT=""; cleanup(){ [[ -n "$TMPROOT" && -d "$TMPROOT" ]] && rm -rf "$TMPROOT"; }
trap cleanup EXIT
TMPROOT=$(mktemp -d)

export FLEET_SESSIONS_DIR="$TMPROOT/sessions"
export FLEET_STATE_DIR="$TMPROOT/state"
mkdir -p "$FLEET_SESSIONS_DIR" "$FLEET_STATE_DIR"

# --- stubs ---------------------------------------------------------------
BIN="$TMPROOT/bin"; mkdir -p "$BIN"
export CLAUDE_ARGV_LOG="$TMPROOT/claude-argv.log"; : > "$CLAUDE_ARGV_LOG"
cat > "$BIN/claude" <<'EOF'
#!/usr/bin/env bash
printf '%s\n' "$*" >> "$CLAUDE_ARGV_LOG"
exit "${STUB_CLAUDE_RC:-0}"
EOF
cat > "$BIN/fleet-claude-stream" <<'EOF'
#!/usr/bin/env bash
# STUB_TOUCH_THROTTLE simulates the real stream formatter spotting a
# short-window 429 and touching the wrap-exported flag file.
[[ -n "${STUB_TOUCH_THROTTLE:-}" && -n "${FLEET_THROTTLE_FLAG:-}" ]] && touch "$FLEET_THROTTLE_FLAG"
cat >/dev/null 2>&1 || true
EOF
cat > "$BIN/fleet-claim" <<'EOF'
#!/usr/bin/env bash
[[ -n "${FLEET_CLAIM_LOG:-}" ]] && printf '%s\n' "$*" >> "$FLEET_CLAIM_LOG"
[[ "$1" == "reservation-of" ]] && { [[ -n "${STUB_RESERVATION:-}" ]] && echo "$STUB_RESERVATION"; }
exit 0
EOF
cat > "$BIN/git" <<'EOF'
#!/usr/bin/env bash
case "$*" in
  *"rev-parse --abbrev-ref HEAD"*) echo "${STUB_BRANCH:-master}" ;;
  *"rev-parse --verify --quiet refs/remotes/origin/"*) exit "${STUB_REMOTE_REF_RC:-1}" ;;
  *"status --porcelain"*)
    [[ -n "${STUB_DIRTY:-}" ]] && echo " M f"
    [[ -n "${STUB_UNTRACKED:-}" ]] && echo "?? junk/"
    ;;
  *"rev-list --count origin/master..HEAD"*) echo "${STUB_AHEAD:-0}" ;;
  *"rev-list --count"*) echo "${STUB_AHEAD_OWN:-0}" ;;
  *) : ;;
esac
exit 0
EOF
cat > "$BIN/tmux" <<'EOF'
#!/usr/bin/env bash
exit 0
EOF
chmod +x "$BIN"/*
export PATH="$BIN:$PATH"

# A worktree dir to run from (its basename is the worktree name / sidecar key).
WT="$TMPROOT/worker-1"; mkdir -p "$WT"
SIDECAR="$FLEET_SESSIONS_DIR/worker-1.session.json"

run_wrap() {  # runs dispatch-wrap from the worktree dir; args: model effort role [fallback] [mode]
  ( cd "$WT" && "$WRAP" "pane-3" "$1" "$2" "$3" "${4:-}" "${5:-live}" 2>>"$TMPROOT/stderr.log" )
}

# =========================================================================
echo "T1: fresh worker dispatch — --session-id, /role-worker, sidecar written"
rm -f "$SIDECAR"
out=$(cd "$WT" && FLEET_DISPATCH_PRINT_LAUNCH=1 "$WRAP" pane-3 "claude-opus-4-8[1m]" xhigh worker "" live 2>/dev/null)
[[ "$out" == resumed=0* ]] && ok "fresh: resumed=0" || bad "fresh resumed flag: $out"
[[ "$out" == *"--session-id "* ]] && ok "fresh: passes --session-id" || bad "fresh missing --session-id: $out"
[[ "$out" == *"/role-worker live"* ]] && ok "fresh: runs /role-worker" || bad "fresh prompt: $out"
[[ -f "$SIDECAR" ]] && ok "fresh: sidecar written" || bad "fresh: sidecar NOT written"
python3 -c "import json,sys;d=json.load(open(sys.argv[1]));assert d['role']=='worker' and d['model']=='claude-opus-4-8[1m]' and d['effort']=='xhigh'" "$SIDECAR" 2>/dev/null \
  && ok "fresh: sidecar records role/model/effort" || bad "fresh: sidecar fields wrong"

echo "T2: merger is resume-eligible — fresh dispatch writes a sidecar"
rm -f "$FLEET_SESSIONS_DIR/worker-1.session.json"
out=$(cd "$WT" && FLEET_DISPATCH_PRINT_LAUNCH=1 "$WRAP" pane-3 sonnet high merger "" live 2>/dev/null)
[[ "$out" == resumed=0* ]] && ok "merger: resumed=0" || bad "merger resumed: $out"
python3 -c "import json,sys;d=json.load(open(sys.argv[1]));assert d['role']=='merger'" "$SIDECAR" 2>/dev/null \
  && ok "merger: sidecar written (crash recovery)" || bad "merger: no/wrong sidecar"
rm -f "$SIDECAR"

echo "T2b: non-dispatched role (queue-manager) — no sidecar, no resume"
out=$(cd "$WT" && FLEET_DISPATCH_PRINT_LAUNCH=1 "$WRAP" pane-3 sonnet high queue-manager "" live 2>/dev/null)
[[ "$out" == resumed=0* ]] && ok "queue-manager: resumed=0" || bad "queue-manager resumed: $out"
[[ ! -f "$SIDECAR" ]] && ok "queue-manager: no sidecar written" || bad "queue-manager wrote a sidecar"

echo "T3: resume — sidecar present -> --resume <id> with STORED model/effort"
printf '{"session_id":"SID-123","role":"worker","model":"claude-opus-4-8[1m]","effort":"xhigh","created_epoch":1}\n' > "$SIDECAR"
# dispatcher passes a DIFFERENT class (sonnet/high) — resume must ignore it.
out=$(cd "$WT" && FLEET_DISPATCH_PRINT_LAUNCH=1 "$WRAP" pane-3 sonnet high worker "" live 2>/dev/null)
[[ "$out" == resumed=1* ]] && ok "resume: resumed=1" || bad "resume flag: $out"
[[ "$out" == *"--resume SID-123"* ]] && ok "resume: --resume <stored id>" || bad "resume id: $out"
[[ "$out" == *"--model claude-opus-4-8[1m] --effort xhigh"* ]] && ok "resume: uses STORED model/effort" || bad "resume config: $out"
[[ "$out" == *"--session-id"* ]] && bad "resume: should NOT pass --session-id" || ok "resume: no --session-id"

echo "T3b: role-mismatched sidecar (pool pane) -> fresh launch, not a cross-role resume"
# Pool panes host every transient role: a hard-killed reviewer's sidecar
# must not be resumed by a worker dispatch landing on the same worktree.
printf '{"session_id":"SID-REV","role":"sonnet-reviewer","model":"sonnet","effort":"high","created_epoch":1}\n' > "$SIDECAR"
out=$(cd "$WT" && FLEET_DISPATCH_PRINT_LAUNCH=1 "$WRAP" pane-3 sonnet high worker "" live 2>/dev/null)
[[ "$out" == resumed=0* ]] && ok "role mismatch: resumed=0" || bad "role mismatch resumed flag: $out"
[[ "$out" == *"--resume"* ]] && bad "role mismatch: must not --resume the foreign session" || ok "role mismatch: no --resume"
rm -f "$SIDECAR"

echo "T4: reviewer fresh dispatch resumable + writes sidecar"
rm -f "$FLEET_SESSIONS_DIR/opus-reviewer.session.json"
WT2="$TMPROOT/opus-reviewer"; mkdir -p "$WT2"
out=$(cd "$WT2" && FLEET_DISPATCH_PRINT_LAUNCH=1 "$WRAP" pane-7 "claude-opus-4-8[1m]" xhigh opus-reviewer "" review-only 2>/dev/null)
[[ "$out" == *"/role-opus-reviewer review-only"* ]] && ok "reviewer: /role-opus-reviewer" || bad "reviewer prompt: $out"
[[ -f "$FLEET_SESSIONS_DIR/opus-reviewer.session.json" ]] && ok "reviewer: sidecar written" || bad "reviewer: no sidecar"

# --- cleanup lifecycle (full run, stubbed claude) ------------------------
echo "T5: cleanup — one failed resume is tolerated (transient), the second clears"
printf '{"session_id":"SID-9","role":"worker","model":"sonnet","effort":"high","created_epoch":1}\n' > "$SIDECAR"
STUB_CLAUDE_RC=1 run_wrap sonnet high worker
[[ -f "$SIDECAR" ]] && ok "1st failed resume kept the sidecar (one retry)" || bad "1st failed resume cleared the sidecar"
python3 -c "import json,sys;d=json.load(open(sys.argv[1]));assert d.get('resume_failures')==1" "$SIDECAR" 2>/dev/null \
  && ok "failure streak recorded (resume_failures=1)" || bad "failure streak missing: $(cat "$SIDECAR" 2>/dev/null)"
STUB_CLAUDE_RC=1 run_wrap sonnet high worker
[[ ! -f "$SIDECAR" ]] && ok "2nd failed resume cleared the sidecar (poisoned session)" || bad "2nd failed resume left the sidecar"

echo "T5b: quota exit (rc=2) on a resume keeps the sidecar untouched"
printf '{"session_id":"SID-Q","role":"worker","model":"sonnet","effort":"high","created_epoch":1}\n' > "$SIDECAR"
STUB_CLAUDE_RC=2 run_wrap sonnet high worker
[[ -f "$SIDECAR" ]] && ok "quota failure kept the sidecar" || bad "quota failure cleared the sidecar"
python3 -c "import json,sys;d=json.load(open(sys.argv[1]));assert not d.get('resume_failures')" "$SIDECAR" 2>/dev/null \
  && ok "quota failure did not count against the streak" || bad "quota failure bumped resume_failures"

echo "T5c: short-window 429 (throttle flag) on a resume keeps the sidecar untouched"
printf '{"session_id":"SID-T","role":"worker","model":"sonnet","effort":"high","created_epoch":1}\n' > "$SIDECAR"
STUB_TOUCH_THROTTLE=1 STUB_CLAUDE_RC=1 run_wrap sonnet high worker
[[ -f "$SIDECAR" ]] && ok "throttled failure kept the sidecar" || bad "throttled failure cleared the sidecar"
python3 -c "import json,sys;d=json.load(open(sys.argv[1]));assert not d.get('resume_failures')" "$SIDECAR" 2>/dev/null \
  && ok "throttled failure did not count against the streak" || bad "throttled failure bumped resume_failures"
rm -f "$SIDECAR"

echo "T6: cleanup — in-flight (claude/* branch + dirty) keeps the sidecar"
rm -f "$SIDECAR"
STUB_BRANCH="claude/123-foo" STUB_DIRTY=1 STUB_CLAUDE_RC=0 run_wrap "claude-opus-4-8[1m]" xhigh worker
[[ -f "$SIDECAR" ]] && ok "in-flight worker kept the sidecar (resumes next dispatch)" || bad "in-flight: sidecar wrongly cleared"

echo "T6b: cleanup — in-flight (reservation present, branch otherwise clean) keeps the sidecar"
rm -f "$SIDECAR"
STUB_RESERVATION="163" STUB_BRANCH="master" STUB_DIRTY="" STUB_AHEAD=0 STUB_CLAUDE_RC=0 run_wrap "claude-opus-4-8[1m]" xhigh worker
[[ -f "$SIDECAR" ]] && ok "reservation-only in-flight kept the sidecar" || bad "reservation-only: sidecar wrongly cleared"

echo "T6c: cleanup — in-flight (claude/* branch, clean but ahead of master) keeps the sidecar"
rm -f "$SIDECAR"
STUB_BRANCH="claude/123-foo" STUB_DIRTY="" STUB_AHEAD=2 STUB_CLAUDE_RC=0 run_wrap "claude-opus-4-8[1m]" xhigh worker
[[ -f "$SIDECAR" ]] && ok "ahead-only in-flight kept the sidecar" || bad "ahead-only: sidecar wrongly cleared"

echo "T7: cleanup — finished/no-op (clean master) clears the sidecar"
rm -f "$SIDECAR"
STUB_BRANCH="master" STUB_DIRTY="" STUB_CLAUDE_RC=0 run_wrap "claude-opus-4-8[1m]" xhigh worker
[[ ! -f "$SIDECAR" ]] && ok "done/no-op worker cleared the sidecar (fresh next)" || bad "done: sidecar wrongly kept"

# --- dispatch targets: 7th arg target=<kind>:<repo>:<N>[:x] ------------------
echo "T8: the legacy plan=<repo>:<N> spelling (#2197) is the plan target"
# An older dispatcher mid rolling-upgrade may still pass it; it parses as
# target=plan:<repo>:<N>, so T10b-T10d below cover it through the one grammar.
rm -f "$SIDECAR"
out=$(cd "$WT" && FLEET_DISPATCH_PRINT_LAUNCH=1 "$WRAP" pane-3 "claude-fable-5[1m]" xhigh worker "" live "plan=engine:2197" 2>/dev/null)
[[ "$out" == *" target=plan:engine:2197 reason=plan engine#2197 plan=engine:2197 prompt="* ]] \
  && ok "plan=engine:2197 -> target plan:engine:2197 + FLEET_PLAN_ISSUE" || bad "legacy plan= alias: $out"
rm -f "$SIDECAR"
out=$(cd "$WT" && FLEET_DISPATCH_PRINT_LAUNCH=1 "$WRAP" pane-3 sonnet high worker "" live 2>/dev/null)
[[ "$out" == *" target= reason= plan= prompt="* ]] && ok "absent 7th arg: nothing exported" || bad "absent-arg leak: $out"
rm -f "$SIDECAR"
out=$(cd "$WT" && FLEET_DISPATCH_PRINT_LAUNCH=1 "$WRAP" pane-3 sonnet high worker "" live "plan=" 2>/dev/null)
[[ "$out" == *" target= reason= plan= prompt="* ]] && ok "bare plan=: not exported (dual-spelling guard)" || bad "bare plan= leaked: $out"
rm -f "$SIDECAR"

echo "T10b: target= exports FLEET_DISPATCH_TARGET and its parts; a plan target keeps FLEET_PLAN_ISSUE"
rm -f "$SIDECAR"
out=$(cd "$WT" && FLEET_DISPATCH_PRINT_LAUNCH=1 "$WRAP" pane-3 sonnet high worker "" live "target=task:engine:1969" 2>/dev/null)
[[ "$out" == *" target=task:engine:1969 reason=task engine#1969 plan= prompt="* ]] \
  && ok "task target: FLEET_DISPATCH_TARGET + REASON exported, no FLEET_PLAN_ISSUE" \
  || bad "task target export: $out"
rm -f "$SIDECAR"
out=$(cd "$WT" && FLEET_DISPATCH_PRINT_LAUNCH=1 "$WRAP" pane-3 "claude-fable-5[1m]" xhigh worker "" live "target=plan:game:7" 2>/dev/null)
[[ "$out" == *" target=plan:game:7 reason=plan game#7 plan=game:7 prompt="* ]] \
  && ok "plan target: FLEET_PLAN_ISSUE=game:7 also exported (the #2197 spelling)" \
  || bad "plan target export: $out"
rm -f "$SIDECAR"
out=$(cd "$WT" && FLEET_DISPATCH_PRINT_LAUNCH=1 "$WRAP" pane-3 sonnet high sonnet-reviewer "" live "target=review:game:12" 2>/dev/null)
[[ "$out" == *" target=review:game:12 reason=review game#12 plan= prompt="* ]] \
  && ok "review target on a reviewer dispatch" || bad "review target export: $out"
rm -f "$SIDECAR"
# The parts are exported individually too — the role docs key on
# FLEET_DISPATCH_KIND. The wrap exports into the claude process, so assert
# through a claude stub that prints its environment, then restore the plain
# stub for the suites below.
cat > "$BIN/claude" <<'EOF'
#!/usr/bin/env bash
printf '%s\n' "$*" >> "$CLAUDE_ARGV_LOG"
printf 'ENV kind=%s repo=%s num=%s target=%s reason=%s\n' \
  "${FLEET_DISPATCH_KIND:-}" "${FLEET_DISPATCH_REPO:-}" "${FLEET_DISPATCH_NUMBER:-}" \
  "${FLEET_DISPATCH_TARGET:-}" "${FLEET_DISPATCH_REASON:-}" >> "$CLAUDE_ARGV_LOG"
exit "${STUB_CLAUDE_RC:-0}"
EOF
chmod +x "$BIN/claude"
: > "$CLAUDE_ARGV_LOG"
(cd "$WT" && "$WRAP" pane-3 sonnet high worker "" live "target=stack:engine:344:397" >/dev/null 2>&1)
grep -q '^ENV kind=stack repo=engine num=344 target=stack:engine:344:397 reason=stack engine#344$' "$CLAUDE_ARGV_LOG" \
  && ok "stack target: KIND/REPO/NUMBER parts reach the claude process" \
  || bad "stack target parts: $(grep '^ENV' "$CLAUDE_ARGV_LOG")"
cat > "$BIN/claude" <<'EOF'
#!/usr/bin/env bash
printf '%s\n' "$*" >> "$CLAUDE_ARGV_LOG"
exit "${STUB_CLAUDE_RC:-0}"
EOF
chmod +x "$BIN/claude"
rm -f "$SIDECAR"

echo "T10c: a malformed target exports nothing (dual-spelling guard)"
for bad_arg in "target=" "target=bogus:engine:1" "target=task:engine:abc" "target=task:other:1" "target=task" "target=:engine:1"; do
  rm -f "$SIDECAR"
  out=$(cd "$WT" && FLEET_DISPATCH_PRINT_LAUNCH=1 "$WRAP" pane-3 sonnet high worker "" live "$bad_arg" 2>/dev/null)
  [[ "$out" == *" target= reason= plan= prompt="* ]] && ok "'$bad_arg' -> nothing exported" || bad "'$bad_arg' leaked: $out"
done
rm -f "$SIDECAR"

echo "T10d: a resume releases the target through its lane's own release arm"
printf '{"session_id":"SID-78","role":"worker","model":"sonnet","effort":"high","created_epoch":1}\n' > "$SIDECAR"
export FLEET_CLAIM_LOG="$TMPROOT/wrap-claim.log"; : > "$FLEET_CLAIM_LOG"
out=$(cd "$WT" && FLEET_DISPATCH_PRINT_LAUNCH=1 "$WRAP" pane-3 sonnet high worker "" live "target=task:game:7" 2>/dev/null)
[[ "$out" == resumed=1* && "$out" == *" target= reason= plan= prompt="* ]] && ok "resume: target dropped from env" || bad "resume target handling: $out"
grep -q -- '^--repo game release 7$' "$FLEET_CLAIM_LOG" \
  && ok "resume: task target released with a plain release (drops lock, label, reservation)" \
  || bad "resume: no task release call: $(cat "$FLEET_CLAIM_LOG")"
printf '{"session_id":"SID-79","role":"sonnet-reviewer","model":"sonnet","effort":"high","created_epoch":1}\n' > "$SIDECAR"
: > "$FLEET_CLAIM_LOG"
out=$(cd "$WT" && FLEET_DISPATCH_PRINT_LAUNCH=1 "$WRAP" pane-3 sonnet high sonnet-reviewer "" live "target=review:engine:3074" 2>/dev/null)
grep -q -- '^review-release 3074 worker-1$' "$FLEET_CLAIM_LOG" \
  && ok "resume: review target released via review-release under the basename" \
  || bad "resume: no review-release call: $(cat "$FLEET_CLAIM_LOG")"
unset FLEET_CLAIM_LOG
rm -f "$SIDECAR"

# --- hermeticity regression lock (#2836) -------------------------------------
# T9 above only proves its absent-arg cases are clean in THIS process,
# which is already running after the setup scrub. That alone doesn't lock the
# scrub against removal — a future edit could delete the `unset
# FLEET_PLAN_ISSUE` above and every case here would still pass, because none
# of them re-introduce an ambient value once the process starts. Reproduce the
# actual bug report's repro command instead: re-invoke this whole suite as a
# child process with FLEET_PLAN_ISSUE ambiently exported (exactly
# `FLEET_PLAN_ISSUE=engine:9999 bash test_dispatch_wrap_session.sh`, the
# acceptance criterion's own repro form) and assert the child still exits 0.
# The child hits the setup scrub before its own T9 run, so this fails iff
# that scrub regresses. FLEET_TEST_SELFCHECK guards against a second level of
# recursion — the child must not spawn a grandchild.
if [[ -z "${FLEET_TEST_SELFCHECK:-}" ]]; then
    echo "T10b: suite is hermetic against an ambient FLEET_PLAN_ISSUE (#2836 repro, regression lock)"
    if FLEET_TEST_SELFCHECK=1 FLEET_PLAN_ISSUE="engine:9999" bash "$0" >"$TMPROOT/selfcheck.log" 2>&1; then
        ok "suite exits 0 when launched with FLEET_PLAN_ISSUE ambiently set"
    else
        bad "suite fails under an ambient FLEET_PLAN_ISSUE (setup scrub regressed): $(tail -5 "$TMPROOT/selfcheck.log")"
    fi
fi

# --- in-flight false positives + resume-loop breaker (worker-2, 07-09→07-14) --
echo "T11: scratch branch is never in-flight — sidecar cleared even when dirty"
rm -f "$SIDECAR"
STUB_BRANCH="claude/worker-1-scratch" STUB_DIRTY=1 STUB_CLAUDE_RC=0 run_wrap "claude-opus-4-8[1m]" xhigh worker
[[ ! -f "$SIDECAR" ]] && ok "scratch branch cleared the sidecar" || bad "scratch branch kept the sidecar"

echo "T12: untracked-only junk is not in-flight — sidecar cleared"
rm -f "$SIDECAR"
STUB_BRANCH="claude/123-foo" STUB_UNTRACKED=1 STUB_CLAUDE_RC=0 run_wrap "claude-opus-4-8[1m]" xhigh worker
[[ ! -f "$SIDECAR" ]] && ok "untracked-only cleared the sidecar" || bad "untracked junk kept the sidecar"

echo "T13: fully-pushed branch (own remote ref current) is not in-flight"
rm -f "$SIDECAR"
# Remote ref exists (RC=0); 0 unpushed vs own ref; 5 "ahead" of master (the
# squash-merge illusion) — must clear.
STUB_BRANCH="claude/123-foo" STUB_REMOTE_REF_RC=0 STUB_AHEAD=5 STUB_AHEAD_OWN=0 STUB_CLAUDE_RC=0 run_wrap "claude-opus-4-8[1m]" xhigh worker
[[ ! -f "$SIDECAR" ]] && ok "pushed branch cleared the sidecar (no squash-merge pin)" || bad "pushed branch kept the sidecar"

echo "T14: resume-loop breaker — clean resumed exit twice while in-flight clears"
printf '{"session_id":"SID-LOOP","role":"worker","model":"sonnet","effort":"high","created_epoch":1}\n' > "$SIDECAR"
# 1st resumed clean exit, genuinely in-flight (task branch + tracked dirty): kept, resumes=1.
STUB_BRANCH="claude/123-foo" STUB_DIRTY=1 STUB_CLAUDE_RC=0 run_wrap sonnet high worker
[[ -f "$SIDECAR" ]] && ok "1st clean resume kept the sidecar" || bad "1st clean resume cleared too early"
python3 -c "import json,sys;d=json.load(open(sys.argv[1]));assert d.get('resumes')==1" "$SIDECAR" 2>/dev/null \
  && ok "resume counter recorded (resumes=1)" || bad "resume counter missing/wrong: $(cat "$SIDECAR" 2>/dev/null)"
# 2nd resumed clean exit, still in-flight: breaker fires, sidecar cleared.
STUB_BRANCH="claude/123-foo" STUB_DIRTY=1 STUB_CLAUDE_RC=0 run_wrap sonnet high worker
[[ ! -f "$SIDECAR" ]] && ok "2nd clean resume cleared the sidecar (loop broken)" || bad "2nd clean resume kept the sidecar (loop!)"

echo "T15: a clean resumed exit resets the transient-failure streak"
printf '{"session_id":"SID-OK","role":"worker","model":"sonnet","effort":"high","created_epoch":1,"resume_failures":1}\n' > "$SIDECAR"
STUB_BRANCH="claude/123-foo" STUB_DIRTY=1 STUB_CLAUDE_RC=0 run_wrap sonnet high worker
python3 -c "import json,sys;d=json.load(open(sys.argv[1]));assert d.get('resume_failures')==0 and d.get('resumes')==1" "$SIDECAR" 2>/dev/null \
  && ok "clean resume zeroed resume_failures (and bumped resumes)" || bad "streak not reset: $(cat "$SIDECAR" 2>/dev/null)"
rm -f "$SIDECAR"

summarize "fleet-dispatch-wrap session tests"
