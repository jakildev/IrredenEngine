#!/usr/bin/env bash
# Verifies --info attributes live holders without relying on the host lock root.

set -uo pipefail

SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)
REPO_ROOT=$(cd "$SCRIPT_DIR/../../.." && pwd)
ACQUIRE="$REPO_ROOT/engine/tools/bin/ir-acquire"

# shellcheck source=lib_assert.sh
source "$SCRIPT_DIR/lib_assert.sh"

if [[ ! -x "$ACQUIRE" ]]; then
    echo "SKIP: ir-acquire not found at $ACQUIRE" >&2
    exit 3
fi

LOCK_ROOT=$(mktemp -d "${TMPDIR:-/tmp}/ir-acquire-info.XXXXXX")
PS_STUB=$(mktemp -d "${TMPDIR:-/tmp}/ir-acquire-info-ps.XXXXXX")
holder_pid=""
cleanup() {
    [[ -n "$holder_pid" ]] && kill -TERM -- "-$holder_pid" 2>/dev/null || true
    set +m
    [[ -n "$holder_pid" ]] && wait "$holder_pid" 2>/dev/null || true
    rm -rf "$LOCK_ROOT" "$PS_STUB"
}
trap cleanup EXIT

set -m
IR_LOCK_ROOT="$LOCK_ROOT" "$ACQUIRE" gpu -- sleep 30 >/dev/null 2>&1 &
holder_pid=$!
set +m
for _ in $(seq 1 20); do
    [[ -d "$LOCK_ROOT/gpu/lock" ]] && break
    sleep 0.1
done

cat > "$PS_STUB/ps" <<'EOF'
#!/usr/bin/env bash
if [[ "$*" == *"-o command="* && "$*" == *"-p "* ]]; then
    printf 'sleep 30\n'
    exit 0
fi
exit 2
EOF
chmod +x "$PS_STUB/ps"

info="$(PATH="$PS_STUB:$PATH" IR_LOCK_ROOT="$LOCK_ROOT" "$ACQUIRE" --info)"
assert_contains "$info" "gpu lock: held by pid $holder_pid" "--info names the gpu holder pid"
if grep -Eq "gpu lock: held by pid $holder_pid \\(age [0-9]+s, command: .*sleep 30\\)" <<<"$info"; then
    ok "--info names a non-negative holder age and command"
else
    bad "--info names a non-negative holder age and command"
    printf '        info: %s\n' "$info"
fi

cat > "$PS_STUB/ps" <<'EOF'
#!/usr/bin/env bash
for arg in "$@"; do
    [[ "$arg" == "-o" ]] && exit 2
done
exit 2
EOF
chmod +x "$PS_STUB/ps"

stub_info="$(PATH="$PS_STUB:$PATH" IR_LOCK_ROOT="$LOCK_ROOT" "$ACQUIRE" --info)"
stub_rc=$?
assert_eq "$stub_rc" "0" "--info succeeds when ps rejects -o"
assert_contains "$stub_info" "gpu lock: held by pid $holder_pid (age " "fallback keeps holder pid and age"
assert_absent "$stub_info" "command:" "fallback omits unavailable command"

summarize "ir-acquire info tests"
