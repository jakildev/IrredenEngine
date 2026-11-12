# Event-driven rendezvous for the fleet-claim race suites. Sourced, never
# executed. The suite exports CLAIM_RUN (one schedule's scratch directory),
# CLAIM_RACE_LIB (this file's absolute path) and CLAIM_RACE_SUITE_PID (its
# own $$), sources this file, and its gh stub sources it again through
# CLAIM_RACE_LIB.
#
# Every wait ends on an observable event: the awaited marker appears, the
# claimant responsible for it exits (its <name>.rc lands), or the schedule is
# abandoned — the suite wrote $CLAIM_RUN/abort (race_abort) after a broken
# rendezvous or on deadlock-guard expiry, or the suite process itself is gone,
# so no stub outlives a killed suite. Elapsed time never picks a
# schedule or turns a slow claimant into a result; the guard
# (CLAIM_RACE_GUARD_SECS, default 60) only breaks a deadlock and reports it.
# Each claimant runs in its own process group, so cleanup signals every
# descendant rather than only the launching subshell; a group that outlives
# its claimant, or the guard, gets SIGTERM and then SIGKILL, each followed by
# a bounded drain (CLAIM_RACE_GRACE_SECS, default 10).
#
# Test seams, honoured by race_stage inside the stub (space-separated lists):
#   CLAIM_RACE_DELAY="<claimant>:<stage>:<secs>"   sleep before the first <stage>
#   CLAIM_RACE_EXIT="<claimant>:<stage>"           exit 1 at <stage>

# --- stub side ---------------------------------------------------------------

race_stage() {
    local stage="$1" spec
    for spec in ${CLAIM_RACE_DELAY:-}; do
        if [[ "$spec" == "${CLAIMANT:-}:$stage:"* \
                && ! -e "$CLAIM_RUN/${CLAIMANT}.delayed-$stage" ]]; then
            : > "$CLAIM_RUN/${CLAIMANT}.delayed-$stage"
            sleep "${spec##*:}"
        fi
    done
    for spec in ${CLAIM_RACE_EXIT:-}; do
        if [[ "$spec" == "${CLAIMANT:-}:$stage" ]]; then
            echo "gh stub: ${CLAIMANT} forced to exit at $stage" >&2
            exit 1
        fi
    done
    return 0
}

# race_abandoned: succeed, with the reason in RACE_ABANDONED, once the
# schedule is abandoned.
race_abandoned() {
    if [[ -e "$CLAIM_RUN/abort" ]]; then
        RACE_ABANDONED="the suite abandoned the schedule"
    elif [[ -n "${CLAIM_RACE_SUITE_PID:-}" ]] && ! kill -0 "$CLAIM_RACE_SUITE_PID" 2>/dev/null; then
        RACE_ABANDONED="the suite process exited"
    else
        return 1
    fi
}

# race_await <marker> <claimant>...
#   Return 0 once $CLAIM_RUN/<marker>-<claimant> exists for every claimant.
#   Return 1, naming the cause on stderr, when a missing claimant has exited
#   or the suite abandoned the schedule.
race_await() {
    local marker="$1" claimant missing
    shift
    while :; do
        missing=""
        for claimant in "$@"; do
            [[ -e "$CLAIM_RUN/$marker-$claimant" ]] || missing="$missing $claimant"
        done
        [[ -z "$missing" ]] && return 0
        for claimant in $missing; do
            if [[ -e "$CLAIM_RUN/$claimant.rc" ]]; then
                echo "race: '$marker' barrier broken: $claimant exited with rc $(cat "$CLAIM_RUN/$claimant.rc") before arriving" >&2
                return 1
            fi
        done
        if race_abandoned; then
            echo "race: '$marker' barrier aborted: $RACE_ABANDONED; missing:$missing" >&2
            return 1
        fi
        sleep 0.01
    done
}

# race_lock <lock-dir>
#   mkdir mutex. Holders never wait while holding it, so the wait ends
#   unsuccessfully only when the schedule is abandoned (a holder killed
#   mid-section).
race_lock() {
    until mkdir "$1" 2>/dev/null; do
        if race_abandoned; then
            echo "race: lock $1 aborted: $RACE_ABANDONED" >&2
            return 1
        fi
        sleep 0.005
    done
}

# --- suite side --------------------------------------------------------------

RACE_NAMES=""
RACE_PIDS=""
RACE_BROKEN=""

race_reset() {
    rm -rf "$CLAIM_RUN"
    mkdir -p "$CLAIM_RUN"
    RACE_NAMES=""
    RACE_PIDS=""
    RACE_BROKEN=""
}

# race_launch <name> <command>...
#   Run <command> in the background with CLAIMANT=<name>. Its combined output
#   lands in $CLAIM_RUN/<name>.out; its exit code lands in <name>.rc last and
#   atomically, so an existing .rc means the claimant has exited. Job control
#   is on only for the launch, making the subshell a process-group leader
#   whose pid is the group id; stdin is /dev/null, as without job control.
race_launch() {
    local name="$1" monitor=0
    shift
    [[ $- == *m* ]] && monitor=1
    set -m
    (
        rc=0
        CLAIMANT="$name" "$@" >"$CLAIM_RUN/$name.out" 2>&1 || rc=$?
        printf '%s\n' "$rc" > "$CLAIM_RUN/$name.rc.tmp"
        mv "$CLAIM_RUN/$name.rc.tmp" "$CLAIM_RUN/$name.rc"
    ) </dev/null &
    RACE_PIDS="$RACE_PIDS $!"
    (( monitor )) || set +m
    RACE_NAMES="$RACE_NAMES $name"
}

race_rc() {
    cat "$CLAIM_RUN/$1.rc" 2>/dev/null || echo running
}

# race_wait_for <claimant> <test-command>...
#   Wait until <test-command> succeeds. Fail, recording why in RACE_BROKEN,
#   when <claimant> exits first or the deadlock guard expires.
race_wait_for() {
    local claimant="$1" deadline=$((SECONDS + ${CLAIM_RACE_GUARD_SECS:-60}))
    shift
    until "$@"; do
        if [[ -e "$CLAIM_RUN/$claimant.rc" ]]; then
            "$@" && return 0
            RACE_BROKEN="$claimant exited with rc $(race_rc "$claimant") before: $*"
            return 1
        fi
        if (( SECONDS >= deadline )); then
            RACE_BROKEN="deadlock guard expired waiting on $claimant for: $*"
            return 1
        fi
        sleep 0.01
    done
}

race_abort() {
    : > "$CLAIM_RUN/abort"
}

race_all_exited() {
    local name
    for name in $RACE_NAMES; do
        [[ -e "$CLAIM_RUN/$name.rc" ]] || return 1
    done
    return 0
}

# race_finish
#   Wait for every claimant to exit. On guard expiry, abort the barriers and
#   give the claimants a grace period to unwind. Then reap every claimant's
#   process group: a group still live after its claimant exited is reported
#   in RACE_BROKEN, and no member survives the call unless it survives
#   SIGKILL, which is reported too.
race_finish() {
    local deadline=$((SECONDS + ${CLAIM_RACE_GUARD_SECS:-60})) grace=0 reaped=1 pid name
    until race_all_exited; do
        if (( SECONDS >= deadline )); then
            (( grace )) && break
            RACE_BROKEN="${RACE_BROKEN:+$RACE_BROKEN; }deadlock guard expired before every claimant exited"
            race_abort
            grace=1
            deadline=$((SECONDS + ${CLAIM_RACE_GRACE_SECS:-10}))
        fi
        sleep 0.01
    done
    if race_all_exited; then
        for pid in $RACE_PIDS; do wait "$pid" 2>/dev/null || true; done
        if (( $(race_live_count) > 0 )); then
            RACE_BROKEN="${RACE_BROKEN:+$RACE_BROKEN; }claimant processes outlived their claimant"
        fi
    fi
    race_kill_survivors || reaped=0
    for name in $RACE_NAMES; do
        [[ -e "$CLAIM_RUN/$name.rc" ]] || printf '143\n' > "$CLAIM_RUN/$name.rc"
    done
    (( reaped )) || return 0
    for pid in $RACE_PIDS; do wait "$pid" 2>/dev/null || true; done
}

# race_kill_survivors
#   SIGTERM every live claimant process group, then SIGKILL whatever is left,
#   each followed by a bounded drain. Fail, recording it in RACE_BROKEN, if a
#   member survives SIGKILL; the caller must not then wait on its subshell.
race_kill_survivors() {
    local sig pid deadline
    for sig in TERM KILL; do
        (( $(race_live_count) > 0 )) || return 0
        for pid in $RACE_PIDS; do kill -"$sig" -- "-$pid" 2>/dev/null || true; done
        deadline=$((SECONDS + ${CLAIM_RACE_GRACE_SECS:-10}))
        while (( $(race_live_count) > 0 && SECONDS < deadline )); do sleep 0.05; done
    done
    (( $(race_live_count) > 0 )) || return 0
    RACE_BROKEN="${RACE_BROKEN:+$RACE_BROKEN; }$(race_live_count) claimant process group(s) survived SIGKILL"
    return 1
}

# race_live_count
#   Claimant process groups with a live member. Bash reaps each exited
#   subshell itself, so a dead claimant's group drops out once its
#   descendants are gone.
race_live_count() {
    local pid count=0
    for pid in $RACE_PIDS; do
        kill -0 -- "-$pid" 2>/dev/null && count=$((count + 1))
    done
    printf '%s\n' "$count"
}

# race_report
#   Each claimant's exit code, the markers it reached, and its captured
#   output, preceded by the broken rendezvous if any.
race_report() {
    local name path markers
    [[ -n "$RACE_BROKEN" ]] && echo "    rendezvous: $RACE_BROKEN"
    for name in $RACE_NAMES; do
        markers=""
        for path in "$CLAIM_RUN"/*-"$name"; do
            [[ -e "$path" ]] && markers="$markers ${path##*/}"
        done
        echo "    claimant $name: rc=$(race_rc "$name") markers=[${markers# }]"
        if [[ -f "$CLAIM_RUN/$name.out" ]]; then
            sed 's/^/      | /' "$CLAIM_RUN/$name.out"
        fi
    done
}

# race_report_if_failed <fail-count-before-the-schedule> <schedule-label>
#   Compares against lib_assert.sh's FAIL counter, which the suite sources.
race_report_if_failed() {
    if (( FAIL > $1 )); then
        echo "  $2 claimant state:"
        race_report
    fi
}
