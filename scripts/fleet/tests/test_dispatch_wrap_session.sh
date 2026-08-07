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

PASS=0; FAIL=0
ok()  { PASS=$((PASS+1)); echo "  ok: $1"; }
bad() { FAIL=$((FAIL+1)); echo "  FAIL: $1"; }
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
python3 -c "import json;d=json.load(open('$SIDECAR'));assert d['role']=='worker' and d['model']=='claude-opus-4-8[1m]' and d['effort']=='xhigh'" 2>/dev/null \
  && ok "fresh: sidecar records role/model/effort" || bad "fresh: sidecar fields wrong"

echo "T2: merger is resume-eligible — fresh dispatch writes a sidecar"
rm -f "$FLEET_SESSIONS_DIR/worker-1.session.json"
out=$(cd "$WT" && FLEET_DISPATCH_PRINT_LAUNCH=1 "$WRAP" pane-3 sonnet high merger "" live 2>/dev/null)
[[ "$out" == resumed=0* ]] && ok "merger: resumed=0" || bad "merger resumed: $out"
python3 -c "import json;d=json.load(open('$SIDECAR'));assert d['role']=='merger'" 2>/dev/null \
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
python3 -c "import json;d=json.load(open('$SIDECAR'));assert d.get('resume_failures')==1" 2>/dev/null \
  && ok "failure streak recorded (resume_failures=1)" || bad "failure streak missing: $(cat "$SIDECAR" 2>/dev/null)"
STUB_CLAUDE_RC=1 run_wrap sonnet high worker
[[ ! -f "$SIDECAR" ]] && ok "2nd failed resume cleared the sidecar (poisoned session)" || bad "2nd failed resume left the sidecar"

echo "T5b: quota exit (rc=2) on a resume keeps the sidecar untouched"
printf '{"session_id":"SID-Q","role":"worker","model":"sonnet","effort":"high","created_epoch":1}\n' > "$SIDECAR"
STUB_CLAUDE_RC=2 run_wrap sonnet high worker
[[ -f "$SIDECAR" ]] && ok "quota failure kept the sidecar" || bad "quota failure cleared the sidecar"
python3 -c "import json;d=json.load(open('$SIDECAR'));assert not d.get('resume_failures')" 2>/dev/null \
  && ok "quota failure did not count against the streak" || bad "quota failure bumped resume_failures"

echo "T5c: short-window 429 (throttle flag) on a resume keeps the sidecar untouched"
printf '{"session_id":"SID-T","role":"worker","model":"sonnet","effort":"high","created_epoch":1}\n' > "$SIDECAR"
STUB_TOUCH_THROTTLE=1 STUB_CLAUDE_RC=1 run_wrap sonnet high worker
[[ -f "$SIDECAR" ]] && ok "throttled failure kept the sidecar" || bad "throttled failure cleared the sidecar"
python3 -c "import json;d=json.load(open('$SIDECAR'));assert not d.get('resume_failures')" 2>/dev/null \
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

# --- planning assignment (#2197): 7th arg -> FLEET_PLAN_ISSUE ---------------
echo "T8: plan=<repo>:<N> 7th arg exports FLEET_PLAN_ISSUE on a fresh dispatch"
rm -f "$SIDECAR"
out=$(cd "$WT" && FLEET_DISPATCH_PRINT_LAUNCH=1 "$WRAP" pane-3 "claude-fable-5[1m]" xhigh worker "" live "plan=engine:2197" 2>/dev/null)
[[ "$out" == *" plan=engine:2197 "* ]] && ok "fresh: FLEET_PLAN_ISSUE=engine:2197" || bad "fresh plan export: $out"
rm -f "$SIDECAR"

echo "T9: absent or bare 'plan=' 7th arg -> FLEET_PLAN_ISSUE stays unset"
rm -f "$SIDECAR"
out=$(cd "$WT" && FLEET_DISPATCH_PRINT_LAUNCH=1 "$WRAP" pane-3 sonnet high worker "" live 2>/dev/null)
[[ "$out" == *" plan= prompt="* ]] && ok "absent arg: no FLEET_PLAN_ISSUE" || bad "absent-arg plan leak: $out"
rm -f "$SIDECAR"
out=$(cd "$WT" && FLEET_DISPATCH_PRINT_LAUNCH=1 "$WRAP" pane-3 sonnet high worker "" live "plan=" 2>/dev/null)
[[ "$out" == *" plan= prompt="* ]] && ok "bare plan=: not exported (dual-spelling guard)" || bad "bare plan= leaked: $out"
rm -f "$SIDECAR"

echo "T10: resume discards the assignment — released, not exported"
printf '{"session_id":"SID-77","role":"worker","model":"sonnet","effort":"high","created_epoch":1}\n' > "$SIDECAR"
export FLEET_CLAIM_LOG="$TMPROOT/wrap-claim.log"; : > "$FLEET_CLAIM_LOG"
out=$(cd "$WT" && FLEET_DISPATCH_PRINT_LAUNCH=1 "$WRAP" pane-3 sonnet high worker "" live "plan=game:7" 2>/dev/null)
[[ "$out" == resumed=1* && "$out" == *" plan= prompt="* ]] && ok "resume: assignment dropped from env" || bad "resume plan handling: $out"
grep -q -- '^--repo game planning-release 7 worker-1$' "$FLEET_CLAIM_LOG" \
  && ok "resume: pre-claim released under the worktree basename (--repo game form)" \
  || bad "resume: no planning-release call: $(cat "$FLEET_CLAIM_LOG")"
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
python3 -c "import json;d=json.load(open('$SIDECAR'));assert d.get('resumes')==1" 2>/dev/null \
  && ok "resume counter recorded (resumes=1)" || bad "resume counter missing/wrong: $(cat "$SIDECAR" 2>/dev/null)"
# 2nd resumed clean exit, still in-flight: breaker fires, sidecar cleared.
STUB_BRANCH="claude/123-foo" STUB_DIRTY=1 STUB_CLAUDE_RC=0 run_wrap sonnet high worker
[[ ! -f "$SIDECAR" ]] && ok "2nd clean resume cleared the sidecar (loop broken)" || bad "2nd clean resume kept the sidecar (loop!)"

echo "T15: a clean resumed exit resets the transient-failure streak"
printf '{"session_id":"SID-OK","role":"worker","model":"sonnet","effort":"high","created_epoch":1,"resume_failures":1}\n' > "$SIDECAR"
STUB_BRANCH="claude/123-foo" STUB_DIRTY=1 STUB_CLAUDE_RC=0 run_wrap sonnet high worker
python3 -c "import json;d=json.load(open('$SIDECAR'));assert d.get('resume_failures')==0 and d.get('resumes')==1" 2>/dev/null \
  && ok "clean resume zeroed resume_failures (and bumped resumes)" || bad "streak not reset: $(cat "$SIDECAR" 2>/dev/null)"
rm -f "$SIDECAR"

echo
echo "PASS: $PASS  FAIL: $FAIL"
[[ "$FAIL" -eq 0 ]]
