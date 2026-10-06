#!/usr/bin/env bash
# fleet-build --detach: hands the rewritten ir-build argv to the adjacent
# fleet-jobs build profile; the foreground path still execs ir-build.
#
# The shim is copied into a temp checkout beside a recording fleet-jobs stub
# and a recording ir-build stub, so neither real tool runs.

set -euo pipefail

SCRIPT_DIR=$(cd "$(dirname "$0")/.." && pwd)
# shellcheck source=lib_preflight.sh
source "$(dirname "$0")/lib_preflight.sh"
SUBJECT="$SCRIPT_DIR/fleet-build"
if [[ ! -f "$SUBJECT" ]]; then
    echo "SKIP: $SUBJECT not found" >&2
    exit 3
fi
# shellcheck source=lib_assert.sh
source "$(dirname "$0")/lib_assert.sh"

TMP=$(mktemp -d "${TMPDIR:-/tmp}/fleet-build-detach.XXXXXX")
trap 'rm -rf "$TMP"' EXIT
mkdir -p "$TMP/scripts/fleet" "$TMP/engine/tools/bin"
cp "$SUBJECT" "$TMP/scripts/fleet/fleet-build"
cat > "$TMP/scripts/fleet/fleet-jobs" <<EOF
#!/usr/bin/env bash
printf '%s\n' "\$@" > "$TMP/jobs.argv"
exit 0
EOF
cat > "$TMP/engine/tools/bin/ir-build" <<EOF
#!/usr/bin/env bash
printf '%s\n' "\$@" > "$TMP/ir-build.argv"
exit 5
EOF
chmod +x "$TMP/scripts/fleet/fleet-build" "$TMP/scripts/fleet/fleet-jobs" \
    "$TMP/engine/tools/bin/ir-build"

run() {
    rm -f "$TMP/jobs.argv" "$TMP/ir-build.argv"
    rc=0
    (cd "$TMP" && "$TMP/scripts/fleet/fleet-build" "$@") >/dev/null 2>&1 || rc=$?
}
argv_of() { [[ -f "$1" ]] && tr '\n' ' ' < "$1" || echo "<not called>"; }

echo "T1: --detach --target format -> one build-profile job, rewritten, no --detach"
run --detach --target format
assert_eq "$rc" 0 "detached start exits with fleet-jobs' status"
assert_eq "$(argv_of "$TMP/jobs.argv")" \
    "start build --name fleet-build -- --target format-changed " \
    "fleet-jobs gets the build profile with the format-changed rewrite"
assert_eq "$(argv_of "$TMP/ir-build.argv")" "<not called>" "ir-build is not run in the foreground"

echo "T2: --detach after other options and the = spelling"
run --target=format --detach -- -j2
assert_eq "$(argv_of "$TMP/jobs.argv")" \
    "start build --name fleet-build -- --target=format-changed -- -j2 " \
    "wrapper option anywhere before -- is consumed"

echo "T3: --detach after CMake's -- belongs to the native tool"
run --target IRShapeDebug -- --detach
assert_eq "$(argv_of "$TMP/jobs.argv")" "<not called>" "no job started"
assert_eq "$(argv_of "$TMP/ir-build.argv")" "--target IRShapeDebug -- --detach " \
    "--detach after -- is forwarded to ir-build"
assert_eq "$rc" 5 "foreground exit code preserved"

echo "T4: foreground control execs ir-build with the rewrite"
run --target format
assert_eq "$(argv_of "$TMP/ir-build.argv")" "--target format-changed " "foreground rewrite"
assert_eq "$(argv_of "$TMP/jobs.argv")" "<not called>" "foreground never touches fleet-jobs"
assert_eq "$rc" 5 "foreground exit code preserved"

echo "T5: --detach without an adjacent fleet-jobs fails closed"
rm "$TMP/scripts/fleet/fleet-jobs"
run --detach --target IRShapeDebug
assert_eq "$rc" 1 "missing fleet-jobs exits 1"
assert_eq "$(argv_of "$TMP/ir-build.argv")" "<not called>" "no silent foreground fallback"

summarize "fleet-build --detach tests"
