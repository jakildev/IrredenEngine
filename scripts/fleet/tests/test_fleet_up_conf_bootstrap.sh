#!/usr/bin/env bash
# Tests for fleet-up's first-run conf bootstrap.
#
# fleet-up writes ~/.fleet/fleet-up.conf from fleet-up.conf.sample on
# first run so the operator has a documented file to edit. T-135's
# acceptance criterion: "~/.fleet/fleet-up.conf exists with documented
# defaults after `fleet-up` runs."
#
# Covers:
#   - bootstrap creates the conf when absent
#   - bootstrap is idempotent (existing operator edits not overwritten)
#   - bootstrap is a no-op when the sample is missing (graceful degrade)

set -euo pipefail

SCRIPT_DIR=$(cd "$(dirname "$0")/.." && pwd)
FLEET_UP="$SCRIPT_DIR/fleet-up"
FLEET_UP_SAMPLE="$SCRIPT_DIR/fleet-up.conf.sample"

if [[ ! -x "$FLEET_UP" ]]; then
    echo "test setup: fleet-up not found at $FLEET_UP" >&2
    exit 1
fi
if [[ ! -f "$FLEET_UP_SAMPLE" ]]; then
    echo "test setup: fleet-up.conf.sample not found at $FLEET_UP_SAMPLE" >&2
    exit 1
fi

PASS=0
FAIL=0
TMPROOT=""

cleanup() {
    [[ -n "$TMPROOT" && -d "$TMPROOT" ]] && rm -rf "$TMPROOT"
}
trap cleanup EXIT

assert_eq() {
    local actual="$1" expected="$2" msg="$3"
    if [[ "$actual" == "$expected" ]]; then
        PASS=$((PASS + 1))
        echo "  ok: $msg"
    else
        FAIL=$((FAIL + 1))
        echo "  FAIL: $msg"
        echo "        expected: $expected"
        echo "        actual:   $actual"
    fi
}

assert_file_exists() {
    local path="$1" msg="$2"
    if [[ -f "$path" ]]; then
        PASS=$((PASS + 1))
        echo "  ok: $msg"
    else
        FAIL=$((FAIL + 1))
        echo "  FAIL: $msg ($path missing)"
    fi
}

# Build a sandbox: tempdir for FLEET_CONF target, PATH stripped of
# tmux so fleet-up's tmux check exits early — the conf bootstrap
# section runs before the check, so the test still observes its
# side effect.
TMPROOT=$(mktemp -d)

# Run fleet-up with no tmux on PATH. PATH=/usr/bin keeps the basic
# shell tools (cp, mkdir, dirname, readlink) reachable while ensuring
# tmux isn't (its homebrew/apt install paths are excluded). fleet-up
# exits 1 at the tmux check immediately after running the conf
# bootstrap — both observable via the assertion below. The `|| true`
# swallows the exit so set -e doesn't kill the test.
run_fleet_up_bootstrap_only() {
    local conf_path="$1"
    PATH=/usr/bin:/bin FLEET_CONF="$conf_path" \
        "$FLEET_UP" >/dev/null 2>&1 || true
}

# --- Test 1: bootstrap creates the conf when absent -----------------------
echo "T1: conf is absent — bootstrap copies the sample"
CONF1="$TMPROOT/test1.conf"
[[ ! -e "$CONF1" ]] || { echo "  setup error: $CONF1 already exists" >&2; exit 1; }
run_fleet_up_bootstrap_only "$CONF1"
assert_file_exists "$CONF1" "conf created at $CONF1"
if [[ -f "$CONF1" ]]; then
    if diff -q "$CONF1" "$FLEET_UP_SAMPLE" >/dev/null 2>&1; then
        PASS=$((PASS + 1))
        echo "  ok: conf matches sample byte-for-byte"
    else
        FAIL=$((FAIL + 1))
        echo "  FAIL: conf does not match sample"
    fi
fi

# --- Test 2: bootstrap is idempotent --------------------------------------
echo "T2: conf already exists — bootstrap leaves it alone"
CONF2="$TMPROOT/test2.conf"
mkdir -p "$(dirname "$CONF2")"
cat >"$CONF2" <<'EOF'
# operator-edited conf
FLEET_CONCURRENCY_OPUS_WORKER=5
EOF
operator_content=$(cat "$CONF2")
run_fleet_up_bootstrap_only "$CONF2"
after=$(cat "$CONF2")
assert_eq "$after" "$operator_content" "operator-edited conf is preserved"

# --- Test 3: target directory does not exist -------------------------------
echo "T3: target dir missing — bootstrap creates it"
CONF3="$TMPROOT/nested/dir/test3.conf"
[[ ! -d "$(dirname "$CONF3")" ]] || { echo "  setup error: nested dir already exists" >&2; exit 1; }
run_fleet_up_bootstrap_only "$CONF3"
assert_file_exists "$CONF3" "conf created in newly-made directory"

# --- Test 4: sample missing — bootstrap is a no-op (graceful degrade) ------
# Copy fleet-up to an isolated dir without a fleet-up.conf.sample alongside,
# then run the copy. The bootstrap's _fleet_up_sample resolution lands in
# the isolated dir, finds no sample, and skips the cp without error.
echo "T4: sample missing — bootstrap skips quietly"
CONF4="$TMPROOT/test4.conf"
ISOLATED_DIR="$TMPROOT/isolated"
mkdir -p "$ISOLATED_DIR"
cp "$FLEET_UP" "$ISOLATED_DIR/fleet-up"
[[ ! -e "$CONF4" ]] || { echo "  setup error: $CONF4 already exists" >&2; exit 1; }
[[ ! -f "$ISOLATED_DIR/fleet-up.conf.sample" ]] || { echo "  setup error: sample present in isolated dir" >&2; exit 1; }
PATH=/usr/bin:/bin FLEET_CONF="$CONF4" \
    "$ISOLATED_DIR/fleet-up" >/dev/null 2>&1 || true
if [[ ! -f "$CONF4" ]]; then
    PASS=$((PASS + 1))
    echo "  ok: conf not created when sample is missing"
else
    FAIL=$((FAIL + 1))
    echo "  FAIL: conf was created despite missing sample ($CONF4)"
fi

echo ""
echo "PASS: $PASS  FAIL: $FAIL"
if (( FAIL > 0 )); then
    exit 1
fi
