#!/usr/bin/env bash
# The ir-acquire lock primitive hands each lock to exactly one caller: a free
# lock, a dead holder's lock, and ir_sweep_stale's take of one. Contenders park
# on FIFO barriers inside the helper's own seams (_ir_holder_alive, mkdir,
# _ir_lock_owned_by), so each interleaving runs the same way every time.
#
# The mixed rows run the helper as it was before the `owner` record
# (fixtures/concurrency_helpers_pre_owner.sh, never edited) against the current
# one on one lock root, in a separate bash: _IR_HELPERS_LOADED makes a second
# `source` in this shell a no-op. A missing fixture fails those rows and the
# run goes on, so a positive control against an older tree still grades the
# rest.
#
# On a host whose /bin/bash is 3.x, every row runs again under it.

set -uo pipefail

SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)
REPO_ROOT=$(cd "$SCRIPT_DIR/../../.." && pwd)
HELPERS="$REPO_ROOT/engine/tools/lib/concurrency_helpers.sh"
ACQUIRE="$REPO_ROOT/engine/tools/bin/ir-acquire"
PRE_OWNER="$SCRIPT_DIR/fixtures/concurrency_helpers_pre_owner.sh"

# shellcheck source=lib_assert.sh
source "$SCRIPT_DIR/lib_assert.sh"

if [[ ! -f "$HELPERS" ]]; then
    echo "SKIP: concurrency_helpers.sh not found at $HELPERS" >&2
    exit 3
fi

ROOT=$(mktemp -d "${TMPDIR:-/tmp}/ir-lock-transfer.XXXXXX")
cleanup() {
    local p
    if [[ -f "$ROOT/live.pids" ]]; then
        while read -r p; do kill "$p" 2>/dev/null || true; done < "$ROOT/live.pids"
    fi
    rm -rf "$ROOT"
}
trap cleanup EXIT

export IR_LOCK_ROOT="$ROOT/locks"
unset IR_QUIET_OWNER _IR_HELPERS_LOADED
mkdir -p "$IR_LOCK_ROOT"
# shellcheck source=../../../engine/tools/lib/concurrency_helpers.sh
source "$HELPERS"
set +e

LOCK="$IR_LOCK_ROOT/gpu/lock"
echo "bash $BASH_VERSION"

live() {
    sleep 300 >/dev/null 2>&1 &
    echo "$!" >> "$ROOT/live.pids"
    echo "$!"
}
dead_pid() {
    sh -c 'exit 0' &
    local p=$!
    wait "$p"
    echo "$p"
}
DEAD1=$(dead_pid)
DEAD2=$(dead_pid)
while [[ "$DEAD2" == "$DEAD1" ]]; do DEAD2=$(dead_pid); done

holder() { _ir_lock_holder "$LOCK" 2>/dev/null || echo none; }
lock_bytes() { cat "$LOCK/owner" 2>/dev/null; echo "|"; cat "$LOCK/pid" 2>/dev/null; }
lock_listing() { ls -A "$LOCK" 2>/dev/null | tr '\n' ' '; echo "|"; cat "$LOCK/pid" 2>/dev/null; }
reset_gpu() { rm -rf "$IR_LOCK_ROOT/gpu" "$IR_LOCK_ROOT/.held"; mkdir -p "$IR_LOCK_ROOT/gpu"; }
winners() { local n=0 rc; for rc in "$@"; do (( rc == 0 )) && n=$(( n + 1 )); done; echo "$n"; }

newbar() { rm -f "$ROOT/parked.$1" "$ROOT/bar.$1"; mkfifo "$ROOT/bar.$1"; }
# _park <name> — the first call parks until the suite opens the barrier.
_park() {
    [[ -e "$ROOT/parked.$1" ]] && return 0
    : > "$ROOT/parked.$1"
    read -r _ < "$ROOT/bar.$1"
}
wait_parked() {
    local i
    for (( i = 0; i < 200; i++ )); do
        [[ -e "$ROOT/parked.$1" ]] && return 0
        sleep 0.05
    done
    return 1
}
# open_bar <name> <pid> — releases the parked contender and returns its exit
# status. A contender that never reached the barrier fails the row instead of
# hanging the suite on a FIFO nobody reads.
open_bar() {
    local rc=0
    if [[ -e "$ROOT/parked.$1" ]]; then
        echo go > "$ROOT/bar.$1"
    else
        bad "barrier '$1' was never reached"
        kill "$2" 2>/dev/null
    fi
    wait "$2" || rc=$?
    return "$rc"
}
# Seams, armed inside a contender's subshell.
park_after_classify() {
    eval "$(declare -f _ir_holder_alive | sed '1s/_ir_holder_alive/_ir_orig_holder_alive/')"
    _ir_holder_alive() {
        local r=0
        _ir_orig_holder_alive "$@" || r=$?
        _park alive
        return "$r"
    }
}
park_after_owned_check() {
    eval "$(declare -f _ir_lock_owned_by | sed '1s/_ir_lock_owned_by/_ir_orig_owned_by/')"
    _ir_lock_owned_by() {
        _ir_orig_owned_by "$@" || return 1
        _park owned
        return 0
    }
}

pre_call() {  # pre_call <function> <args...> — runs it from the pre-owner helper
    "$BASH" -c 'source "$1" || exit 99; set +e; shift; "$@"' _ "$PRE_OWNER" "$@"
}
have_pre_owner() {
    [[ -f "$PRE_OWNER" ]] && return 0
    bad "$1: pre-owner fixture missing at $PRE_OWNER"
    return 1
}

echo "== two reclaimers"
reset_gpu
_ir_try_lock "$LOCK" "$DEAD1" ""
HA=$(live); HB=$(live)
newbar alive
( park_after_classify; _ir_try_lock "$LOCK" "$HB" "" ) & bp=$!
wait_parked alive
_ir_try_lock "$LOCK" "$HA" ""; a=$?
before=$(lock_bytes)
open_bar alive "$bp"; b=$?
after=$(lock_bytes)
assert_eq "$(winners "$a" "$b")" "1" "two reclaimers: exactly one acquires"
assert_eq "$b" "1" "two reclaimers: the parked loser returns 1"
assert_eq "$(holder)" "$HA" "two reclaimers: the winner holds the lock"
assert_eq "$after" "$before" "two reclaimers: the loser leaves the winner's owner and pid bytes unchanged"

echo "== unstamped creator"
reset_gpu
HC=$(live); HD=$(live)
newbar mkdir
(
    mkdir() {
        command mkdir "$@"
        local r=$?
        (( r == 0 )) && [[ "${!#}" == "$LOCK" ]] && _park mkdir
        return "$r"
    }
    _ir_try_lock "$LOCK" "$HC" ""
) & cp=$!
wait_parked mkdir
_ir_try_lock "$LOCK" "$HD" ""; d=$?
open_bar mkdir "$cp"; c=$?
assert_eq "$(winners "$c" "$d")" "1" "unstamped creator: exactly one acquires"
assert_eq "$d" "0" "unstamped creator: the live contender acquires"
assert_eq "$c" "1" "unstamped creator: the released creator returns 1"
assert_eq "$(holder)" "$HD" "unstamped creator: the contender holds the lock"

echo "== dead reaper holds the marker"
reset_gpu
_ir_try_lock "$LOCK" "$DEAD1" ""
# The reaper's record names a dead pid, and its process exits inside the mv
# that would replace `owner`: it dies holding the marker.
(
    mv() { [[ "${!#}" == "$LOCK/owner" ]] && exit 0; command mv "$@"; }
    _ir_try_lock "$LOCK" "$DEAD2" ""
)
markers=$(ls -A "$LOCK" 2>/dev/null | grep -c '^reap\.')
assert_eq "$markers" "1" "dead reaper: its marker is left in the lock"
assert_eq "$(holder)" "$DEAD1" "dead reaper: the lock still names the first dead holder"
HE=$(live)
_ir_try_lock "$LOCK" "$HE" ""; e=$?
assert_eq "$e" "0" "dead reaper: a third contender acquires"
assert_eq "$(holder)" "$HE" "dead reaper: the third contender holds the lock"

echo "== dead reaper already transferred"
reset_gpu
_ir_try_lock "$LOCK" "$DEAD1" ""
HF=$(live)
newbar alive
( park_after_classify; _ir_try_lock "$LOCK" "$HF" "" ) & fp=$!
wait_parked alive
_ir_try_lock "$LOCK" "$DEAD2" ""; m=$?
assert_eq "$m:$(holder)" "0:$DEAD2" "already transferred: a reclaim for a dead pid takes the lock"
open_bar alive "$fp"; f=$?
assert_eq "$f" "0" "already transferred: the contender that read the first record acquires in that call"
assert_eq "$(holder)" "$HF" "already transferred: the contender holds the lock"

echo "== sweep vs reclaim"
reset_gpu
_ir_try_lock "$LOCK" "$DEAD1" ""
mkdir -p "$IR_LOCK_ROOT/.held/$DEAD1"
: > "$IR_LOCK_ROOT/.held/$DEAD1/gpu__lock"
HG=$(live)
newbar owned
( park_after_owned_check; ir_sweep_stale ) & sp=$!
wait_parked owned
_ir_try_lock "$LOCK" "$HG" ""; g=$?
open_bar owned "$sp"
assert_eq "$g" "0" "sweep vs reclaim: the reclaimer acquires"
assert_eq "$([[ -d "$LOCK" ]] && echo present || echo removed)" "present" "sweep vs reclaim: the released sweep removes nothing"
assert_eq "$(holder)" "$HG" "sweep vs reclaim: the reclaimer stays the holder"

echo "== pre-owner helper vs a live lock"
if have_pre_owner "pre-owner vs live"; then
    reset_gpu
    HH=$(live)
    _ir_try_lock "$LOCK" "$HH" ""
    before=$(lock_listing)
    pre_call _ir_try_lock "$LOCK"; rc=$?
    assert_eq "$rc" "1" "pre-owner vs live: its _ir_try_lock returns 1"
    assert_eq "$(lock_listing)" "$before" "pre-owner vs live: the lock's listing and pid are unchanged"
    IR_ACQUIRE_HOLDER_PID="$HH" IR_ACQUIRE_HELD_VERB=gpu pre_call ir_inherited_lock_covers gpu; rc=$?
    assert_eq "$rc" "0" "pre-owner vs live: its ir_inherited_lock_covers covers the holder"
    pre_call _ir_lock_owned_by "$LOCK" 1 ""; rc=$?
    assert_eq "$rc" "1" "pre-owner vs live: its _ir_lock_owned_by for pid 1 is false"
fi

echo "== live pre-owner lock"
if have_pre_owner "live pre-owner lock"; then
    reset_gpu
    HL=$(live)
    pre_call _ir_try_lock "$LOCK" "$HL" ""
    before=$(lock_listing)
    _ir_try_lock "$LOCK"; rc=$?
    assert_eq "$rc" "1" "live pre-owner lock: _ir_try_lock returns 1"
    assert_eq "$(lock_listing)" "$before" "live pre-owner lock: the lock is unchanged"
    IR_ACQUIRE_HOLDER_PID="$HL" IR_ACQUIRE_HELD_VERB=gpu ir_inherited_lock_covers gpu; rc=$?
    assert_eq "$rc" "0" "live pre-owner lock: ir_inherited_lock_covers covers its holder"
fi

echo "== two reclaimers vs a dead pre-owner lock"
if have_pre_owner "dead pre-owner lock"; then
    reset_gpu
    pre_call _ir_try_lock "$LOCK" "$DEAD1" ""
    HA=$(live); HB=$(live)
    newbar alive
    ( park_after_classify; _ir_try_lock "$LOCK" "$HB" "" ) & bp=$!
    wait_parked alive
    _ir_try_lock "$LOCK" "$HA" ""; a=$?
    open_bar alive "$bp"; b=$?
    assert_eq "$(winners "$a" "$b")" "1" "dead pre-owner lock: exactly one adopts it"
    assert_eq "$(holder)" "$HA" "dead pre-owner lock: the adopter holds it"
fi

echo "== two Cygwin runtimes"
# The other runtime's holder can carry the ancestor's pid number; only the
# owner token tells them apart.
reset_gpu
HX=$(live)
_ir_try_lock "$LOCK" "$HX" ""
IR_ACQUIRE_HOLDER_PID="$HX" IR_ACQUIRE_HELD_VERB=gpu ir_inherited_lock_covers gpu; rc=$?
assert_eq "$rc" "0" "runtimes: same pid and token covers"
IR_ACQUIRE_HOLDER_PID="$HX" IR_ACQUIRE_HELD_VERB=gpu IR_ACQUIRE_HOLDER_WINPID="4242 C:/other-runtime" \
    ir_inherited_lock_covers gpu; rc=$?
assert_eq "$rc" "1" "runtimes: same pid, env token is not the lock's"
reset_gpu
_ir_try_lock "$LOCK" "$HX" "4242 C:/other-runtime"
IR_ACQUIRE_HOLDER_PID="$HX" IR_ACQUIRE_HELD_VERB=gpu ir_inherited_lock_covers gpu; rc=$?
assert_eq "$rc" "1" "runtimes: same pid, another runtime's token on the lock"

# One shell plays both runtimes: same pid, a different token each. Odd numbers
# are never real Windows pids, so neither reads as live from its token.
out=$(
    as_runtime() { _IR_SELF_WINPID="$1"; _IR_RUNTIME_ROOT="$2"; }
    ir_self_owner_token() { echo "$_IR_SELF_WINPID $_IR_RUNTIME_ROOT"; }
    gpu="$IR_LOCK_ROOT/gpu/lock"; perf="$IR_LOCK_ROOT/perf/lock"
    rm -rf "$IR_LOCK_ROOT/gpu" "$IR_LOCK_ROOT/perf"; mkdir -p "$IR_LOCK_ROOT/gpu" "$IR_LOCK_ROOT/perf"
    as_runtime 99999991 C:/rt-a; _ir_try_lock "$gpu";  _ir_record_held "$gpu"
    as_runtime 99999993 C:/rt-b; _ir_try_lock "$perf"; _ir_record_held "$perf"
    _ir_release_one "$gpu"; [[ -d "$gpu" ]] && printf "kept " || printf "stolen "
    ir_release_all
    [[ -d "$gpu" ]] && printf "kept " || printf "stolen "
    [[ -d "$perf" ]] && printf "held " || printf "released "
    as_runtime 99999991 C:/rt-a; ir_release_all
    [[ -d "$gpu" ]] && printf "held" || printf "released"
)
assert_eq "$out" "kept kept released released" "runtimes: rt-b's exit leaves rt-a's lock; each releases its own"

reset_gpu
ledger="$IR_LOCK_ROOT/.held/$DEAD1.99999991"
mkdir -p "$ledger"; echo "99999991 C:/rt-a" > "$ledger/.winpid"; : > "$ledger/gpu__lock"
_ir_try_lock "$LOCK" "$DEAD1" "99999993 C:/rt-b"
ir_sweep_stale
out="$([[ -d "$LOCK" ]] && printf "kept " || printf "stolen ")$([[ -d "$ledger" ]] && printf "ledger-left " || printf "ledger-swept ")"
rm -rf "$LOCK"
mkdir -p "$ledger"; echo "99999991 C:/rt-a" > "$ledger/.winpid"; : > "$ledger/gpu__lock"
_ir_try_lock "$LOCK" "$DEAD1" "99999991 C:/rt-a"
ir_sweep_stale
out+="$([[ -d "$LOCK" ]] && printf "kept" || printf "swept")"
assert_eq "$out" "kept ledger-swept swept" "runtimes: the sweep spares a same-pid lock of another runtime"

if [[ -r /proc/$$/winpid ]]; then
    # A pid this runtime cannot see, stamped with a live Windows pid (ours).
    reset_gpu
    _ir_try_lock "$LOCK" "$DEAD1" "$_IR_SELF_WINPID C:/other-runtime"
    _ir_try_lock "$LOCK"; held=$?
    rm -rf "$LOCK"
    _ir_try_lock "$LOCK" "$DEAD1" "$_IR_SELF_WINPID $_IR_RUNTIME_ROOT"
    _ir_try_lock "$LOCK"; taken=$?
    assert_eq "$held:$taken" "1:0" "runtimes: a foreign-runtime live holder is kept; a same-runtime dead one is taken"
else
    echo "  note: no /proc/\$\$/winpid (not a Cygwin runtime); skipping the Windows-pid liveness case"
fi

echo "== bash 3.2 release"
if (( BASH_VERSINFO[0] == 3 )); then
    if [[ -x "$ACQUIRE" ]]; then
        rel_root="$ROOT/release32"
        IR_LOCK_ROOT="$rel_root" "$BASH" "$ACQUIRE" gpu -- /usr/bin/true >/dev/null 2>&1
        assert_eq "$([[ -d "$rel_root/gpu/lock" ]] && echo left || echo released)" "released" \
            "bash 3.2: ir-acquire gpu -- true leaves no gpu lock behind"
    else
        bad "bash 3.2: ir-acquire not found at $ACQUIRE"
    fi
else
    echo "  note: runs in the /bin/bash 3.x pass"
fi

echo "== stress"
cat > "$ROOT/contend.sh" <<'EOF'
# contend.sh <helpers> <lock-root> <rounds> <log>
source "$1"
set +e
lock="$2/gpu/lock"
for (( n = 0; n < $3; n++ )); do
    if _ir_try_lock "$lock"; then
        mkdir "$2/critical" 2>/dev/null || echo double >> "$4"
        echo win >> "$4"
        rmdir "$2/critical" 2>/dev/null
        _ir_release_one "$lock"
    fi
done
EOF
cat > "$ROOT/reclaim.sh" <<'EOF'
# reclaim.sh <helpers> <lock-root> <result-file> <other-result-file>
source "$1"
set +e
lock="$2/gpu/lock"
if _ir_try_lock "$lock"; then
    echo taken > "$3"
    for (( i = 0; i < 200; i++ )); do [[ -e "$4" ]] && break; sleep 0.05; done
    _ir_release_one "$lock"
else
    echo refused > "$3"
fi
EOF
stress_root="$ROOT/stress"
mkdir -p "$stress_root/gpu"
: > "$stress_root/log"
for _ in 1 2 3 4 5 6 7 8; do
    IR_LOCK_ROOT="$stress_root" "$BASH" "$ROOT/contend.sh" "$HELPERS" "$stress_root" 100 "$stress_root/log" &
done
wait
wins=$(grep -c '^win$' "$stress_root/log")
doubles=$(grep -c '^double$' "$stress_root/log")
echo "  8 contenders x 100 rounds: $wins wins, $doubles doubles"
assert_eq "$doubles" "0" "stress: no two contenders inside the lock at once"
assert_eq "$([[ "$wins" -gt 0 ]] && echo yes || echo no)" "yes" "stress: contenders do acquire"

good_rounds=0
for (( r = 0; r < 20; r++ )); do
    rm -f "$stress_root/acquired" "$stress_root/res."*
    IR_LOCK_ROOT="$stress_root" "$BASH" -c 'source "$1"; set +e; _ir_try_lock "$2" && { : > "$3"; exec sleep 30; }' \
        _ "$HELPERS" "$stress_root/gpu/lock" "$stress_root/acquired" &
    victim=$!
    for (( i = 0; i < 200; i++ )); do [[ -e "$stress_root/acquired" ]] && break; sleep 0.05; done
    kill -KILL "$victim" 2>/dev/null
    wait "$victim" 2>/dev/null
    IR_LOCK_ROOT="$stress_root" "$BASH" "$ROOT/reclaim.sh" "$HELPERS" "$stress_root" "$stress_root/res.1" "$stress_root/res.2" &
    IR_LOCK_ROOT="$stress_root" "$BASH" "$ROOT/reclaim.sh" "$HELPERS" "$stress_root" "$stress_root/res.2" "$stress_root/res.1" &
    wait
    [[ "$(cat "$stress_root/res.1" "$stress_root/res.2" 2>/dev/null | grep -c '^taken$')" == 1 ]] \
        && good_rounds=$(( good_rounds + 1 ))
done
assert_eq "$good_rounds" "20" "stress: each of 20 SIGKILLed holders is reclaimed exactly once"
assert_eq "$(ls -A "$stress_root/gpu")" "" "stress: no lock, record or released dir is left in gpu/"

if (( BASH_VERSINFO[0] > 3 )) && [[ -z "${IR_LOCK_TRANSFER_INNER:-}" ]]; then
    sys_major=""
    [[ -x /bin/bash ]] && sys_major=$(/bin/bash -c 'echo "${BASH_VERSINFO[0]}"' 2>/dev/null || true)
    if [[ "$sys_major" == "3" ]]; then
        echo "== every row again under /bin/bash 3.x"
        inner_rc=0
        IR_LOCK_TRANSFER_INNER=1 /bin/bash "$0" 2>&1 | sed 's/^/    /' || inner_rc=$?
        assert_eq "$inner_rc" "0" "the /bin/bash 3.x pass is green"
    else
        echo "  note: /bin/bash is not bash 3.x (major='${sys_major:-none}'); skipping the 3.x pass"
    fi
fi

summarize "ir lock transfer tests"
