#!/usr/bin/env bash
# The gpu lock an ir-run holds can be lent to a child run and taken back
# (IREngine.handOff): ir-acquire exports a lease token, `--lease-release`
# frees the lock for exactly one nested ir-run, and `--lease-reacquire`
# waits for that child and restores the lock to the original ir-acquire shell.
#
# The wrapper and the lock helpers are real; the engine processes are bash
# stubs that drive the lease verbs the way the engine's handoff controller
# does. Every lock lives under an isolated IR_LOCK_ROOT.
set -uo pipefail

SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)
source "$SCRIPT_DIR/lib_preflight.sh"
REPO_ROOT=$(cd "$SCRIPT_DIR/../../.." && pwd)
RUN="$REPO_ROOT/engine/tools/bin/ir-run"
ACQUIRE="$REPO_ROOT/engine/tools/bin/ir-acquire"
source "$SCRIPT_DIR/lib_assert.sh"

if [[ ! -x "$RUN" || ! -x "$ACQUIRE" ]]; then
    echo "SKIP: ir-run or ir-acquire is missing" >&2
    exit 3
fi

FIXTURE=$(mktemp -d "${TMPDIR:-/tmp}/ir-run-handoff.XXXXXX")
cleanup_fixture() {
    jobs -p | xargs kill 2>/dev/null || true
    rm -rf "$FIXTURE"
}
trap cleanup_fixture EXIT
BUILD="$FIXTURE/build"
mkdir -p "$BUILD"

unset IR_RUN_LOCK_TOKEN IR_ACQUIRE_TOOL IR_ACQUIRE_HOLDER_PID IR_ACQUIRE_HELD_VERB
unset IR_ACQUIRE_HOLDER_WINPID IR_QUIET_OWNER IR_WINDOW_MODE FLEET_WINDOW_MODE
unset FLEET_RUN_DEFAULT_TIMEOUT IRREDEN_BUILD_DIR IR_RUN_WRAPPER IR_RUN_BASH
unset IR_RUN_HOST_CLOSE_IDENTITY IR_RUN_HOST_CLOSE_CLOCK IR_RUN_HOST_CLOSE_QUERY
export IR_LOCK_ROOT="$FIXTURE/locks"
export IR_QUEUE_TIMEOUT=30
export HANDOFF_DIR="$FIXTURE/handoff"
export HANDOFF_LOG="$HANDOFF_DIR/parent.log"
mkdir -p "$HANDOFF_DIR"
LOCK="$IR_LOCK_ROOT/gpu/lock"

# wait_for <file> — bounded wait used by the stubs and the arms below.
WAIT_FOR='wait_for() { local i; for (( i = 0; i < 200; i++ )); do [[ -e "$1" ]] && return 0; sleep 0.1; done; return 1; }'

cat > "$BUILD/handoff-parent" <<EOF
#!/usr/bin/env bash
set -u
$WAIT_FOR
EOF
cat >> "$BUILD/handoff-parent" <<'EOF'
log() { printf '%s\n' "$*" >> "$HANDOFF_LOG"; }
holder() { cat "$IR_LOCK_ROOT/gpu/lock/pid" 2>/dev/null || echo none; }
lease() { "$IR_ACQUIRE_TOOL" "$@" 2>> "$HANDOFF_DIR/lease.err"; }
log "token=${IR_RUN_LOCK_TOKEN:-none}"
log "wrapper=${IR_RUN_WRAPPER:-none}"
log "initial_holder=$(holder) acquire_holder=${IR_ACQUIRE_HOLDER_PID:-none}"
lease --lease-reacquire "$IR_RUN_LOCK_TOKEN"; log "reacquire_before_release_rc=$?"
lease --lease-release 0123456789abcdef0123456789abcdef; log "wrong_token_rc=$?"
lease --lease-release "../$IR_RUN_LOCK_TOKEN"; log "malformed_token_rc=$?"
log "holder_after_refusals=$(holder)"
outsider() {  # a gpu contender outside this process tree, which no hold covers
    env -u IR_ACQUIRE_HOLDER_PID -u IR_ACQUIRE_HELD_VERB -u IR_ACQUIRE_HOLDER_WINPID \
        "$IR_ACQUIRE_TOOL" --nonblock gpu -- true 2>/dev/null
}
outsider; log "contender_before_release_rc=$?"
lease --lease-release "$IR_RUN_LOCK_TOKEN"; log "release_rc=$?"
"$IR_RUN_WRAPPER" handoff-child --auto-screenshot 1 > "$HANDOFF_DIR/child.out" 2>&1 &
child=$!
if wait_for "$HANDOFF_DIR/child-entered"; then log "child_entered=yes"; else log "child_entered=no"; fi
log "holder_during_child=$(holder)"
lease --lease-release "$IR_RUN_LOCK_TOKEN"; log "double_release_rc=$?"
outsider; log "second_contender_rc=$?"
log "holder_after_contender=$(holder)"
lease --lease-reacquire "$IR_RUN_LOCK_TOKEN" &
reacquire=$!
sleep 1
if kill -0 "$reacquire" 2>/dev/null; then log "reacquire_waiting=yes"; else log "reacquire_waiting=no"; fi
touch "$HANDOFF_DIR/child-go"
wait "$reacquire"; log "reacquire_rc=$?"
if [[ -e "$HANDOFF_DIR/child-exited" ]]; then log "child_exited_before_reacquire=yes"; else log "child_exited_before_reacquire=no"; fi
wait "$child"; log "child_rc=$?"
log "final_holder=$(holder)"
lease --lease-reacquire "$IR_RUN_LOCK_TOKEN"; log "double_reacquire_rc=$?"
exit 0
EOF

cat > "$BUILD/handoff-child" <<EOF
#!/usr/bin/env bash
$WAIT_FOR
EOF
cat >> "$BUILD/handoff-child" <<'EOF'
printf 'child_holder=%s child_acquire_holder=%s\n' \
    "$(cat "$IR_LOCK_ROOT/gpu/lock/pid" 2>/dev/null || echo none)" \
    "${IR_ACQUIRE_HOLDER_PID:-none}" > "$HANDOFF_DIR/child.log"
touch "$HANDOFF_DIR/child-entered"
wait_for "$HANDOFF_DIR/child-go" || exit 9
touch "$HANDOFF_DIR/child-exited"
exit 0
EOF

cat > "$BUILD/env-probe" <<'EOF'
#!/usr/bin/env bash
printf 'token=%s\n' "${IR_RUN_LOCK_TOKEN:-none}"
printf 'build_dir=%s\n' "${IRREDEN_BUILD_DIR:-none}"
EOF
chmod +x "$BUILD/handoff-parent" "$BUILD/handoff-child" "$BUILD/env-probe"

field() {  # field <key> — the value the parent logged for <key>
    sed -n "s/^$1=\([^ ]*\).*/\1/p" "$HANDOFF_LOG" | tail -1
}

echo "Transfer: tokenized release lends the lock to one nested ir-run"
OUT=$("$RUN" --build-dir "$BUILD" handoff-parent --auto-screenshot 1 2>&1)
RC=$?
assert_eq "$RC" "0" "parent run exits 0"
TOKEN=$(field token)
assert_eq "$([[ "$TOKEN" =~ ^[0-9a-f]{32}$ ]] && echo yes || echo "$TOKEN")" "yes" \
    "ir-acquire gpu exports an opaque lease token"
EXPECTED_WRAPPER="$(cd "$(dirname "$RUN")" && pwd)/ir-run"
case "$(uname -s)" in
    MINGW*|MSYS*|CYGWIN*) EXPECTED_WRAPPER="$(cygpath -m "$EXPECTED_WRAPPER")" ;;  # ir-run hands the exe a Windows-form path
esac
assert_eq "$(field wrapper)" "$EXPECTED_WRAPPER" \
    "ir-run exports its canonical wrapper path"
PARENT_HOLDER=$(sed -n 's/.*acquire_holder=\([0-9]*\).*/\1/p' "$HANDOFF_LOG" | head -1)
assert_eq "$(field initial_holder)" "$PARENT_HOLDER" "parent's ir-acquire owns gpu/lock at start"
assert_eq "$(field reacquire_before_release_rc)" "1" "reacquire before release is refused"
assert_eq "$(field wrong_token_rc)" "1" "an unknown token is refused"
assert_eq "$(field malformed_token_rc)" "2" "a malformed token is a usage error"
assert_eq "$(field holder_after_refusals)" "$PARENT_HOLDER" "refusals leave the lock with the parent"
assert_eq "$(field contender_before_release_rc)" "1" "a competing run cannot enter before the release"
assert_eq "$(field release_rc)" "0" "the tokenized release succeeds"
assert_eq "$(field child_entered)" "yes" "the release lets the nested ir-run enter"
assert_contains "$(cat "$HANDOFF_DIR/child.out")" "ir-acquire gpu --" \
    "the nested ir-run queues on its own lock once the parent's no longer covers it"
CHILD_HOLDER=$(sed -n 's/.*child_acquire_holder=\([0-9]*\).*/\1/p' "$HANDOFF_DIR/child.log")
assert_eq "$(field holder_during_child)" "$CHILD_HOLDER" "the nested run holds gpu/lock under its own ir-acquire"
assert_eq "$([[ -n "$CHILD_HOLDER" && "$CHILD_HOLDER" != "$PARENT_HOLDER" ]] && echo yes || echo no)" \
    "yes" "the nested run's holder is not the parent's"
assert_eq "$(field double_release_rc)" "1" "a second release is refused"
assert_eq "$(field second_contender_rc)" "1" "a second contender cannot enter while the child holds the lock"
assert_eq "$(field holder_after_contender)" "$CHILD_HOLDER" "refused transitions never touch the child's lock"
assert_eq "$(field reacquire_waiting)" "yes" "the parent's reacquire waits while the child runs"
assert_eq "$(field reacquire_rc)" "0" "the reacquire succeeds once the child exits"
assert_eq "$(field child_exited_before_reacquire)" "yes" "the reacquire completed only after the child exited"
assert_eq "$(field child_rc)" "0" "the nested ir-run exits 0"
assert_eq "$(field final_holder)" "$PARENT_HOLDER" "the parent's ir-acquire owns gpu/lock again"
assert_eq "$(field double_reacquire_rc)" "1" "a second reacquire is refused"
assert_contains "$(cat "$HANDOFF_DIR/child.out")" "RESULT=CLEAN exe=handoff-child" \
    "the nested run reports its own verdict"
assert_contains "$OUT" "RESULT=CLEAN exe=handoff-parent" "the parent keeps its clean-exit verdict"
assert_eq "$([[ -e "$LOCK" ]] && echo held || echo free)" "free" "the parent's exit releases the reacquired lock"
assert_eq "$(ls "$IR_LOCK_ROOT/leases" 2>/dev/null | wc -l | tr -d ' ')" "0" "the parent's exit drops its lease"

# start_holder <script-body> — `ir-acquire gpu` around a bash body in the
# background; sets HOLDER_PID (the ir-acquire shell) and waits for the body
# to write $HANDOFF_DIR/holder-token.
start_holder() {
    rm -f "$HANDOFF_DIR/holder-token" "$HANDOFF_DIR/holder-go"
    "$ACQUIRE" gpu -- bash -c "$WAIT_FOR; $1" &
    HOLDER_PID=$!
    local i
    for (( i = 0; i < 200; i++ )); do
        [[ -s "$HANDOFF_DIR/holder-token" ]] && break
        sleep 0.1
    done
    HOLDER_TOKEN=$(cat "$HANDOFF_DIR/holder-token" 2>/dev/null || true)
}

echo "Dead owner: a lease whose ir-acquire died cannot be released or reacquired"
start_holder 'echo "$IR_RUN_LOCK_TOKEN" > "$HANDOFF_DIR/holder-token"; wait_for "$HANDOFF_DIR/holder-go"'
assert_eq "$(cat "$LOCK/pid" 2>/dev/null)" "$HOLDER_PID" "the holder owns gpu/lock"
kill -9 "$HOLDER_PID" 2>/dev/null
wait "$HOLDER_PID" 2>/dev/null
touch "$HANDOFF_DIR/holder-go"
"$ACQUIRE" --lease-release "$HOLDER_TOKEN" 2>/dev/null
assert_eq "$?" "1" "release against a dead owner is refused"
assert_eq "$(cat "$LOCK/pid" 2>/dev/null)" "$HOLDER_PID" "the refused release leaves the stale lock as it was"
rm -rf "$LOCK" "$IR_LOCK_ROOT/leases" "$IR_LOCK_ROOT/.held"

start_holder '"$IR_ACQUIRE_TOOL" --lease-release "$IR_RUN_LOCK_TOKEN"; echo "$IR_RUN_LOCK_TOKEN" > "$HANDOFF_DIR/holder-token"; wait_for "$HANDOFF_DIR/holder-go"'
assert_eq "$([[ -e "$LOCK" ]] && echo held || echo free)" "free" "the holder lent its lock"
kill -9 "$HOLDER_PID" 2>/dev/null
wait "$HOLDER_PID" 2>/dev/null
touch "$HANDOFF_DIR/holder-go"
"$ACQUIRE" --timeout 5 --lease-reacquire "$HOLDER_TOKEN" 2>/dev/null
assert_eq "$?" "1" "reacquire for a dead owner is refused"
assert_eq "$([[ -e "$LOCK" ]] && echo held || echo free)" "free" "the refused reacquire stamps no lock"
rm -rf "$IR_LOCK_ROOT/leases" "$IR_LOCK_ROOT/.held"

echo "Cleanup while released: the owner's exit drops the lent lease"
start_holder '"$IR_ACQUIRE_TOOL" --lease-release "$IR_RUN_LOCK_TOKEN"; echo "$IR_RUN_LOCK_TOKEN" > "$HANDOFF_DIR/holder-token"; wait_for "$HANDOFF_DIR/holder-go"'
assert_eq "$([[ -d "$IR_LOCK_ROOT/leases/$HOLDER_TOKEN/released" ]] && echo released || echo other)" \
    "released" "the lease records the released state"
touch "$HANDOFF_DIR/holder-go"
wait "$HOLDER_PID"
assert_eq "$?" "0" "the released holder exits 0"
assert_eq "$([[ -e "$IR_LOCK_ROOT/leases/$HOLDER_TOKEN" ]] && echo kept || echo dropped)" "dropped" \
    "the exit trap drops the released lease"
assert_eq "$([[ -e "$LOCK" ]] && echo held || echo free)" "free" "no gpu lock outlives the released holder"
assert_eq "$(ls "$IR_LOCK_ROOT/.held" 2>/dev/null | wc -l | tr -d ' ')" "0" "no held ledger outlives the released holder"

echo "Owner exit during release: a contender's lock survives the release"
GATE="$HANDOFF_DIR/release-gate"
rm -f "$GATE.reached" "$GATE.go" "$HANDOFF_DIR/contender-in" "$HANDOFF_DIR/contender-go"
start_holder 'echo "$IR_RUN_LOCK_TOKEN" > "$HANDOFF_DIR/holder-token"; wait_for "$HANDOFF_DIR/holder-go"'
IR_LEASE_TEST_GATE="$GATE" "$ACQUIRE" --lease-release "$HOLDER_TOKEN" 2>/dev/null &
RELEASE_PID=$!
for (( i = 0; i < 200; i++ )); do [[ -e "$GATE.reached" ]] && break; sleep 0.1; done
assert_eq "$([[ -e "$GATE.reached" ]] && echo parked || echo missing)" "parked" \
    "the release passed its checks and parked before removing the lock"
touch "$HANDOFF_DIR/holder-go"
# The owner's command has returned and its exit trap is running: it either
# parks on the transition (marked by `dropping`) or exits outright.
for (( i = 0; i < 200; i++ )); do
    [[ -e "$IR_LOCK_ROOT/leases/$HOLDER_TOKEN/dropping" ]] && break
    kill -0 "$HOLDER_PID" 2>/dev/null || break
    sleep 0.1
done
assert_eq "$(cat "$LOCK/pid" 2>/dev/null)" "$HOLDER_PID" \
    "the exiting owner keeps its lock while the release is in flight"
"$ACQUIRE" --timeout 20 gpu -- bash -c \
    "$WAIT_FOR; touch \"\$HANDOFF_DIR/contender-in\"; wait_for \"\$HANDOFF_DIR/contender-go\"" &
CONTENDER_PID=$!
for (( i = 0; i < 20; i++ )); do [[ -e "$HANDOFF_DIR/contender-in" ]] && break; sleep 0.1; done
assert_eq "$([[ -e "$HANDOFF_DIR/contender-in" ]] && echo entered || echo queued)" "queued" \
    "a contender cannot enter while the release is in flight"
touch "$GATE.go"
wait "$RELEASE_PID"
assert_eq "$?" "0" "the parked release completes against the live owner"
wait "$HOLDER_PID"
assert_eq "$?" "0" "the owner exits 0 once the release is done"
for (( i = 0; i < 200; i++ )); do [[ -e "$HANDOFF_DIR/contender-in" ]] && break; sleep 0.1; done
assert_eq "$([[ -e "$HANDOFF_DIR/contender-in" ]] && echo entered || echo queued)" "entered" \
    "the contender enters once the owner has exited"
assert_eq "$(cat "$LOCK/pid" 2>/dev/null)" "$CONTENDER_PID" \
    "the contender's lock survives the completed release"
touch "$HANDOFF_DIR/contender-go"
wait "$CONTENDER_PID"
assert_eq "$([[ -e "$IR_LOCK_ROOT/leases/$HOLDER_TOKEN" ]] && echo kept || echo dropped)" "dropped" \
    "the owner's exit still drops the lease"

echo "Owner exit during reacquire: the waiting reacquire yields to the exit"
rm -f "$HANDOFF_DIR/contender-in" "$HANDOFF_DIR/contender-go"
start_holder '"$IR_ACQUIRE_TOOL" --lease-release "$IR_RUN_LOCK_TOKEN"; echo "$IR_RUN_LOCK_TOKEN" > "$HANDOFF_DIR/holder-token"; wait_for "$HANDOFF_DIR/holder-go"'
"$ACQUIRE" gpu -- bash -c \
    "$WAIT_FOR; touch \"\$HANDOFF_DIR/contender-in\"; wait_for \"\$HANDOFF_DIR/contender-go\"" &
CONTENDER_PID=$!
for (( i = 0; i < 200; i++ )); do [[ -e "$HANDOFF_DIR/contender-in" ]] && break; sleep 0.1; done
"$ACQUIRE" --timeout 60 --lease-reacquire "$HOLDER_TOKEN" 2>/dev/null &
REACQUIRE_PID=$!
for (( i = 0; i < 200; i++ )); do
    [[ -d "$IR_LOCK_ROOT/leases/$HOLDER_TOKEN/transition" ]] && break
    sleep 0.1
done
touch "$HANDOFF_DIR/holder-go"
SECONDS=0
wait "$HOLDER_PID"
assert_eq "$?" "0" "the owner exits 0 while its reacquire waits"
assert_eq "$(( SECONDS < 10 ))" "1" "the owner's exit does not wait out the reacquire timeout"
wait "$REACQUIRE_PID"
assert_eq "$?" "1" "the waiting reacquire is refused once the owner exits"
assert_eq "$(cat "$LOCK/pid" 2>/dev/null)" "$CONTENDER_PID" "the contender's lock is untouched"
touch "$HANDOFF_DIR/contender-go"
wait "$CONTENDER_PID"
assert_eq "$([[ -e "$LOCK" ]] && echo held || echo free)" "free" "no gpu lock outlives the contender"

echo "Token scope: only a run its gpu lock covers sees a token"
OUT=$("$ACQUIRE" cpu 1 -- bash -c 'echo "token=${IR_RUN_LOCK_TOKEN:-none}"' 2>&1)
assert_contains "$OUT" "token=none" "a cpu-only hold exports no lease token"
OUT=$(IR_RUN_LOCK_TOKEN=0123456789abcdef0123456789abcdef IR_RUN_GL_EXCLUSIVE=0 \
    "$RUN" --build-dir "$BUILD" env-probe 2>&1)
assert_contains "$OUT" "token=none" "ir-run drops a token no lock of its own covers"
assert_contains "$OUT" "build_dir=$(cd "$BUILD" && pwd)" "ir-run exports the build tree a child resolves in"

summarize "ir-run handoff lock tests"
