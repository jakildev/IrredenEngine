# lib_daemon_reload.sh — sandbox and polling helpers shared by the
# persistent-daemon self-reload E2E suites (test_daemon_reload.sh,
# test_dispatcher_reload_inputs.sh). Source it after lib_assert.sh, with
# TMPROOT set; it defines functions only.

# stage_daemon_sandbox <fleet-dir> — copy the fleet scripts to $TMPROOT/lib and
# stub every outbound edge in $TMPROOT/bin. Sets STAGE and SANDBOX_PATH.
#
# The whole directory is staged so a daemon's FLEET_LIB_DIR resolves inside
# the sandbox, and editing a "source file" edits the sandbox's, never the
# repo's.
stage_daemon_sandbox() {
    local stub tick_stub
    STAGE="$TMPROOT/lib"
    cp -R "$1" "$STAGE"
    mkdir -p "$TMPROOT/bin"

    # Stub every outbound edge so the sandboxed daemons stay hermetic — no live
    # GitHub, no live ~/.fleet. `gh` and `git` failing is a state the scout
    # already handles (it logs "degraded: preserving last-known-good" and ticks
    # on), which is exactly the condition the reload check must survive: it sits
    # OUTSIDE the try/except around tick_once(), so a daemon that cannot reach
    # GitHub must still be able to load its own fix. tmux exiting 1 makes
    # session_exists false, so the dispatcher never reaches its dispatch pass.
    # The stubs must precede the real binaries on PATH, which is why SANDBOX_PATH
    # puts $TMPROOT/bin ahead of /usr/bin.
    for stub in gh git tmux; do
        printf '#!/usr/bin/env bash\nexit 1\n' >"$TMPROOT/bin/$stub"
    done
    # fleet-gh-token doubles as the tick counter. Both daemons call it exactly
    # once per tick and nowhere else — the dispatcher at the top of its loop, the
    # scout as the first statement of tick_once — so one appended line per call
    # IS the daemon's tick sequence, and "did the reload land within three ticks"
    # becomes a count instead of a stopwatch. A seconds-based bound cannot tell a
    # slow reload from a slow host, which is the whole distinction the bound
    # draws. The dispatcher resolves the bare name through PATH; the scout
    # prefers its own sibling (_fleet_script_argv), so the staged copy is
    # replaced too. Neither copy is in either daemon's source surface, so
    # replacing them does not itself look like a source change.
    #
    # Each line is tagged with the boot generation that wrote it. A reload keeps
    # the pid, so the daemon's own count of `started` lines is the only thing
    # that separates the two images — and tagging makes "ticks the pre-reload
    # image ran" a fact recoverable from the file at any later moment, rather
    # than a sample that has to be taken in the window between the reload and
    # the next tick.
    tick_stub='#!/usr/bin/env bash
if [[ -n "${FLEET_TICK_LOG:-}" ]]; then
    boots=0
    if [[ -f "${FLEET_DAEMON_LOG:-}" ]]; then
        boots=$(grep -c "started (pid=" "$FLEET_DAEMON_LOG")
    fi
    printf "tick boot=%s\n" "$boots" >>"$FLEET_TICK_LOG"
fi
exit 0
'
    printf '%s' "$tick_stub" >"$TMPROOT/bin/fleet-gh-token"
    printf '%s' "$tick_stub" >"$STAGE/fleet-gh-token"
    chmod +x "$TMPROOT/bin"/* "$STAGE/fleet-gh-token"
    # Stubs first (they must shadow a real gh/git on this host), then the running
    # bash's directory ahead of /bin. The dispatcher re-execs through its
    # `#!/usr/bin/env bash` shebang, so the bash PATH order decides which
    # interpreter the RELOADED image gets: this daemon needs bash 4 and macOS's
    # /bin/bash is 3.2, which cannot even parse it.
    SANDBOX_PATH="$TMPROOT/bin":"$(dirname "$BASH")":/usr/bin:/bin:/usr/sbin:/sbin
}

# wait_for <file> <fixed-string> <seconds> — poll rather than sleep a flat
# worst case.
wait_for() {
    local file="$1" needle="$2" limit="$3" waited=0
    while (( waited < limit )); do
        [[ -f "$file" ]] && grep -qF -- "$needle" "$file" && return 0
        sleep 1
        waited=$((waited + 1))
    done
    return 1
}

started_revs() { grep -o 'rev=[0-9a-f]*' "$1" | sed 's/rev=//'; }
started_pids() { grep -o 'started (pid=[0-9]*' "$1" | sed 's/.*pid=//'; }

# ticks_of <tick-log> <generation> — ticks written so far by that boot
# generation of the daemon (generation 1 is the image launched here).
ticks_of() {
    if [[ -f "$1" ]]; then grep -c "boot=$2\$" "$1"; else echo 0; fi
}

# wait_for_boots <log> <n> <seconds> — wait until the log holds N `started`
# lines. NOT wait_for on a substring of that line: every marker on a boot line
# ("rearm=", "interval=") is already present from boot 1, so a substring wait
# returns instantly and the count assertion after it races the re-exec'd image
# rather than waiting for it. That vacuous wait passed standalone and failed
# only under a loaded `run_all.sh` — the assertion has to wait on the thing it
# is about to assert.
wait_for_boots() {
    local file="$1" want="$2" limit="$3" waited=0
    while (( waited < limit )); do
        [[ -f "$file" ]] && (( $(started_pids "$file" | wc -l) >= want )) && return 0
        sleep 1
        waited=$((waited + 1))
    done
    return 1
}

# assert_within_ticks <label> <tick-log> <ticks-at-edit> <deciding-offset> —
# the contracted bound: a daemon completes an in-place reload within three
# ticks of the surface edit. A reload that regressed to four or more ticks (a
# debounce needing a third match, the check moved below a slow stage, a gate
# that re-probes before it acts) still finishes inside every wall-clock wait
# here, so this is the only arm that sees it.
#
# Counted from generation 1's own tick lines, so the reloaded image's ticks
# cannot inflate it however late this runs. <deciding-offset> accounts for
# where each daemon's tick calls fleet-gh-token relative to its reload check:
# the dispatcher's call is above the check, so the tick that execs has
# already written its line (0); the scout's is below it, so that tick leaves
# no line (1). A result of 0 means no generation-1 tick was recorded at all
# and the arm measured nothing, which fails rather than passes.
assert_within_ticks() {
    local label="$1" tick_log="$2" base="$3" offset="$4"
    local n=$(( $(ticks_of "$tick_log" 1) - base + offset ))
    if (( n >= 1 && n <= 3 )); then
        ok "$label (took $n)"
    else
        bad "$label (took $n)"
    fi
}
