#!/usr/bin/env bash
# engine/tools/lib/concurrency_helpers.sh — shared helpers for ir-* tools.
#
# Sourced (not exec'd) by ir-host-probe, ir-acquire, and any future ir-* tool
# that needs the engine-level coordination primitives. The functions defined
# here are the only thing those tools should depend on from this library —
# everything else (paths, defaults) is computed from the three-config-layer
# resolver below.
#
# The lock primitives use mkdir plus hard links, which work identically on
# Linux, macOS, WSL and MSYS2 — no flock dependency. Each lock holds an
# `owner` record so `ir-acquire --info` can attribute waits to the holding
# process.

set -euo pipefail

# Re-entrancy guard — these tools chain (`ir-acquire benchmark -- ir-run ...`)
# and would otherwise re-source the helpers on each hop.
if [[ -n "${_IR_HELPERS_LOADED:-}" ]]; then
    return 0
fi
_IR_HELPERS_LOADED=1

# ---------------------------------------------------------------------------
# Path resolution
# ---------------------------------------------------------------------------

# ir_enclosing_engine_root <dir> — nearest ancestor (including <dir>) that
# looks like an engine checkout (CMakePresets.json + engine/). Returns 1
# when no ancestor matches.
#
# The `cd ... && pwd` normalization is load-bearing, not incidental: it puts
# <dir> in POSIX-drive form (/c/...) on MSYS2, which is what lets the "/"
# sentinel terminate the walk. Drop it and this loop inherits the drive-root
# fixed point documented on ir_creation_worktree_engine_root below.
ir_enclosing_engine_root() {
    local here="$1"
    [[ -d "$here" ]] || return 1
    here="$(cd "$here" && pwd)"
    while [[ "$here" != "/" ]]; do
        if [[ -f "$here/CMakePresets.json" && -d "$here/engine" ]]; then
            echo "$here"
            return 0
        fi
        here="$(dirname "$here")"
    done
    return 1
}

# Engine root: walk up from this file (lib/ is at $engine/engine/tools/lib/).
# Same approach as scripts/fleet/fleet-common.sh.
IR_ENGINE_ROOT="$(ir_enclosing_engine_root "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)")" || {
    echo "ir-tools: cannot resolve engine root from ${BASH_SOURCE[0]}" >&2
    return 1
}
IR_TOOLS_DIR="$IR_ENGINE_ROOT/engine/tools"
IR_DEFAULTS_TOML="$IR_TOOLS_DIR/concurrency.toml"
IR_HOST_TOML="${IR_HOST_TOML:-$HOME/.config/irreden/host.toml}"
IR_QUIET_HELPER="$IR_TOOLS_DIR/lib/quiet_window.py"

# ir_worktree_root — the *invoker's* worktree, distinct from IR_ENGINE_ROOT
# (which resolves to the script's own checkout via symlink walk-up). The
# distinction matters because ir-build / ir-run are typically symlinked from
# ~/bin/ into the main clone, but each fleet worktree builds into its own
# <worktree>/build/. We want the build dir to follow the cwd, not the script
# source.
ir_worktree_root() {
    local root
    root="$(git rev-parse --show-toplevel 2>/dev/null || echo "$IR_ENGINE_ROOT")"
    # In a nested worktree (.claude/worktrees/<name>), the worktree's own
    # CMakePresets.json sits at the root. Walk up only when it's missing
    # (older checkouts, ad-hoc layouts).
    if [[ ! -f "$root/CMakePresets.json" ]]; then
        if [[ -f "$root/../../CMakePresets.json" ]]; then
            root="$(cd "$root/../.." && pwd)"
        elif [[ -f "$root/../CMakePresets.json" ]]; then
            root="$(cd "$root/.." && pwd)"
        fi
    fi
    echo "$root"
}

# ir_creation_worktree_engine_root <worktree-root> — when <worktree-root>
# is a downstream-creation checkout nested under an engine root at
# creations/<name>/... (the fleet layout puts agent worktrees at
# creations/<name>/.claude/worktrees/<agent>/), echo the enclosing engine
# root and return 0. Returns 1 for engine checkouts (they carry their own
# CMakePresets.json) and for repos outside an engine tree.
#
# Note ir_worktree_root resolves a creation's MAIN checkout
# (creations/<name>/, two levels below the engine root) to the engine
# root via its ../../ walk-up, so only nested *worktrees* reach this
# detection — the main checkout keeps building through <engine>/build.
#
# Walks ancestors with `dirname` rather than delegating to
# ir_enclosing_engine_root, because that helper's `cd ... && pwd` silently
# normalizes to POSIX-drive form (/c/...) on MSYS2/Git-Bash, while
# <worktree-root> — sourced from `git rev-parse --show-toplevel` in every
# real caller — is spelled in Windows-drive form (C:/...). Comparing the
# two spellings in the `case` below never matches. `dirname` is a
# pure string operation, so walking directly on <worktree-root> keeps the
# returned engine root in the SAME spelling as the input; the `-f`/`-d`
# file tests resolve either spelling transparently. Matching spelling
# matters downstream too: ir_default_build_dir prefix-strips <worktree-root>
# with this return value, and ir-build feeds it straight into `cmake -S`
# under cmd.exe, which cannot resolve a POSIX-style path.
#
# The walk terminates on a `dirname` fixed point, NOT on "." / "/" sentinels:
# MSYS2/Git-Bash `dirname` is idempotent at a bare Windows drive root
# ("C:" -> "C:", "C:/" -> "C:/"), so a Windows-drive-form <worktree-root> with
# no engine-root ancestor never reaches either sentinel. Regression: T5 in
# scripts/fleet/tests/test_ir_build_dir_resolution.sh.
ir_creation_worktree_engine_root() {
    local root="$1"
    [[ -f "$root/CMakePresets.json" ]] && return 1
    local candidate="$root" prev=""
    while [[ "$candidate" != "$prev" ]]; do
        prev="$candidate"
        candidate="$(dirname "$candidate")"
        if [[ -f "$candidate/CMakePresets.json" && -d "$candidate/engine" ]]; then
            case "$root" in
                "$candidate"/creations/*) echo "$candidate"; return 0 ;;
                *) return 1 ;;
            esac
        fi
    done
    return 1
}

# ir_default_build_dir <worktree-root> [debug|release] — the build tree
# ir-build/ir-run use when IRREDEN_BUILD_DIR is not set. Release builds stay
# separate from debug builds so their CMake caches never cross-contaminate.
ir_default_build_dir() {
    local root="$1"
    local mode="${2:-debug}"
    local suffix=""
    case "$mode" in
        debug) ;;
        release) suffix="-release" ;;
        *)
            echo "ir-tools: unsupported build mode '$mode'" >&2
            return 2
            ;;
    esac
    local eng
    if eng="$(ir_creation_worktree_engine_root "$root")"; then
        local rel="${root#"$eng"/creations/}"
        local creation="${rel%%/*}"
        echo "$eng/build-$creation-$(basename "$root")$suffix"
    else
        if [[ "$mode" == "release" ]]; then
            echo "$root/build-release"
        else
            echo "$root/build"
        fi
    fi
}

# ir_build_preset [debug|release] [platform] — resolve the host CMake preset.
# The optional platform makes resolver tests hermetic; production uses uname.
ir_build_preset() {
    local mode="${1:-debug}"
    local platform="${2:-$(uname -s)}"
    case "$mode" in
        debug|release) ;;
        *)
            echo "ir-tools: unsupported build mode '$mode'" >&2
            return 2
            ;;
    esac
    case "$platform" in
        Darwin)               echo "macos-$mode" ;;
        Linux)                echo "linux-$mode" ;;
        MINGW*|MSYS*|CYGWIN*) echo "windows-$mode" ;;
        *)
            echo "ir-tools: unsupported platform $platform" >&2
            return 1
            ;;
    esac
}

# Lock dir lives in a runtime-temp location so it survives across shells but
# clears on reboot. XDG_RUNTIME_DIR is Linux-only; macOS doesn't set it.
# A pre-set IR_LOCK_ROOT wins (used by tests to isolate from host locks).
if [[ -z "${IR_LOCK_ROOT:-}" ]]; then
    if [[ -n "${XDG_RUNTIME_DIR:-}" ]]; then
        IR_LOCK_ROOT="${XDG_RUNTIME_DIR}/irreden/locks"
    else
        IR_LOCK_ROOT="/tmp/irreden-${USER:-$(id -un)}/locks"
    fi
fi
IR_CACHE_ROOT="${XDG_CACHE_HOME:-$HOME/.cache}/irreden"
export IR_LOCK_ROOT IR_CACHE_ROOT

mkdir -p "$IR_LOCK_ROOT/cpu" "$IR_LOCK_ROOT/gpu" "$IR_LOCK_ROOT/perf" \
    "$IR_LOCK_ROOT/quiet/records" "$IR_LOCK_ROOT/quiet/leases" "$IR_CACHE_ROOT"

# ---------------------------------------------------------------------------
# Tiny TOML reader (handles only the subset this repo's tomls use:
# `[section]` headers, `key = value` lines, # comments, quoted-string and
# bare-token values). Adequate for concurrency.toml and host.toml.
#
# Usage: _ir_read_toml <file> <section> <key>
# ---------------------------------------------------------------------------

_ir_read_toml() {
    local file="$1" section="$2" key="$3"
    [[ -f "$file" ]] || return 1
    awk -v want_sec="$section" -v want_key="$key" '
        /^[[:space:]]*#/ { next }
        /^[[:space:]]*\[/ {
            gsub(/[][[:space:]]/, "", $0)
            cur_sec = $0
            next
        }
        /=/ {
            if (cur_sec != want_sec) next
            split($0, parts, "=")
            k = parts[1]
            gsub(/[[:space:]]/, "", k)
            if (k != want_key) next
            v = substr($0, index($0, "=") + 1)
            sub(/#.*$/, "", v)            # strip trailing comment
            gsub(/^[[:space:]]+|[[:space:]]+$/, "", v)
            gsub(/^"|"$/, "", v)
            print v
            exit
        }
    ' "$file"
}

# Three-layer config: env var → host toml → defaults toml.
# Usage: _ir_config <section> <key> <env-var-name>
_ir_config() {
    local section="$1" key="$2" env_name="$3"
    local env_val
    if [[ -n "${env_name:-}" ]]; then
        env_val="${!env_name:-}"
        if [[ -n "${env_val:-}" ]]; then
            echo "$env_val"
            return 0
        fi
    fi
    local host_val
    host_val="$(_ir_read_toml "$IR_HOST_TOML" "$section" "$key" 2>/dev/null || true)"
    if [[ -n "${host_val:-}" ]]; then
        echo "$host_val"
        return 0
    fi
    _ir_read_toml "$IR_DEFAULTS_TOML" "$section" "$key"
}

# ---------------------------------------------------------------------------
# Budget resolvers
# ---------------------------------------------------------------------------

ir_cpu_count() {
    if command -v nproc >/dev/null 2>&1; then
        nproc
    elif command -v sysctl >/dev/null 2>&1; then
        sysctl -n hw.ncpu
    else
        echo 4
    fi
}

ir_cpu_budget() {
    local v
    v="$(_ir_config cpu budget IR_CPU_BUDGET)"
    if [[ "$v" == "auto" ]]; then
        ir_cpu_count
    else
        echo "$v"
    fi
}

ir_workers() {
    local v
    v="$(_ir_config concurrency workers IR_FLEET_WORKERS)"
    if [[ "$v" == "auto" || -z "$v" ]]; then
        echo 1
    else
        echo "$v"
    fi
}

ir_per_build_max() {
    local v
    v="$(_ir_config cpu per_build_max IR_BUILD_JOBS)"
    if [[ "$v" == "auto" || -z "$v" ]]; then
        local budget workers
        budget="$(ir_cpu_budget)"
        workers="$(ir_workers)"
        # floor (integer) division; minimum 1 core per build
        local cap=$(( budget / workers ))
        (( cap < 1 )) && cap=1
        echo "$cap"
    else
        echo "$v"
    fi
}

ir_gpu_exclusive() {
    local v
    v="$(_ir_config gpu exclusive IR_GPU_EXCLUSIVE)"
    if [[ "$v" == "auto" || -z "$v" ]]; then
        case "$(uname -s)" in
            Darwin) echo true ;;
            *)      echo false ;;
        esac
    else
        echo "$v"
    fi
}

ir_perf_exclusive() {
    # Always true; the toml key exists for documentation but isn't a knob.
    echo true
}

ir_queue_timeout() {
    local v
    v="$(_ir_config concurrency queue_timeout_seconds IR_QUEUE_TIMEOUT)"
    [[ -z "$v" ]] && v=600
    echo "$v"
}

ir_quiet_linger() {
    _ir_config quiet linger_seconds IR_QUIET_LINGER
}

ir_quiet_max() {
    _ir_config quiet max_seconds IR_QUIET_MAX
}

ir_quiet_drain() {
    _ir_config quiet drain_seconds IR_QUIET_DRAIN
}

ir_quiet_settle_cpu() {
    _ir_config quiet settle_cpu IR_QUIET_SETTLE_CPU
}

ir_quiet_settle_sample() {
    _ir_config quiet settle_sample_seconds IR_QUIET_SETTLE_SAMPLE
}

ir_quiet_status() {
    python3 "$IR_QUIET_HELPER" status "$@"
}

ir_quiet_lease_create() {
    local pid="${2:-$$}"
    python3 "$IR_QUIET_HELPER" lease-create "$1" --pid "$pid" \
        --owner-token "$(ir_owner_token_for_pid "$pid")"
}

ir_quiet_lease_drop() {
    python3 "$IR_QUIET_HELPER" lease-drop "$1"
}

# The parker reads its own runtime's process table on Windows, so it is told
# which runtime launched it; empty off Windows.
ir_quiet_park() {
    local tag="$1"
    shift
    local runtime=()
    [[ -z "$_IR_RUNTIME_ROOT" ]] || runtime=(--runtime-root "$_IR_RUNTIME_ROOT")
    python3 "$IR_QUIET_HELPER" park "$tag" ${runtime[@]+"${runtime[@]}"} "$@"
}

_ir_quiet_defer_nonbenchmark() {
    [[ -n "${IR_QUIET_OWNER:-}" ]] || return 0
    [[ "${IR_QUIET_ACQUIRE_VERB:-}" != "benchmark" ]] || return 0
    if ir_quiet_status --owner "$IR_QUIET_OWNER" >/dev/null 2>&1; then
        ir_quiet_park "$IR_QUIET_OWNER"
        # Quiet-window deferral is bounded by the window cap, not by the
        # queued acquisition's own timeout.
        started="$(date +%s)"
    fi
}

# ---------------------------------------------------------------------------
# Lock primitives — exclusive owner record, dead-holder transfer
# ---------------------------------------------------------------------------
#
# A "lock" is a directory under $IR_LOCK_ROOT/{cpu,gpu,perf}/. The directory
# is only a container: holding the lock means owning its `owner` file, a
# three-line record (holder pid, owner token, nonce "<epoch>.<writer>.<hex>").
# A record is written complete as a dotfile in the lock's parent and then
# hard-linked into place, so `ln` is the exclusive step and no reader sees a
# partial record.
#
# - Free lock: the first `ln <record> <lock>/owner` wins.
# - Dead owner R: a contender links `reap.<nonce(R)>`, then replaces `owner`
#   with `mv -f`, but only while `owner` is still a record on the chain of
#   dead records it walked. A failed marker link names the marker's holder M:
#   live means refuse, dead means walk on to `reap.<nonce(M)>`. Every owner
#   after the first is a node of that one chain, so its first unheld marker
#   can be linked exactly once. Markers stay until the lock is released.
# - Release renames the lock dir aside, then deletes the copy. Nothing removes
#   a lock dir in place, so no remover can land after a contender's take.
#
# Helper copies older than the record still share the lock root (a worktree
# branch, a shell that sourced an older file). `pid`, `winpid` and
# `acquired_at` stay as single-line mirrors the owner writes once it holds
# `owner`, so those copies read a live lock as held. A dir with `pid` but no
# `owner` is such a copy's lock: judged from those files, adopted through
# `ln owner` when its holder is dead, never removed. New code never reads a
# mirror in a dir that has `owner`.
#
# Held resources are tracked in a per-process list under $IR_LOCK_ROOT/.held/
# (see _ir_held_dir) — the trap in ir-acquire walks it on exit.

_ir_pid_alive() {
    local pid="$1"
    [[ -n "$pid" ]] || return 1
    kill -0 "$pid" 2>/dev/null
}

# On native Windows two Cygwin runtimes (MSYS2's and Git for Windows') reach
# the same lock root, because both map /tmp through TEMP, but each has its own
# pid table: `kill -0` from one reports every holder in the other dead, and
# the stale-reclaim below would take a live holder's locks. A holder therefore
# records "<windows-pid> <cygwin-root>" beside its pid. Empty off Windows.
_IR_SELF_WINPID=""
_IR_RUNTIME_ROOT=""
if [[ -r /proc/$$/winpid ]]; then
    read -r _IR_SELF_WINPID < /proc/$$/winpid || true
    _IR_RUNTIME_ROOT="$(cygpath -m / 2>/dev/null || true)"
fi

_ir_write_winpid() {
    [[ -n "$_IR_SELF_WINPID" ]] || return 0
    ir_self_owner_token > "$1"
}

# ir_self_owner_token — the winpid record this process stamps on its locks;
# empty off Windows, where a lock carries none.
ir_self_owner_token() {
    ir_owner_token_for_pid "$$"
}

ir_owner_token_for_pid() {
    local pid="$1" winpid=""
    [[ -r "/proc/$pid/winpid" ]] || return 0
    read -r winpid < "/proc/$pid/winpid" || true
    [[ -n "$winpid" ]] || return 0
    echo "$winpid $_IR_RUNTIME_ROOT"
}

# _ir_holder_alive <pid> <owner-token> — same-runtime holders are judged by
# `kill -0` alone; a holder from the other runtime by its Windows pid.
_ir_holder_alive() {
    local pid="$1" token="$2" winpid="" root=""
    _ir_pid_alive "$pid" && return 0
    read -r winpid root <<<"$token" || true
    [[ -n "$winpid" && "$root" != "$_IR_RUNTIME_ROOT" ]] || return 1
    ps -W 2>/dev/null | awk -v w="$winpid" '$4 == w { found = 1 } END { exit !found }'
}

# The lock paths read their files with `read` and build names with
# `printf -v`, not command substitution: on native Windows every fork costs
# milliseconds, and a cpu-slot wait polls every slot five times a second.
_IR_BASH_VERSION=$(( BASH_VERSINFO[0] * 100 + BASH_VERSINFO[1] ))

# _ir_lock_rec_read <file> — reads a record into _IR_REC_PID, _IR_REC_TOKEN
# and _IR_REC_NONCE; fails when the file is missing or the pid or nonce is
# empty. A record file is only ever replaced whole, so one open sees one
# record.
_ir_lock_rec_read() {
    _IR_REC_PID="" _IR_REC_TOKEN="" _IR_REC_NONCE=""
    { IFS= read -r _IR_REC_PID; IFS= read -r _IR_REC_TOKEN; IFS= read -r _IR_REC_NONCE; } 2>/dev/null < "$1" || true
    [[ -n "$_IR_REC_PID" && -n "$_IR_REC_NONCE" ]]
}

# _ir_read_line <file> — sets _IR_LINE to the file's first line, empty when
# the file is missing.
_ir_read_line() {
    _IR_LINE=""
    { IFS= read -r _IR_LINE; } 2>/dev/null < "$1" || true
}

# _ir_lock_rand — sets _IR_RAND to random hex digits. A bash 3.2 subshell's
# $$ is its parent's, so this is what keeps names written by two subshells
# apart.
_ir_lock_rand() {
    if (( _IR_BASH_VERSION >= 501 )); then
        printf -v _IR_RAND '%08x%08x' "$SRANDOM" "$SRANDOM"
    else
        _IR_RAND="$(od -An -N6 -tx1 /dev/urandom)"
        _IR_RAND="${_IR_RAND//[!0-9a-f]/}"
    fi
}

# _ir_lock_rec_ensure <lockdir> [<pid> [<owner-token>]] — writes this
# attempt's record on first use and leaves its path in `_ir_rec`, which the
# caller declares local and removes. Writing it lazily keeps the poll against
# a live holder cheap. The record sits beside the lock dir because a hard
# link cannot cross filesystems.
_ir_lock_rec_ensure() {
    [[ -z "$_ir_rec" ]] || return 0
    local lockdir="$1" pid="${2:-$$}" token now rec
    if (( $# >= 3 )); then token="$3"; else token="$(ir_self_owner_token)"; fi
    if (( _IR_BASH_VERSION >= 402 )); then
        printf -v now '%(%s)T' -1
    else
        now="$(date +%s)"
    fi
    _ir_lock_rand
    rec="${lockdir%/*}/.rec.$now.$$.$_IR_RAND"
    if ! printf '%s\n%s\n%s\n' "$pid" "$token" "$now.$$.$_IR_RAND" > "$rec" 2>/dev/null; then
        rm -f "$rec"
        return 1
    fi
    _ir_rec="$rec"
}

# _ir_retry_mv <src> <dst> — native Windows refuses a rename while another
# process has a handle open on the file or inside the dir. That clears within
# moments, so retry for about 5 s; fail at once when <src> is gone.
_ir_retry_mv() {
    local i
    for (( i = 0; i < 50; i++ )); do
        mv -f "$1" "$2" 2>/dev/null && return 0
        [[ -e "$1" ]] || return 1
        sleep 0.1
    done
    return 1
}

# _ir_lock_mirror <lockdir> <record-file> — writes the single-line files that
# older helper copies read. winpid lands before pid, because an older reader
# in the other Cygwin runtime judges a pid it cannot see by that file. A
# mirror that never lands is logged, not fatal: new code never reads one
# beside `owner`.
_ir_lock_mirror() {
    local lockdir="$1"
    _ir_lock_rec_read "$2" || return 0
    if [[ -n "$_IR_REC_TOKEN" ]]; then
        _ir_lock_mirror_one "$lockdir" winpid "$_IR_REC_TOKEN" "$_IR_REC_NONCE"
    elif [[ -e "$lockdir/winpid" ]]; then
        rm -f "$lockdir/winpid"
    fi
    _ir_lock_mirror_one "$lockdir" acquired_at "${_IR_REC_NONCE%%.*}" "$_IR_REC_NONCE"
    _ir_lock_mirror_one "$lockdir" pid "$_IR_REC_PID" "$_IR_REC_NONCE"
}

_ir_lock_mirror_one() {  # <lockdir> <name> <value> <nonce>
    local tmp="$1/.$2.$4"
    if ! { echo "$3" > "$tmp" && _ir_retry_mv "$tmp" "$1/$2"; } 2>/dev/null; then
        rm -f "$tmp" 2>/dev/null
        echo "ir-acquire: could not write the $2 mirror on $1" >&2
    fi
    return 0
}

_ir_lock_holder() {
    local lockdir="$1"
    if _ir_lock_rec_read "$lockdir/owner"; then
        echo "$_IR_REC_PID"
        return 0
    fi
    [[ ! -e "$lockdir/owner" && -f "$lockdir/pid" ]] || return 1
    cat "$lockdir/pid" 2>/dev/null
}

# _ir_lock_take <lockdir> [<pid> [<owner-token>]] — transfers a lock whose
# `owner` was just read into _IR_REC_* along the takeover-marker chain.
# Returns as _ir_lock_contend does.
_ir_lock_take() {
    local lockdir="$1" chain=" " marker="" swap
    while :; do
        [[ "$chain" != *" $_IR_REC_NONCE "* ]] || return 1
        chain+="$_IR_REC_NONCE "
        _ir_holder_alive "$_IR_REC_PID" "$_IR_REC_TOKEN" && return 1
        _ir_lock_rec_ensure "$@" || return 1
        marker="$lockdir/reap.$_IR_REC_NONCE"
        ln "$_ir_rec" "$marker" 2>/dev/null && break
        [[ -d "$lockdir" ]] || return 2
        _ir_lock_rec_read "$marker" || return 1
    done
    # A refusal drops the marker: nobody else can link that name while this
    # process lives, so keeping it would wedge the lock until this exits.
    if ! _ir_lock_rec_read "$lockdir/owner"; then
        rm -f "$marker"
        return 2
    fi
    if [[ "$chain" != *" $_IR_REC_NONCE "* ]]; then
        rm -f "$marker"
        return 1
    fi
    swap="$lockdir/.swap.${_ir_rec##*/.rec.}"
    if ln "$_ir_rec" "$swap" 2>/dev/null && mv -f "$swap" "$lockdir/owner" 2>/dev/null; then
        return 0
    fi
    rm -f "$swap" "$marker"
    return 1
}

# _ir_lock_contend <lockdir> [<pid> [<owner-token>]] — one attempt on a lock
# dir that exists. 0 acquired, 1 refused, 2 the dir went away mid-attempt (a
# release renamed it) and the caller may retry.
_ir_lock_contend() {
    local lockdir="$1" legacy
    if _ir_lock_rec_read "$lockdir/owner"; then
        _ir_lock_take "$@"
        return $?
    fi
    [[ -d "$lockdir" ]] || return 2
    [[ ! -e "$lockdir/owner" ]] || return 1
    _ir_read_line "$lockdir/pid"
    legacy="$_IR_LINE"
    if [[ -n "$legacy" ]]; then
        _ir_read_line "$lockdir/winpid"
        _ir_holder_alive "$legacy" "$_IR_LINE" && return 1
    fi
    _ir_lock_rec_ensure "$@" || return 1
    if ! ln "$_ir_rec" "$lockdir/owner" 2>/dev/null; then
        [[ -d "$lockdir" ]] || return 2
        return 1
    fi
    # An older copy reclaims by removing and recreating the dir, then stamping
    # `pid`; a changed `pid` means this link landed in that copy's new lock.
    _ir_read_line "$lockdir/pid"
    if [[ "$_IR_LINE" != "$legacy" ]]; then
        rm -f "$lockdir/owner"
        return 1
    fi
    return 0
}

# _ir_try_lock <lockdir> [<pid> [<owner-token>]] — 0 when this call took the
# lock, 1 when a live holder has it. The pid and token stamp the lock for
# another live process: a GPU lease reacquire runs in a short-lived control
# process but restores the lock to the ir-acquire shell that owns the lease.
# Tests seed locks through this call too, never by writing the files.
_ir_try_lock() {
    local lockdir="$1" _ir_rec="" i rc=1
    for (( i = 0; i < 3; i++ )); do
        mkdir "$lockdir" 2>/dev/null || true
        rc=0
        _ir_lock_contend "$@" || rc=$?
        (( rc == 2 )) || break
        rc=1
    done
    if (( rc == 0 )); then
        _ir_lock_mirror "$lockdir" "$_ir_rec"
    fi
    [[ -z "$_ir_rec" ]] || rm -f "$_ir_rec"
    return "$rc"
}

# _ir_lock_detach <lockdir> — releases a lock the caller owns: renames the
# dir aside, then deletes the copy. A dir that cannot be renamed stays in
# place and reads as a dead holder once its owner exits; it is never removed
# in place, where the remove could land after a contender's take.
_ir_lock_detach() {
    local lockdir="$1" gone
    _ir_lock_rand
    gone="${lockdir%/*}/.released.${lockdir##*/}.$$.$_IR_RAND"
    _ir_retry_mv "$lockdir" "$gone" || return 1
    rm -rf "$gone" 2>/dev/null || true
}

# Acquire a single named exclusive lock. Blocks until available or timeout.
# Usage: ir_acquire_exclusive <name> <subdir> [--nonblock] [--timeout SECS]
ir_acquire_exclusive() {
    local name="$1" subdir="$2"
    shift 2
    local nonblock=false
    local timeout
    timeout="$(ir_queue_timeout)"
    while [[ $# -gt 0 ]]; do
        case "$1" in
            --nonblock) nonblock=true ;;
            --timeout)  timeout="$2"; shift ;;
        esac
        shift
    done
    local lockdir="$IR_LOCK_ROOT/$subdir/$name"
    local started
    started="$(date +%s)"
    while true; do
        _ir_quiet_defer_nonbenchmark
        if _ir_try_lock "$lockdir"; then
            _ir_record_held "$lockdir"
            return 0
        fi
        if $nonblock; then
            return 1
        fi
        local now elapsed
        now="$(date +%s)"
        elapsed=$(( now - started ))
        if (( elapsed >= timeout )); then
            echo "ir-acquire: timeout waiting for lock $name ($subdir, ${timeout}s)" >&2
            return 1
        fi
        sleep 0.2
    done
}

# Acquire N CPU slot locks out of the configured budget. Slots are
# represented as slot-N dirs; we walk 1..budget and grab the first N free
# ones. Slot identity is meaningless — only the count matters.
#
# One attempt (the walk, and on a shortfall handing the partials back) runs
# under the cpu/walk gate. Two ungated walks that overlap leapfrog each
# other, both come up short, and the fixed retry sleep keeps them in phase:
# a livelock once a walk outlasts the sleep. A held gate is a failed
# attempt. The gate is never held across a park, a sleep or the command.
# Usage: ir_acquire_cpu <count> [--nonblock] [--timeout SECS]
ir_acquire_cpu() {
    local want="$1"
    shift
    local nonblock=false
    local timeout
    timeout="$(ir_queue_timeout)"
    while [[ $# -gt 0 ]]; do
        case "$1" in
            --nonblock) nonblock=true ;;
            --timeout)  timeout="$2"; shift ;;
        esac
        shift
    done
    local budget
    budget="$(ir_cpu_budget)"
    if (( want > budget )); then
        echo "ir-acquire: requested $want CPU slots > budget $budget; capping" >&2
        want=$budget
    fi
    local started
    started="$(date +%s)"
    local gate="$IR_LOCK_ROOT/cpu/walk"
    local got=()
    while true; do
        _ir_quiet_defer_nonbenchmark
        if _ir_try_lock "$gate"; then
            _ir_record_held "$gate"
            local i
            for (( i=1; i<=budget; i++ )); do
                (( ${#got[@]} >= want )) && break
                local slot="$IR_LOCK_ROOT/cpu/slot-$i"
                if _ir_try_lock "$slot"; then
                    got+=("$slot")
                    _ir_record_held "$slot"
                fi
            done
            # Couldn't get enough — release the partials before the gate.
            if (( ${#got[@]} < want )); then
                for slot in ${got[@]+"${got[@]}"}; do
                    _ir_release_one "$slot"
                done
                got=()
            fi
            _ir_release_one "$gate"
            (( ${#got[@]} == 0 )) || return 0
        fi
        if $nonblock; then
            return 1
        fi
        local now elapsed
        now="$(date +%s)"
        elapsed=$(( now - started ))
        if (( elapsed >= timeout )); then
            echo "ir-acquire: timeout waiting for $want CPU slots (${timeout}s)" >&2
            return 1
        fi
        sleep 0.2
    done
}

# ir_inherited_lock_covers <gpu|perf|benchmark> — true when an enclosing
# ir-acquire, named by the IR_ACQUIRE_HOLDER_PID / IR_ACQUIRE_HOLDER_WINPID /
# IR_ACQUIRE_HELD_VERB it exports to its wrapped command, still owns every
# exclusive lock <verb> needs.
# The locks are not re-entrant: a nested acquire of the same resource waits out
# its queue timeout against its own ancestor. Ownership is re-read from the
# lock dirs, so an env var outliving its holder covers nothing.
#
# The pid alone does not identify the owner on native Windows: the two Cygwin
# runtimes number their pids independently, so a holder in the other runtime
# can carry the ancestor's pid. The lock's winpid record must equal the
# exported one as well; off Windows both are empty.
ir_inherited_lock_covers() {
    local want="$1"
    local holder="${IR_ACQUIRE_HOLDER_PID:-}" held="${IR_ACQUIRE_HELD_VERB:-}"
    local token="${IR_ACQUIRE_HOLDER_WINPID:-}"
    [[ -n "$holder" && -n "$held" ]] || return 1
    local locks
    case "$want:$held" in
        gpu:gpu|gpu:benchmark)   locks="gpu" ;;
        perf:perf|perf:benchmark) locks="perf" ;;
        benchmark:benchmark)      locks="gpu perf" ;;
        *) return 1 ;;
    esac
    local l lockdir
    for l in $locks; do
        lockdir="$IR_LOCK_ROOT/$l/lock"
        _ir_lock_owned_by "$lockdir" "$holder" "$token" || return 1
    done
    return 0
}

# _ir_held_dir — this process's ledger of held locks. The pid alone does not
# name a process on native Windows (the two Cygwin runtimes number their pids
# independently), so the ledger is keyed "<pid>.<windows-pid>" there; two
# holders sharing a pid would otherwise share one ledger and release each
# other's locks on exit.
_ir_held_dir() {
    echo "$IR_LOCK_ROOT/.held/$$${_IR_SELF_WINPID:+.$_IR_SELF_WINPID}"
}

# _ir_lock_owned_by <lockdir> <pid> <owner-token> — the lock's pid and owner
# token both match. Off Windows the token and the record's are both empty. An
# older copy's lock is read from its pid and winpid files, which keeps
# ir_inherited_lock_covers right in a process tree that mixes versions.
#
# Every return in the lock paths names its status: under bash 5.2 a bare
# `return` run anywhere inside a trap handler returns the status from before
# the trap, and ir-acquire releases from its EXIT trap.
_ir_lock_owned_by() {
    local lockdir="$1" pid="$2" token="$3"
    if _ir_lock_rec_read "$lockdir/owner"; then
        [[ "$_IR_REC_PID" == "$pid" && "$_IR_REC_TOKEN" == "$token" ]]
        return $?
    fi
    [[ ! -e "$lockdir/owner" ]] || return 1
    [[ "$(cat "$lockdir/pid" 2>/dev/null || true)" == "$pid" ]] || return 1
    [[ "$(cat "$lockdir/winpid" 2>/dev/null || true)" == "$token" ]]
}

_ir_record_held() {
    local lockdir="$1"
    local helddir
    helddir="$(_ir_held_dir)"
    mkdir -p "$helddir" 2>/dev/null || true
    # A dotfile, so the `*` walks below never read it as a held lock.
    [[ -f "$helddir/.winpid" ]] || _ir_write_winpid "$helddir/.winpid"
    # Use the basename plus the parent dir name so we can reconstruct the
    # full path on release (cpu/slot-3, gpu/lock, etc.).
    # Path encoding: '/' → '__'; resource paths must not contain '__'.
    local rel="${lockdir#$IR_LOCK_ROOT/}"
    local safe
    safe="${rel//\//__}"
    : > "$helddir/$safe"
}

_ir_release_one() {
    local lockdir="$1"
    # Only release if we hold it (defensive against double-release).
    if _ir_lock_owned_by "$lockdir" "$$" "$(ir_self_owner_token)"; then
        _ir_lock_detach "$lockdir" || true
    fi
    local rel="${lockdir#$IR_LOCK_ROOT/}"
    local safe="${rel//\//__}"
    rm -f "$(_ir_held_dir)/$safe" 2>/dev/null || true
}

# Release every lock held by this process. Trap target.
ir_release_all() {
    local heldroot
    heldroot="$(_ir_held_dir)"
    [[ -d "$heldroot" ]] || return 0
    local f
    for f in "$heldroot"/*; do
        [[ -e "$f" ]] || continue
        local safe
        safe="$(basename "$f")"
        # Not `${safe//__/\/}`: bash 3.2 keeps that backslash.
        local rel="${safe//__//}"
        _ir_release_one "$IR_LOCK_ROOT/$rel"
    done
    rm -f "$heldroot/.winpid" 2>/dev/null || true
    rmdir "$heldroot" 2>/dev/null || true
}

# ---------------------------------------------------------------------------
# GPU lease — lend the gpu lock to a child run and take it back
# ---------------------------------------------------------------------------
#
# An ir-acquire that holds the gpu lock records a lease under
# $IR_LOCK_ROOT/leases/<token>/ and exports the token to its command
# (IR_RUN_LOCK_TOKEN). The lock belongs to the ir-acquire shell, not to the
# engine it wraps, so the engine lends it through two control verbs that run
# in their own short-lived process:
#   ir-acquire --lease-release <token>     HELD -> RELEASED
#   ir-acquire --lease-reacquire <token>   RELEASED -> HELD (waits for the lock)
# The record names the owner (pid, winpid record, held ledger); every
# transition re-verifies that owner is alive and never re-stamps the lock to
# the control process. A `released` marker dir is the state; a `transition`
# dir serializes the verbs. The owner's EXIT trap drops the record in either
# state, and takes the same `transition` mutex to do it: a transition whose
# checks passed then runs to completion against a live owner, so the owner's
# lock release can never interleave with it and free a lock a contender has
# since taken.
#
# IR_LEASE_TEST_GATE=<path> parks a release between its checks and the lock
# removal (touches <path>.reached, waits for <path>.go) — the test seam for
# that interleaving.

_ir_lease_dir() {
    [[ "$1" =~ ^[0-9a-f]{32}$ ]] || return 1
    echo "$IR_LOCK_ROOT/leases/$1"
}

# ir_gpu_lease_create — records a lease on the gpu lock this process holds and
# prints its token.
ir_gpu_lease_create() {
    local token lease
    token="$(od -An -N16 -tx1 /dev/urandom | tr -d ' \n')"
    lease="$(_ir_lease_dir "$token")" || return 1
    mkdir -p "$IR_LOCK_ROOT/leases"
    mkdir "$lease"
    echo "$$" > "$lease/holder"
    _ir_write_winpid "$lease/winpid"
    _ir_held_dir > "$lease/held_dir"
    echo "$token"
}

# ir_gpu_lease_drop <token> — the owner's exit path. `dropping` makes a
# reacquire still waiting on the lock give up rather than hold this exit for
# its whole timeout; a mutex whose control process died is broken.
ir_gpu_lease_drop() {
    local lease
    lease="$(_ir_lease_dir "${1:-}")" || return 0
    [[ -d "$lease" ]] || return 0
    mkdir "$lease/dropping" 2>/dev/null || true
    local pid unstamped=0
    until _ir_lease_lock_transition "$lease"; do
        pid="$(cat "$lease/transition/pid" 2>/dev/null || true)"
        if [[ -n "$pid" ]]; then
            _ir_holder_alive "$pid" "$(cat "$lease/transition/winpid" 2>/dev/null || true)" || rm -rf "$lease/transition"
        elif (( ++unstamped > 50 )); then
            rm -rf "$lease/transition"
        fi
        sleep 0.1
    done
    rm -rf "$lease" 2>/dev/null || true
}

# _ir_lease_lock_transition <lease> — the winpid record lands before the pid,
# so a reader that sees the pid can judge it from either Cygwin runtime.
_ir_lease_lock_transition() {
    mkdir "$1/transition" 2>/dev/null || return 1
    _ir_write_winpid "$1/transition/winpid"
    echo "$$" > "$1/transition/pid"
}

# _ir_lease_transition <release|reacquire> <token> [--timeout SECS] — one
# guarded transition. Exit 2 for a malformed token, 1 for any refusal; a
# refusal leaves the lock and the lease as they were.
_ir_lease_transition() {
    local op="$1" token="${2:-}"
    shift 2 || shift $#
    local timeout
    timeout="$(ir_queue_timeout)"
    while [[ $# -gt 0 ]]; do
        case "$1" in
            --timeout) timeout="$2"; shift ;;
        esac
        shift
    done
    local lease
    if ! lease="$(_ir_lease_dir "$token")"; then
        echo "ir-acquire: malformed lease token" >&2
        return 2
    fi
    if [[ ! -d "$lease" ]] || ! _ir_lease_lock_transition "$lease"; then
        echo "ir-acquire: no lease for that token, or a transition is in progress" >&2
        return 1
    fi
    local status=0
    if [[ -d "$lease/dropping" ]]; then
        echo "ir-acquire: lease owner is exiting" >&2
        status=1
    else
        "_ir_lease_$op" "$lease" "$timeout" || status=$?
    fi
    rm -rf "$lease/transition" 2>/dev/null || true
    return "$status"
}

_ir_lease_owner_alive() {
    local lease="$1"
    _ir_holder_alive "$(cat "$lease/holder" 2>/dev/null || true)" "$(cat "$lease/winpid" 2>/dev/null || true)"
}

_ir_lease_release() {
    local lease="$1"
    local holder owner lockdir="$IR_LOCK_ROOT/gpu/lock"
    holder="$(cat "$lease/holder")"
    owner="$(cat "$lease/winpid" 2>/dev/null || true)"
    if ! _ir_lease_owner_alive "$lease"; then
        echo "ir-acquire: lease owner $holder is not alive" >&2
        return 1
    fi
    if [[ -d "$lease/released" ]]; then
        echo "ir-acquire: lease is already released" >&2
        return 1
    fi
    if ! _ir_lock_owned_by "$lockdir" "$holder" "$owner"; then
        echo "ir-acquire: gpu lock is not held by lease owner $holder" >&2
        return 1
    fi
    if [[ -n "${IR_LEASE_TEST_GATE:-}" ]]; then
        : > "$IR_LEASE_TEST_GATE.reached"
        while [[ ! -e "$IR_LEASE_TEST_GATE.go" ]]; do sleep 0.05; done
    fi
    # The state flips before the lock goes: a release interrupted here leaves
    # RELEASED with the owner still stamped, which reacquire accepts as-is.
    mkdir "$lease/released"
    rm -f "$(cat "$lease/held_dir")/gpu__lock" 2>/dev/null || true
    _ir_lock_detach "$lockdir"
}

_ir_lease_reacquire() {
    local lease="$1" timeout="$2"
    local holder owner helddir lockdir="$IR_LOCK_ROOT/gpu/lock"
    holder="$(cat "$lease/holder")"
    owner="$(cat "$lease/winpid" 2>/dev/null || true)"
    helddir="$(cat "$lease/held_dir")"
    if [[ ! -d "$lease/released" ]]; then
        echo "ir-acquire: lease is not released" >&2
        return 1
    fi
    local started now
    started="$(date +%s)"
    while ! _ir_lock_owned_by "$lockdir" "$holder" "$owner"; do
        if [[ -d "$lease/dropping" ]] || ! _ir_lease_owner_alive "$lease"; then
            echo "ir-acquire: lease owner $holder is exiting or gone" >&2
            return 1
        fi
        # The record, and so any takeover marker it links, names the owner:
        # a control process killed between that link and its replace of
        # `owner` leaves the marker reading live, and the lock held, until the
        # owner exits.
        _ir_try_lock "$lockdir" "$holder" "$owner" && break
        now="$(date +%s)"
        if (( now - started >= timeout )); then
            echo "ir-acquire: timeout reacquiring the gpu lock (${timeout}s)" >&2
            return 1
        fi
        sleep 0.2
    done
    # The owner may have begun exiting, or died, while this waited. Its ledger
    # does not list a lock stamped here, so its trap would leave the lock
    # behind. An exiting owner is parked on the mutex, still alive, so no
    # reclaimer can have taken the lock from it.
    if [[ -d "$lease/dropping" ]] || ! _ir_lease_owner_alive "$lease"; then
        _ir_lock_owned_by "$lockdir" "$holder" "$owner" && _ir_lock_detach "$lockdir"
        echo "ir-acquire: lease owner $holder exited during reacquire" >&2
        return 1
    fi
    mkdir -p "$helddir" 2>/dev/null || true
    : > "$helddir/gpu__lock"
    rmdir "$lease/released"
}

# _ir_lock_sweep_one <lockdir> — takes a dead holder's lock through the same
# transfer a contender uses, then releases it. A lock a contender took first
# reads live and is left alone; nothing here removes a lock it does not own.
_ir_lock_sweep_one() {
    local lockdir="$1" _ir_rec="" rc=0
    _ir_lock_contend "$lockdir" "$$" || rc=$?
    [[ -z "$_ir_rec" ]] || rm -f "$_ir_rec"
    if (( rc == 0 )); then
        _ir_lock_detach "$lockdir" || true
    fi
}

# Sweep dead holders' locks. Cheap enough to run on every ir-acquire --info
# call; deliberate (no separate gc daemon).
ir_sweep_stale() {
    local heldroot="$IR_LOCK_ROOT/.held"
    [[ -d "$heldroot" ]] || return 0
    local pdir
    for pdir in "$heldroot"/*; do
        [[ -d "$pdir" ]] || continue
        local pid token
        pid="$(basename "$pdir")"
        pid="${pid%%.*}"
        token="$(cat "$pdir/.winpid" 2>/dev/null || true)"
        if ! _ir_holder_alive "$pid" "$token"; then
            local f
            for f in "$pdir"/*; do
                [[ -e "$f" ]] || continue
                local safe rel lockdir
                safe="$(basename "$f")"
                rel="${safe//__//}"
                lockdir="$IR_LOCK_ROOT/$rel"
                if _ir_lock_owned_by "$lockdir" "$pid" "$token"; then
                    _ir_lock_sweep_one "$lockdir"
                fi
            done
            rm -rf "$pdir" 2>/dev/null || true
        fi
    done
}

# _ir_lock_holder_info <lockdir> — print the holder age and, where ps supports
# it, the command line. A missing command is intentionally not an error: the
# MSYS2 ps implementation does not accept -o. The age comes from the owner
# record's nonce, which a transfer replaces; an older copy's lock has only
# `acquired_at`.
_ir_lock_holder_info() {
    local lockdir="$1" holder="$2" acquired_at="" now age=0 command_line=""
    if _ir_lock_rec_read "$lockdir/owner"; then
        acquired_at="${_IR_REC_NONCE%%.*}"
    else
        acquired_at="$(cat "$lockdir/acquired_at" 2>/dev/null || true)"
    fi
    if [[ "$acquired_at" =~ ^[0-9]+$ ]]; then
        now="$(date +%s)"
        if (( now >= acquired_at )); then
            age=$(( now - acquired_at ))
        fi
    fi
    command_line="$(ps -o command= -p "$holder" 2>/dev/null || true)"
    printf 'pid %s (age %ss' "$holder" "$age"
    [[ -n "$command_line" ]] && printf ', command: %s' "$command_line"
    printf ')'
}

# Print human-readable lock state. Used by ir-acquire --info.
ir_print_lock_state() {
    ir_sweep_stale
    local budget
    budget="$(ir_cpu_budget)"
    local in_use=0
    local i
    for (( i=1; i<=budget; i++ )); do
        local slot="$IR_LOCK_ROOT/cpu/slot-$i"
        if [[ -d "$slot" ]]; then
            local h
            h="$(_ir_lock_holder "$slot" || echo "?")"
            echo "cpu slot-$i lock: held by $(_ir_lock_holder_info "$slot" "$h")"
            in_use=$(( in_use + 1 ))
        fi
    done
    local gpu_holder=""
    [[ -d "$IR_LOCK_ROOT/gpu/lock" ]] && gpu_holder="$(_ir_lock_holder "$IR_LOCK_ROOT/gpu/lock" || echo "?")"
    local perf_holder=""
    [[ -d "$IR_LOCK_ROOT/perf/lock" ]] && perf_holder="$(_ir_lock_holder "$IR_LOCK_ROOT/perf/lock" || echo "?")"

    echo "cpu budget: $budget ($in_use in use, $(( budget - in_use )) free)"
    # A held gate stalls every CPU acquisition on the host; free prints nothing.
    local gate="$IR_LOCK_ROOT/cpu/walk" gate_holder
    if [[ -d "$gate" ]] && gate_holder="$(_ir_lock_holder "$gate")"; then
        echo "cpu walk gate: held by $(_ir_lock_holder_info "$gate" "$gate_holder")"
    fi
    if [[ -n "$gpu_holder" ]]; then
        echo "gpu lock: held by $(_ir_lock_holder_info "$IR_LOCK_ROOT/gpu/lock" "$gpu_holder")"
    else
        echo "gpu lock: free"
    fi
    if [[ -n "$perf_holder" ]]; then
        echo "perf lock: held by $(_ir_lock_holder_info "$IR_LOCK_ROOT/perf/lock" "$perf_holder")"
    else
        echo "perf lock: free"
    fi
}
