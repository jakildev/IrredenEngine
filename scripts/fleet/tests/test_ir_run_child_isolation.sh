#!/usr/bin/env bash
# Tests for ir-run's child isolation (--child-env / --child-unset /
# --child-prefix). The restrictions must reach the executable and its prefix
# and never the wrapper chain: a clean-box PATH or a sandbox around ir-run or
# ir-acquire breaks the bash-4 tooling before the executable starts.
#
# Hermetic: a fake build dir holds a probe executable that prints the
# environment it received; an absolute-path prefix stub logs its argv and the
# PATH it sees, then execs the rest like sandbox-exec; an `env` shim at the
# front of the wrapper's PATH logs the PATH the wrapper launched it under. An
# isolated IR_LOCK_ROOT keeps the ir-acquire cases off the host's real locks.
#
#   plain exec / --result-file CLEAN / watchdog / ir-acquire capture /
#   ir-acquire under the watchdog:
#     prefix stub receives its own args, then ./<exe> and the untouched
#       program args
#     prefix and executable see the unset and set variables
#     the wrapper keeps its original PATH
#   no child options → no env hop, no isolation line
#   malformed child options → exit 2
set -uo pipefail

SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)
REPO_ROOT=$(cd "$SCRIPT_DIR/../../.." && pwd)
IR_RUN="$REPO_ROOT/engine/tools/bin/ir-run"

# shellcheck source=lib_assert.sh
source "$SCRIPT_DIR/lib_assert.sh"

if [[ ! -x "$IR_RUN" ]]; then
    echo "test setup: ir-run not found at $IR_RUN" >&2
    exit 1
fi

TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT
BUILD="$TMP/build"
SHIM_BIN="$TMP/wrapper-bin"
mkdir -p "$BUILD/bin" "$SHIM_BIN"
export IR_LOCK_ROOT="$TMP/locks"
unset IR_WINDOW_MODE FLEET_WINDOW_MODE FLEET_RUN_DEFAULT_TIMEOUT IR_RUN_RESULT_FILE
unset IR_ACQUIRE_HOLDER_PID IR_ACQUIRE_HELD_VERB
export IR_TEST_DROP=present

CHILD_PATH="/usr/bin:/bin"
WRAPPER_PATH="$SHIM_BIN:$PATH"
WRAPPER_SEEN_PATH="$WRAPPER_PATH"
# On native Windows ir-run prepends the MSYS2 mingw64 runtime dir to its own
# PATH and wraps every verb-less run in the gpu lock. Pin the runtime dir to a
# synthetic one and opt out of the lock so the plain cases take the exec path
# on every host; the capture cases still go through ir-acquire.
case "$(uname -s)" in
    MINGW*|MSYS*|CYGWIN*)
        mkdir -p "$TMP/mingw64-bin"
        export IR_MSYS2_MINGW
        IR_MSYS2_MINGW="$(cygpath -w "$TMP/mingw64-bin")"
        WRAPPER_SEEN_PATH="$(cygpath -u "$IR_MSYS2_MINGW"):$WRAPPER_PATH"
        export IR_RUN_GL_EXCLUSIVE=0
        ;;
esac
PREFIX_LOG="$TMP/prefix.log"
ENV_LOG="$TMP/env.log"

# The probe and the stub run under the child PATH, so they name /bin/sh.
cat > "$BUILD/bin/probe-exe" <<'EOF'
#!/bin/sh
echo "exe-path=$PATH"
echo "exe-drop=${IR_TEST_DROP-unset}"
echo "exe-set=${IR_TEST_SET-unset}"
for arg in "$@"; do echo "exe-arg=$arg"; done
EOF
chmod +x "$BUILD/bin/probe-exe"

PREFIX_STUB="$TMP/prefix stub"
cat > "$PREFIX_STUB" <<EOF
#!/bin/sh
{
    echo "prefix-path=\$PATH"
    echo "prefix-drop=\${IR_TEST_DROP-unset}"
    echo "prefix-set=\${IR_TEST_SET-unset}"
    for arg in "\$@"; do echo "prefix-arg=\$arg"; done
} > "$PREFIX_LOG"
[ "\$1" = "-p" ] && shift 2
exec "\$@"
EOF
chmod +x "$PREFIX_STUB"

cat > "$SHIM_BIN/env" <<EOF
#!/bin/sh
echo "env-path=\$PATH" > "$ENV_LOG"
exec /usr/bin/env "\$@"
EOF
chmod +x "$SHIM_BIN/env"

PROFILE='(version 1) (deny "two words")'
CHILD_OPTS=(--child-unset IR_TEST_DROP --child-env "PATH=$CHILD_PATH"
            --child-env "IR_TEST_SET=a b=c"
            --child-prefix "$PREFIX_STUB" --child-prefix -p
            --child-prefix "$PROFILE")

run_ir() {
    rm -f "$PREFIX_LOG" "$ENV_LOG"
    OUT="$(PATH="$WRAPPER_PATH" "$IR_RUN" "$@" 2>&1)"
    RC=$?
    PREFIX_OUT="$(cat "$PREFIX_LOG" 2>/dev/null || true)"
    ENV_OUT="$(cat "$ENV_LOG" 2>/dev/null || true)"
}

check_isolated() {
    local shape="$1"
    assert_eq "$RC" "0" "$shape: exits 0"
    assert_contains "$OUT" \
        "ir-run: child isolation: prefix=$PREFIX_STUB unset=IR_TEST_DROP set=PATH,IR_TEST_SET" \
        "$shape: launch names the prefix and the variables"
    assert_absent "$(grep '^ir-run:' <<<"$OUT")" "a b=c" "$shape: ir-run lines never print a value"
    assert_eq "$PREFIX_OUT" "prefix-path=$CHILD_PATH
prefix-drop=unset
prefix-set=a b=c
prefix-arg=-p
prefix-arg=$PROFILE
prefix-arg=./probe-exe
prefix-arg=--child-env
prefix-arg=X=1
prefix-arg=two words" "$shape: prefix sees the child env and receives exact argv"
    assert_contains "$OUT" "exe-path=$CHILD_PATH" "$shape: executable sees the child PATH"
    assert_contains "$OUT" "exe-drop=unset" "$shape: executable sees the unset"
    assert_contains "$OUT" "exe-set=a b=c" "$shape: executable sees the set value intact"
    assert_contains "$OUT" "exe-arg=--child-env" "$shape: a program arg spelled like an option is untouched"
    assert_contains "$OUT" "exe-arg=two words" "$shape: a program arg with spaces is intact"
    assert_eq "$ENV_OUT" "env-path=$WRAPPER_SEEN_PATH" "$shape: wrapper keeps its original PATH"
}

echo "plain exec:"
run_ir "${CHILD_OPTS[@]}" --build-dir "$BUILD" probe-exe --child-env X=1 "two words"
check_isolated "plain exec"
assert_absent "$OUT" "RESULT=" "plain exec: no verdict (exec path)"

echo "result file (CLEAN):"
run_ir "${CHILD_OPTS[@]}" --result-file "$TMP/result.json" --build-dir "$BUILD" \
    probe-exe --child-env X=1 "two words"
check_isolated "result file"
assert_contains "$OUT" "ir-run: RESULT=CLEAN exe=probe-exe exit=0" "result file: CLEAN verdict"

echo "watchdog:"
run_ir "${CHILD_OPTS[@]}" --timeout 30 --build-dir "$BUILD" \
    probe-exe --child-env X=1 "two words"
check_isolated "watchdog"
assert_contains "$OUT" "ir-run: RESULT=CLEAN exe=probe-exe exit=0" "watchdog: CLEAN verdict"

echo "ir-acquire capture:"
run_ir "${CHILD_OPTS[@]}" --build-dir "$BUILD" \
    probe-exe --child-env X=1 "two words" --auto-screenshot 1
assert_contains "$OUT" "ir-acquire gpu --" "capture: launch is wrapped in ir-acquire"
assert_contains "$OUT" "ir-run: RESULT=CLEAN exe=probe-exe exit=0" "capture: CLEAN verdict"
assert_contains "$OUT" "exe-arg=--auto-screenshot" "capture: verb reaches the executable"
PREFIX_OUT="${PREFIX_OUT%$'\nprefix-arg=--auto-screenshot\nprefix-arg=1'}"
check_isolated "capture"

echo "ir-acquire capture under the watchdog:"
run_ir "${CHILD_OPTS[@]}" --timeout 30 --build-dir "$BUILD" \
    probe-exe --child-env X=1 "two words" --auto-screenshot 1
assert_contains "$OUT" "ir-acquire gpu --" "watchdog capture: launch is wrapped in ir-acquire"
assert_contains "$OUT" "ir-run: RESULT=CLEAN exe=probe-exe exit=0" "watchdog capture: CLEAN verdict"
PREFIX_OUT="${PREFIX_OUT%$'\nprefix-arg=--auto-screenshot\nprefix-arg=1'}"
check_isolated "watchdog capture"

echo "a crash behind the prefix keeps its exit code:"
printf '#!/bin/sh\nexit 7\n' > "$BUILD/bin/crash-exe"
chmod +x "$BUILD/bin/crash-exe"
run_ir "${CHILD_OPTS[@]}" --result-file "$TMP/crash.json" --build-dir "$BUILD" crash-exe
assert_eq "$RC" "7" "crash: exit code propagates through env and the prefix"
assert_contains "$OUT" "ir-run: RESULT=CRASH exe=crash-exe exit=7" "crash: CRASH verdict"

echo "no child options:"
run_ir --result-file "$TMP/plain.json" --build-dir "$BUILD" probe-exe
assert_eq "$RC" "0" "no options: exits 0"
assert_absent "$OUT" "child isolation" "no options: no isolation line"
assert_eq "$ENV_OUT" "" "no options: no env hop"
assert_contains "$OUT" "exe-drop=present" "no options: the executable inherits the wrapper env"

echo "malformed child options exit 2:"
for bad_opts in "--child-env NOEQUALS" "--child-env 1BAD=x" "--child-env =x" \
                "--child-unset BAD-NAME" "--child-unset A=B"; do
    # shellcheck disable=SC2086  # word-split on purpose: option + value
    run_ir $bad_opts --build-dir "$BUILD" probe-exe
    assert_eq "$RC" "2" "'$bad_opts' exits 2"
done
for flag in --child-env --child-unset --child-prefix; do
    run_ir --build-dir "$BUILD" "$flag"
    assert_eq "$RC" "2" "$flag with no argument exits 2"
done
run_ir --child-prefix "" --build-dir "$BUILD" probe-exe
assert_eq "$RC" "2" "--child-prefix with an empty argument exits 2"

summarize "ir-run child isolation"
