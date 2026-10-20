#!/usr/bin/env bash
# Tests for engine/tools/bin/ir-launcher — the IRLauncher supervisor: first
# use, the update loop, the installed/<id>/ + `current` commit, recovery of
# interrupted states, and the one-writer lock.
#
# Hermetic. Each row builds a fake checkout (CMakePresets.json, engine/, a
# git repo) holding copies of ir-launcher, ir-build, ir-acquire and the
# helpers, so the script resolves its root to the fixture. Stubs on PATH:
#   cmake   models ir-build's configure (--preset … -B) and build (--build …
#           --target IRLauncher) shapes, fails closed on anything else; a
#           build writes a stub IRLauncher directory, or stages a new
#           scripts/ file and fails when the fixture says so.
#   ir-run  runs the installed stub IRLauncher found by name under
#           --build-dir, from its own directory.
#   mv      passes through, except the step-4 replace of installed/current,
#           which can fail or park on a barrier (fixture files).
#   date    passes through, except the <id> stamp, which can be pinned.
# The stub IRLauncher logs a timestamp, its pid and argv, and exits with the
# next code from the fixture's codes file ("hold" waits for a release file).
# The shipped script reads none of the stubs' configuration.

set -uo pipefail

REPO_ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
# shellcheck source=lib_preflight.sh
source "$(dirname "$0")/lib_preflight.sh"
SUBJECT="$REPO_ROOT/engine/tools/bin/ir-launcher"
if [[ ! -x "$SUBJECT" ]]; then
    echo "SKIP: $SUBJECT not found or not executable" >&2
    exit 3
fi

# shellcheck source=lib_assert.sh
source "$(dirname "$0")/lib_assert.sh"

TMP=$(mktemp -d "${TMPDIR:-/tmp}/ir-launcher.XXXXXX")
TMP=$(cd "$TMP" && pwd -P)
cleanup() {
    local f p
    for f in "$TMP"/*.fix/pids; do
        [[ -f "$f" ]] || continue
        while IFS= read -r p; do kill "$p" 2>/dev/null || true; done < "$f"
    done
    rm -rf "$TMP"
}
trap cleanup EXIT

export HOME="$TMP/home"
mkdir -p "$HOME"
export IR_LOCK_ROOT="$TMP/locks"
export IR_HOST_TOML="$TMP/no-host.toml"
unset IR_QUIET_OWNER IR_QUIET_ACQUIRE_VERB IRREDEN_BUILD_DIR IR_RUN_WRAPPER \
    IR_ACQUIRE_HOLDER_PID IR_ACQUIRE_HOLDER_WINPID IR_ACQUIRE_HELD_VERB 2>/dev/null || true

STUB="$TMP/stub-bin"
mkdir -p "$STUB"
REAL_MV=$(command -v mv)
REAL_DATE=$(command -v date)
export STUB_LAUNCHER_SRC="$TMP/stub-launcher"

cat > "$STUB/cmake" <<'EOF'
#!/usr/bin/env bash
set -u
F="$STUB_FIX"
echo "$(date +%s) $$ cmake $*" >> "$F/events.log"
echo "$*" >> "$F/cmake.log"
case "${1:-}" in
    --preset)
        b=""
        while [[ $# -gt 0 ]]; do
            if [[ "$1" == "-B" ]]; then b="$2"; shift; fi
            shift
        done
        [[ -n "$b" ]] || { echo "stub cmake: configure without -B" >&2; exit 99; }
        mkdir -p "$b"
        echo "# stub cache" > "$b/CMakeCache.txt"
        exit 0
        ;;
    --build)
        b="$2"; shift 2
        target=""
        while [[ $# -gt 0 ]]; do
            if [[ "$1" == "--target" ]]; then target="$2"; shift; fi
            shift
        done
        [[ "$target" != "cmake_check_build_system" ]] || exit 0
        [[ "$target" == "IRLauncher" ]] || { echo "stub cmake: unmodeled target '$target'" >&2; exit 99; }
        n=$(( $(cat "$F/builds" 2>/dev/null || echo 0) + 1 ))
        echo "$n" > "$F/builds"
        out="$b/creations/launcher"
        mkdir -p "$out/scripts" "$out/data" "$out/CMakeFiles"
        if [[ -e "$F/build-fail" ]]; then
            echo "staged-by-build-$n" > "$out/scripts/new_from_failed_build.lua"
            echo "error: stub compile failure" >&2
            exit 1
        fi
        echo "v$n" > "$out/scripts/version"
        echo "data" > "$out/data/x"
        echo "obj" > "$out/CMakeFiles/main.o"
        cp "$STUB_LAUNCHER_SRC" "$out/IRLauncher"
        chmod +x "$out/IRLauncher"
        exit 0
        ;;
esac
echo "stub cmake: unmodeled invocation: $*" >&2
exit 99
EOF

cat > "$STUB/ir-run" <<'EOF'
#!/usr/bin/env bash
set -u
F="$STUB_FIX"
echo "$(date +%s) $$ ir-run $*" >> "$F/events.log"
[[ "${1:-}" == "--build-dir" ]] || { echo "stub ir-run: expected --build-dir first" >&2; exit 99; }
dir="$2"; name="$3"; shift 3
[[ -f "$dir/$name" ]] || { echo "stub ir-run: no $name under $dir" >&2; exit 98; }
cd "$dir" || exit 98
exec "./$name" "$@"
EOF

cat > "$STUB_LAUNCHER_SRC" <<'EOF'
#!/usr/bin/env bash
set -u
F="$STUB_FIX"
echo "$$" >> "$F/pids"
n=$(( $(cat "$F/launches" 2>/dev/null || echo 0) + 1 ))
echo "$n" > "$F/launches"
code=$(sed -n "${n}p" "$F/codes" 2>/dev/null)
code="${code:-0}"
ver=$(cat scripts/version 2>/dev/null || echo none)
irkv=$(cat save_files/launcher.irkv 2>/dev/null || echo none)
echo "$(date +%s) $$ launch n=$n dir=$(basename "$PWD") ver=$ver irkv=$irkv argv=$*" >> "$F/events.log"
mkdir -p save_files
[[ -f save_files/launcher.irkv ]] || echo "written-by-$ver" > save_files/launcher.irkv
if [[ "$code" == "hold" ]]; then
    touch "$F/launcher-up"
    i=0
    while [[ ! -e "$F/launcher-release" ]] && (( i < 1200 )); do sleep 0.05; i=$(( i + 1 )); done
    code=0
fi
echo "$(date +%s) $$ exit n=$n code=$code" >> "$F/events.log"
exit "$code"
EOF

cat > "$STUB/mv" <<EOF
#!/usr/bin/env bash
REAL_MV="$REAL_MV"
EOF
cat >> "$STUB/mv" <<'EOF'
F="${STUB_FIX:-/nonexistent}"
args=("$@")
count=${#args[@]}
src=""
(( count >= 2 )) && src="${args[$(( count - 2 ))]}"
case "$src" in
    */installed/current.tmp|installed/current.tmp)
        echo "$(date +%s) $$ mv-commit" >> "$F/events.log"
        if [[ -e "$F/mv-fail" ]]; then
            echo "mv: stub replace failure" >&2
            exit 1
        fi
        if [[ -e "$F/mv-barrier" ]]; then
            touch "$F/mv-ready"
            i=0
            while [[ ! -e "$F/mv-release" ]] && (( i < 1200 )); do sleep 0.05; i=$(( i + 1 )); done
        fi
        ;;
esac
exec "$REAL_MV" "$@"
EOF

cat > "$STUB/date" <<EOF
#!/usr/bin/env bash
REAL_DATE="$REAL_DATE"
EOF
cat >> "$STUB/date" <<'EOF'
if [[ "$*" == "-u +%Y%m%dT%H%M%SZ" && -e "${STUB_FIX:-/nonexistent}/date-fixed" ]]; then
    echo "20261018T071500Z"
    exit 0
fi
exec "$REAL_DATE" "$@"
EOF
chmod +x "$STUB"/* "$STUB_LAUNCHER_SRC"
export PATH="$STUB:$PATH"

# On native Windows ir-build runs cmake inside `cmd.exe //c "set PATH=
# $IR_MSYS2_MINGW;%PATH% && cmake …"`, and cmd.exe skips an extensionless
# script, so the stub above is invisible and the real cmake.exe runs. A
# cmake.bat that hands its arguments to the stub, in a directory the shipped
# IR_MSYS2_MINGW seam puts first on cmd.exe's PATH, routes the call back to it.
case "$(uname -s)" in
    MINGW*|MSYS*|CYGWIN*)
        WIN_STUB="$(cygpath -w "$STUB")"
        printf '@echo off\r\n"%s" "%%~dp0cmake" %%*\r\nexit /b %%ERRORLEVEL%%\r\n' \
            "$(cygpath -w "$BASH")" > "$STUB/cmake.bat"
        export IR_MSYS2_MINGW="$WIN_STUB"
        ;;
esac

# --- fixture helpers ----------------------------------------------------------

# new_fixture <name> — a fake checkout at $R with its stub state at $STUB_FIX.
new_fixture() {
    R="$TMP/$1"
    export STUB_FIX="$TMP/$1.fix"
    mkdir -p "$R/engine/tools/bin" "$R/engine/tools/lib" "$STUB_FIX"
    cp "$SUBJECT" "$REPO_ROOT/engine/tools/bin/ir-build" "$REPO_ROOT/engine/tools/bin/ir-acquire" \
        "$R/engine/tools/bin/"
    cp "$REPO_ROOT/engine/tools/lib/concurrency_helpers.sh" "$REPO_ROOT/engine/tools/lib/quiet_window.py" \
        "$R/engine/tools/lib/"
    cp "$REPO_ROOT/engine/tools/concurrency.toml" "$R/engine/tools/"
    touch "$R/CMakePresets.json"
    git -C "$R" init -q
    : > "$STUB_FIX/events.log"
    : > "$STUB_FIX/cmake.log"
    : > "$STUB_FIX/pids"
    LAUNCHER="$R/engine/tools/bin/ir-launcher"
    INST="$R/build-launcher/installed"
}

set_codes() {
    printf '%s\n' "$@" > "$STUB_FIX/codes"
    echo 0 > "$STUB_FIX/launches"
}

# launch [args…] — runs the fixture's launcher in the foreground; sets RC,
# OUT (stdout+stderr).
launch() {
    RC=0
    OUT=$("$LAUNCHER" "$@" 2>&1) || RC=$?
}

current() { cat "$INST/current" 2>/dev/null || echo "<none>"; }
installed_dirs() {
    find "$INST" -mindepth 1 -maxdepth 1 -type d 2>/dev/null | sed 's|.*/||' | LC_ALL=C sort | tr '\n' ' '
}
tree_hash() {
    (cd "$INST" 2>/dev/null && {
        find . | LC_ALL=C sort
        find . -type f | LC_ALL=C sort | while IFS= read -r f; do cksum "$f"; done
    })
}
cmake_calls() { wc -l < "$STUB_FIX/cmake.log" | tr -d ' '; }
builds() { cat "$STUB_FIX/builds" 2>/dev/null || echo 0; }
nth_launch() { grep " launch n=$1 " "$STUB_FIX/events.log" || echo "<no launch $1>"; }

# wait_for <file> <tenths> — 0 once <file> exists.
wait_for() {
    local i
    for (( i = 0; i < $2 * 2; i++ )); do
        [[ -e "$1" ]] && return 0
        sleep 0.05
    done
    return 1
}
# exited_within <pid> <tenths> — 0 once <pid> is gone; kills it otherwise.
exited_within() {
    local i
    for (( i = 0; i < $2 * 2; i++ )); do
        kill -0 "$1" 2>/dev/null || return 0
        sleep 0.05
    done
    kill "$1" 2>/dev/null || true
    return 1
}
dead_pid() {
    sh -c 'exit 0' &
    local p=$!
    wait "$p"
    echo "$p"
}
# helper_call <function> <args…> — runs a lock helper from the fixture's copy.
helper_call() {
    "$BASH" -c 'source "$1" || exit 99; set +e; shift; "$@"' _ \
        "$R/engine/tools/lib/concurrency_helpers.sh" "$@"
}
lock_holder() { helper_call _ir_lock_holder "$R/build-launcher/lock" 2>/dev/null || echo none; }

# --- T0: usage ---------------------------------------------------------------
echo "T0: usage"
new_fixture usage
launch --bogus
assert_eq "$RC" "2" "an unknown option exits 2"
assert_contains "$OUT" "unknown option '--bogus'" "an unknown option names itself"
assert_eq "$(cmake_calls)" "0" "an unknown option builds nothing"

# --- T1: first run -------------------------------------------------------------
echo "T1: first run configures, builds, installs and runs supervised"
new_fixture first
set_codes 0
launch
assert_eq "$RC" "0" "first run exits with the launcher's code"
assert_contains "$(cat "$STUB_FIX/cmake.log")" "--preset " "first run configures with the host preset"
assert_contains "$(cat "$STUB_FIX/cmake.log")" "-B $R/build-launcher" "configure targets build-launcher/"
assert_contains "$(cat "$STUB_FIX/cmake.log")" "--build $R/build-launcher" "the build runs in build-launcher/"
ID1=$(current)
assert_eq "$(installed_dirs)" "$ID1 " "installed/ holds exactly the copy current names"
if [[ -f "$INST/$ID1/IRLauncher" && -f "$INST/$ID1/scripts/version" ]]; then
    ok "the installed copy holds the executable and its staged scripts"
else
    bad "the installed copy is incomplete"
fi
[[ ! -e "$INST/$ID1/CMakeFiles" ]] && ok "CMakeFiles is not installed" || bad "CMakeFiles was installed"
assert_contains "$(cat "$STUB_FIX/events.log")" \
    "ir-run --build-dir build-launcher/installed/$ID1 IRLauncher --window-mode normal --supervised" \
    "the launcher runs from the installed copy through ir-run, supervised"
assert_contains "$(nth_launch 1)" "dir=$ID1 ver=v1" "the installed v1 copy ran"

# --- T2: update ------------------------------------------------------------------
echo "T2: update requested by the launcher"
new_fixture update
set_codes 86 7
launch
assert_eq "$RC" "7" "the script exits with the second launch's code"
EV="$STUB_FIX/events.log"
exit_line=$(grep -n " exit n=1 code=86" "$EV" | head -n 1 | cut -d: -f1)
build2_line=$(grep -n " cmake --build" "$EV" | sed -n 2p | cut -d: -f1)
if [[ -n "$exit_line" && -n "$build2_line" ]] && (( exit_line < build2_line )); then
    ok "the launcher's exit is logged before the update build starts"
else
    bad "the update build did not follow the launcher's exit (exit=$exit_line build=$build2_line)"
fi
ID2=$(current)
assert_contains "$(nth_launch 2)" "dir=$ID2 ver=v2 irkv=written-by-v1" \
    "the second launch runs the new copy with launcher.irkv carried over"
assert_eq "$(installed_dirs)" "$ID2 " "installed/ holds exactly one directory, the one current names"
assert_absent "$(nth_launch 2)" "--update-failed" "a successful update relaunches without the banner flag"

# --- T3: failed update -------------------------------------------------------------
echo "T3: failed update keeps the live copy byte-identical"
new_fixture failed
set_codes 0 86 0
launch
ID1=$(current)
BEFORE=$(tree_hash)
touch "$STUB_FIX/build-fail"
launch
assert_eq "$RC" "0" "the relaunched copy's exit code ends the loop"
assert_eq "$(tree_hash)" "$BEFORE" "current and the copy it names are byte-identical after the failed build"
assert_contains "$(nth_launch 3)" "dir=$ID1 ver=v1" "the previous copy is relaunched"
assert_contains "$(nth_launch 3)" "--update-failed build-launcher/update.log" \
    "the relaunch gets --update-failed with the root-relative log"
assert_contains "$(cat "$R/build-launcher/update.log")" "stub compile failure" "the build output lands in update.log"
if ls -d "$INST"/*.staging >/dev/null 2>&1; then bad "a .staging directory remains"; else ok "no .staging directory remains"; fi

# --- T4: commit failure ------------------------------------------------------------
echo "T4: step-4 replace failure"
new_fixture commit
set_codes 0 86 0
launch
ID1=$(current)
BEFORE=$(tree_hash)
touch "$STUB_FIX/mv-fail"
launch
assert_eq "$RC" "0" "a failed commit relaunches and ends with its code"
assert_eq "$(builds)" "2" "the update build ran"
assert_contains "$(nth_launch 3)" "dir=$ID1" "the old <id> runs"
assert_contains "$(nth_launch 3)" "--update-failed build-launcher/update.log" "the old <id> runs with --update-failed"
assert_eq "$(installed_dirs)" "$ID1 " "the new directory is gone after the pass"
assert_eq "$(tree_hash)" "$BEFORE" "installed/ is unchanged by the failed commit"

# --- T5: interrupted swap ----------------------------------------------------------
echo "T5: recovery of interrupted states"
new_fixture interrupted
launch --update
Z=$(current)
assert_eq "$RC" "0" "seed: --update installs a first copy"
mkdir -p "$INST/20200101T000000Z-1.staging/scripts"
echo partial > "$INST/20200101T000000Z-1.staging/scripts/x"
set_codes 0
launch
assert_eq "$(installed_dirs)" "$Z " "(a) a .staging directory is removed; only <z> remains"
assert_contains "$(nth_launch 1)" "dir=$Z" "(a) <z> ran"
cp -R "$INST/$Z" "$INST/20200101T000000Z-2"
set_codes 0
launch
assert_eq "$(installed_dirs)" "$Z " "(b) a complete orphan is removed; only <z> remains"
assert_contains "$(nth_launch 1)" "dir=$Z" "(b) <z> ran"
cp -R "$INST/$Z" "$INST/20200101T000000Z-3"
rm -f "$INST/current"
B0=$(builds)
set_codes 0
launch
NEWC=$(current)
assert_eq "$(builds)" "$(( B0 + 1 ))" "(c) a missing current runs first use"
if [[ "$NEWC" != "<none>" && "$NEWC" != "$Z" && "$NEWC" != "20200101T000000Z-3" && -f "$INST/$NEWC/IRLauncher" ]]; then
    ok "(c) first use writes current naming a complete new copy"
else
    bad "(c) current after first use: $NEWC"
fi
assert_contains "$(nth_launch 1)" "dir=$NEWC" "(c) the new copy ran"
[[ -d "$INST/20200101T000000Z-3" ]] && ok "(c) first use deletes nothing but .staging directories" \
    || bad "(c) first use removed an orphan"

# --- T6: overlapping writers --------------------------------------------------------
echo "T6: overlapping writers"
new_fixture overlap
launch --update
OLD=$(current)
touch "$STUB_FIX/mv-barrier"
"$LAUNCHER" --update > "$TMP/overlap.a.out" 2>&1 &
A=$!
if wait_for "$STUB_FIX/mv-ready" 300; then
    ok "updater A parks on the step-4 barrier"
else
    bad "updater A never reached the step-4 barrier"
fi
assert_eq "$(current)" "$OLD" "while A is parked, current still names the old copy"
HASH0=$(tree_hash)
C0=$(cmake_calls)
"$LAUNCHER" --update > "$TMP/overlap.b.out" 2>&1 &
B=$!
if exited_within "$B" 10; then
    rc=0; wait "$B" || rc=$?
    [[ "$rc" != "0" ]] && ok "a second --update exits non-zero within one second" \
        || bad "a second --update exited 0"
else
    bad "a second --update did not exit within one second"
fi
assert_contains "$(cat "$TMP/overlap.b.out")" "pid $A" "the second --update prints A's pid"
"$LAUNCHER" > "$TMP/overlap.c.out" 2>&1 &
C=$!
if exited_within "$C" 10; then
    rc=0; wait "$C" || rc=$?
    [[ "$rc" != "0" ]] && ok "a plain ir-launcher exits non-zero within one second" \
        || bad "a plain ir-launcher exited 0"
else
    bad "a plain ir-launcher did not exit within one second"
fi
assert_contains "$(cat "$TMP/overlap.c.out")" "pid $A" "the plain ir-launcher prints A's pid"
assert_eq "$(cmake_calls)" "$C0" "neither contender called cmake"
assert_eq "$(tree_hash)" "$HASH0" "installed/ is unchanged by the contenders"
touch "$STUB_FIX/mv-release"
rc=0; wait "$A" || rc=$?
assert_eq "$rc" "0" "updater A completes after the release"
NEWA=$(current)
if [[ "$NEWA" != "$OLD" && -f "$INST/$NEWA/IRLauncher" ]]; then
    ok "current names A's copy, which holds the executable"
else
    bad "current after A: $NEWA"
fi
assert_eq "$(installed_dirs)" "$NEWA " "no other directory remains"
rm -f "$STUB_FIX/mv-barrier" "$STUB_FIX/mv-ready" "$STUB_FIX/mv-release"

# --- T7: running launcher -----------------------------------------------------------
echo "T7: --update while a supervisor's launcher runs"
new_fixture running
launch --update
set_codes hold
"$LAUNCHER" > "$TMP/running.s.out" 2>&1 &
S=$!
wait_for "$STUB_FIX/launcher-up" 300 && ok "the supervised launcher is running" \
    || bad "the supervised launcher never started"
HASH0=$(tree_hash)
"$LAUNCHER" --update > "$TMP/running.u.out" 2>&1 &
U=$!
if exited_within "$U" 10; then
    rc=0; wait "$U" || rc=$?
    [[ "$rc" != "0" ]] && ok "--update refuses while a launcher runs" || bad "--update exited 0 while a launcher ran"
else
    bad "--update did not exit within one second"
fi
assert_contains "$(cat "$TMP/running.u.out")" "pid $S" "--update prints the supervisor's pid"
assert_eq "$(tree_hash)" "$HASH0" "installed/ is unchanged"
touch "$STUB_FIX/launcher-release"
rc=0; wait "$S" || rc=$?
assert_eq "$rc" "0" "the supervisor exits with its launcher's code"
assert_eq "$(lock_holder)" "none" "the supervisor releases the lock on exit"

# --- T8: stale lock -----------------------------------------------------------------
echo "T8: a dead holder's lock is taken over"
new_fixture stale
launch --update
OLD=$(current)
DEAD=$(dead_pid)
helper_call _ir_try_lock "$R/build-launcher/lock" "$DEAD" ""
assert_eq "$(lock_holder)" "$DEAD" "the lock is seeded through the helper with a dead pid"
launch --update
assert_eq "$RC" "0" "--update takes the stale lock over and completes"
[[ "$(current)" != "$OLD" ]] && ok "the update installed a new copy" || bad "current did not move"
assert_eq "$(lock_holder)" "none" "the lock is released after the update"

# --- T9: two stale reclaimers ---------------------------------------------------------
echo "T9: two stale reclaimers, 20 rounds"
new_fixture reclaim
launch --update
touch "$STUB_FIX/mv-barrier"
DEAD=$(dead_pid)
parked=0 loser_refused=0 loser_no_cmake=0 holder_ok=0 winner_ok=0 one_new=0
for (( round = 1; round <= 20; round++ )); do
    rm -f "$STUB_FIX/mv-ready" "$STUB_FIX/mv-release"
    PREV=$(current)
    helper_call _ir_try_lock "$R/build-launcher/lock" "$DEAD" ""
    C0=$(cmake_calls)
    "$LAUNCHER" --update > "$TMP/reclaim.1.out" 2>&1 &
    P1=$!
    "$LAUNCHER" --update > "$TMP/reclaim.2.out" 2>&1 &
    P2=$!
    wait_for "$STUB_FIX/mv-ready" 300 && parked=$(( parked + 1 ))
    winner="" loser=""
    for (( i = 0; i < 200; i++ )); do
        if ! kill -0 "$P1" 2>/dev/null; then loser=$P1; winner=$P2; break; fi
        if ! kill -0 "$P2" 2>/dev/null; then loser=$P2; winner=$P1; break; fi
        sleep 0.05
    done
    if [[ -n "$loser" ]]; then
        rc=0; wait "$loser" || rc=$?
        [[ "$rc" != "0" ]] && loser_refused=$(( loser_refused + 1 ))
        [[ "$(( $(cmake_calls) - C0 ))" == "1" ]] && loser_no_cmake=$(( loser_no_cmake + 1 ))
        [[ "$(lock_holder)" == "$winner" ]] && holder_ok=$(( holder_ok + 1 ))
    fi
    touch "$STUB_FIX/mv-release"
    if [[ -n "$winner" ]]; then
        rc=0; wait "$winner" || rc=$?
        [[ "$rc" == "0" ]] && winner_ok=$(( winner_ok + 1 ))
    else
        wait "$P1" 2>/dev/null; wait "$P2" 2>/dev/null
    fi
    NOW=$(current)
    [[ "$NOW" != "$PREV" && "$(installed_dirs)" == "$NOW " ]] && one_new=$(( one_new + 1 ))
done
rm -f "$STUB_FIX/mv-barrier"
assert_eq "$parked" "20" "every round's winner parks on the step-4 barrier"
assert_eq "$loser_refused" "20" "every round's other run exits non-zero while the winner is parked"
assert_eq "$loser_no_cmake" "20" "the refused run makes zero cmake calls (the winner's build is the only one)"
assert_eq "$holder_ok" "20" "_ir_lock_holder names the parked winner"
assert_eq "$winner_ok" "20" "the winner completes after the release"
assert_eq "$one_new" "20" "exactly one new <id> exists after each round"

# --- T10: unique ids -----------------------------------------------------------------
echo "T10: a fixed stamp still yields unique ids"
new_fixture ids
touch "$STUB_FIX/date-fixed"
launch --update
I1=$(current)
launch --update
I2=$(current)
assert_contains "$I1" "20261018T071500Z-" "the <id> carries the UTC stamp"
[[ "$I1" != "$I2" && "$I2" == 20261018T071500Z-* ]] && ok "two sequential updates produce two different <id>s" \
    || bad "ids collided: $I1 $I2"

# --- T11: pass-through -----------------------------------------------------------------
echo "T11: pass-through"
new_fixture passthrough
launch --update
B0=$(builds)
set_codes 5
launch
assert_eq "$RC" "5" "any other exit code ends the loop with that code"
assert_eq "$(builds)" "$B0" "a pass-through exit builds nothing"
assert_eq "$(grep -c ' launch n=' "$STUB_FIX/events.log")" "1" "a pass-through exit launches once"
touch "$STUB_FIX/build-fail"
launch --update
[[ "$RC" != "0" ]] && ok "--update alone exits non-zero on a failed build" || bad "--update exited 0 on a failed build"
new_fixture firstfail
touch "$STUB_FIX/build-fail"
launch
[[ "$RC" != "0" ]] && ok "a failed first-use build exits non-zero" || bad "a failed first-use build exited 0"
[[ ! -e "$INST/current" ]] && ok "a failed first use writes no current" || bad "a failed first use wrote current"

# --- T12: no leftovers ------------------------------------------------------------------
echo "T12: no logged pid outlives the script"
alive=""
for f in "$TMP"/*.fix/pids; do
    [[ -f "$f" ]] || continue
    while IFS= read -r p; do
        [[ -n "$p" ]] || continue
        kill -0 "$p" 2>/dev/null && alive="$alive $p"
    done < "$f"
done
assert_eq "$alive" "" "every launcher pid the stubs logged has exited"

summarize "ir-launcher tests"
