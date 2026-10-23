#!/usr/bin/env bash
# Tests for ir-build's member-less static archive sweep: an archive an
# interrupted `ar` left holding no object members is deleted before the build
# so make regenerates it, a healthy archive is untouched, and a build dir
# that does not exist yet is skipped. fleet-build names itself in the log line.
#
# Hermetic: cmake is a recording stub on PATH that fails closed on any
# invocation shape ir-build does not issue here. The archives come from the
# host's real `ar`, so the member-less shape is whatever that `ar` leaves
# behind after its last member is deleted.

set -euo pipefail

SCRIPT_DIR=$(cd "$(dirname "$0")/../../.." && pwd)
# shellcheck source=lib_preflight.sh
source "$(dirname "$0")/lib_preflight.sh"
IR_BUILD="$SCRIPT_DIR/engine/tools/bin/ir-build"
FLEET_BUILD="$SCRIPT_DIR/scripts/fleet/fleet-build"

if [[ ! -x "$IR_BUILD" || ! -x "$FLEET_BUILD" ]]; then
    echo "SKIP: $IR_BUILD or $FLEET_BUILD not found or not executable" >&2
    exit 3
fi
CC_BIN="$(command -v cc || command -v gcc || command -v clang || true)"
if ! command -v ar >/dev/null 2>&1 || [[ -z "$CC_BIN" ]]; then
    echo "SKIP: ar or a C compiler not on PATH"
    exit 0
fi

IR_LOCK_ROOT="$(mktemp -d)/locks"
export IR_LOCK_ROOT

# shellcheck source=lib_assert.sh
source "$(dirname "$0")/lib_assert.sh"

TIMEOUT_CMD=""
if command -v timeout >/dev/null 2>&1; then
    TIMEOUT_CMD="timeout"
elif command -v gtimeout >/dev/null 2>&1; then
    TIMEOUT_CMD="gtimeout"
fi

FAKE="$(mktemp -d)"
trap 'rm -rf "$FAKE" "${IR_LOCK_ROOT%/locks}"' EXIT

STUB_BIN="$FAKE/stub-bin"
mkdir -p "$STUB_BIN"
cat > "$STUB_BIN/cmake" <<'STUB_EOF'
#!/usr/bin/env bash
set -euo pipefail
printf '%s\n' "$*" >> "$STUB_CALL_LOG"
case "${1:-}" in
    --preset|--build) exit 0 ;;
    *) echo "stub cmake: unmodeled invocation shape: $*" >&2; exit 99 ;;
esac
STUB_EOF
chmod +x "$STUB_BIN/cmake"
export STUB_CALL_LOG="$FAKE/cmake-calls.log"
: > "$STUB_CALL_LOG"

new_engine_fixture() {
    mkdir -p "$1/engine"
    touch "$1/CMakePresets.json"
    git -C "$1" init -q
}

# make_archive <path> <member-count> — member-count 0 builds the archive
# with one real object and deletes it, the shape an interrupted `ar` leaves
# (a symbol table and no members). The object is compiled because macOS `ar`
# silently drops a member that is not a Mach-O file.
make_archive() {
    local path="$1" count="$2" dir
    dir="$(mktemp -d "$FAKE/ar.XXXXXX")"
    mkdir -p "$(dirname "$path")"
    printf 'int fixture_symbol;\n' > "$dir/a.c"
    ( cd "$dir" && "$CC_BIN" -c a.c -o a.o )
    ( cd "$dir" && ar rc "$path" a.o )
    if (( count == 0 )); then
        ( cd "$dir" && ar d "$path" a.o )
    fi
}

# run_tool <tool> <cwd> <logfile> — combined stdout+stderr to <logfile>.
run_tool() {
    local tool="$1" cwd="$2" logfile="$3"
    (
        cd "$cwd" || exit 127
        export PATH="$STUB_BIN:$PATH"
        if [[ -n "$TIMEOUT_CMD" ]]; then
            "$TIMEOUT_CMD" 30 "$tool" --target FixtureTarget
        else
            "$tool" --target FixtureTarget
        fi
    ) >"$logfile" 2>&1
}

seed_tree() {
    mkdir -p "$1/build/engine" "$1/build/sub"
    touch "$1/build/CMakeCache.txt"
    make_archive "$1/build/engine/libBroken.a" 0
    make_archive "$1/build/sub/libHealthy.a" 1
}

echo "T1: ir-build removes a member-less archive and keeps a healthy one"
ENG1="$FAKE/eng1"; new_engine_fixture "$ENG1"; seed_tree "$ENG1"
rc=0; run_tool "$IR_BUILD" "$ENG1" "$FAKE/t1.log" || rc=$?
assert_eq "$rc" "0" "T1: build proceeds after the sweep"
assert_eq "$([[ -e "$ENG1/build/engine/libBroken.a" ]] && echo present || echo absent)" "absent" \
    "T1: member-less archive deleted"
assert_eq "$([[ -e "$ENG1/build/sub/libHealthy.a" ]] && echo present || echo absent)" "present" \
    "T1: healthy archive left alone"
assert_contains "$(cat "$FAKE/t1.log")" "ir-build: removed member-less archive " \
    "T1: removal is logged with the archive path"
assert_contains "$(cat "$FAKE/t1.log")" "/eng1/build/engine/libBroken.a" \
    "T1: log names the archive path"
assert_absent "$(cat "$FAKE/t1.log")" "libHealthy" "T1: healthy archive is not mentioned"

echo "T2: fleet-build names itself in the removal line"
ENG2="$FAKE/eng2"; new_engine_fixture "$ENG2"; seed_tree "$ENG2"
rc=0; run_tool "$FLEET_BUILD" "$ENG2" "$FAKE/t2.log" || rc=$?
assert_eq "$rc" "0" "T2: build proceeds after the sweep"
assert_contains "$(cat "$FAKE/t2.log")" "fleet-build: removed member-less archive " \
    "T2: fleet-build prefix on the removal line"

echo "T3: a build dir that does not exist yet is skipped"
ENG3="$FAKE/eng3"; new_engine_fixture "$ENG3"
rc=0; run_tool "$IR_BUILD" "$ENG3" "$FAKE/t3.log" || rc=$?
assert_eq "$rc" "0" "T3: first configure + build proceeds"
assert_absent "$(cat "$FAKE/t3.log")" "member-less" "T3: no sweep output without a build dir"

echo "T4: a clean tree produces no sweep output"
ENG4="$FAKE/eng4"; new_engine_fixture "$ENG4"
mkdir -p "$ENG4/build"; touch "$ENG4/build/CMakeCache.txt"
make_archive "$ENG4/build/libHealthy.a" 1
rc=0; run_tool "$IR_BUILD" "$ENG4" "$FAKE/t4.log" || rc=$?
assert_eq "$rc" "0" "T4: build proceeds"
assert_absent "$(cat "$FAKE/t4.log")" "member-less" "T4: nothing removed"
assert_eq "$([[ -e "$ENG4/build/libHealthy.a" ]] && echo present || echo absent)" "present" \
    "T4: healthy archive kept"

summarize "ir-build member-less archive sweep tests"
