#!/usr/bin/env bash
# engine/tools/lib/concurrency_helpers.sh — shared helpers for ir-* tools.
#
# Sourced (not exec'd) by ir-host-probe, ir-acquire, and any future ir-* tool
# that needs the engine-level coordination primitives. The functions defined
# here are the only thing those tools should depend on from this library —
# everything else (paths, defaults) is computed from the three-config-layer
# resolver below.
#
# The lock primitives use atomic mkdir, which works identically on Linux,
# macOS, and WSL — no flock dependency. Each lock holds a `pid` file so
# `ir-acquire --info` can attribute waits to the holding process.

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

# ir_default_build_dir <worktree-root> — the build tree ir-build/ir-run
# use when IRREDEN_BUILD_DIR is not set. Engine checkouts build in-tree
# (<root>/build, the preset binaryDir). A downstream-creation worktree
# has no presets of its own — its targets compile through the enclosing
# engine with the worktree added as a user project — so it gets a
# dedicated dir at <engine>/build-<creation>-<agent>/ (covered by the
# engine .gitignore's build-*/ pattern), keeping build output out of the
# creation repo and away from the engine's own build trees.
ir_default_build_dir() {
    local root="$1"
    local eng
    if eng="$(ir_creation_worktree_engine_root "$root")"; then
        local rel="${root#"$eng"/creations/}"
        local creation="${rel%%/*}"
        echo "$eng/build-$creation-$(basename "$root")"
    else
        echo "$root/build"
    fi
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

ir_quiet_park() {
    local tag="$1"
    shift
    python3 "$IR_QUIET_HELPER" park "$tag" "$@"
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
# Lock primitives — atomic mkdir, PID-death recovery
# ---------------------------------------------------------------------------
#
# A "lock" is a directory under $IR_LOCK_ROOT/{cpu,gpu,perf}/. mkdir is the
# atomic operation: only one caller can create a given dir, the rest see
# EEXIST. After creating it we write our PID into the dir; on acquire
# contention, we re-check and reclaim if the holder PID is gone.
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

# _ir_holder_alive <pid> <winpid-file> — same-runtime holders are judged by
# `kill -0` alone; a holder from the other runtime by its Windows pid.
_ir_holder_alive() {
    local pid="$1" winpid_file="$2"
    _ir_pid_alive "$pid" && return 0
    [[ -f "$winpid_file" ]] || return 1
    local winpid="" root=""
    read -r winpid root < "$winpid_file" || true
    [[ -n "$winpid" && "$root" != "$_IR_RUNTIME_ROOT" ]] || return 1
    ps -W 2>/dev/null | awk -v w="$winpid" '$4 == w { found = 1 } END { exit !found }'
}

_ir_lock_holder() {
    local lockdir="$1"
    [[ -f "$lockdir/pid" ]] || return 1
    cat "$lockdir/pid" 2>/dev/null
}

# Try to create a lock dir. If it exists, check whether the holder is dead;
# if so, reclaim. Returns 0 on success, 1 if the lock is held by a live PID.
_ir_stamp_lock() {
    local lockdir="$1"
    echo "$$" > "$lockdir/pid"
    date +%s > "$lockdir/acquired_at"
    _ir_write_winpid "$lockdir/winpid"
}

_ir_try_lock() {
    local lockdir="$1"
    if mkdir "$lockdir" 2>/dev/null; then
        _ir_stamp_lock "$lockdir"
        return 0
    fi
    local holder
    holder="$(_ir_lock_holder "$lockdir" || echo "")"
    if [[ -n "$holder" ]] && _ir_holder_alive "$holder" "$lockdir/winpid"; then
        return 1
    fi
    # Stale — reclaim. Use rm -rf to nuke any files left by the dead holder,
    # then re-create atomically. The re-create may still lose to a concurrent
    # reclaimer; that's correct (the other wins).
    rm -rf "$lockdir" 2>/dev/null || true
    if mkdir "$lockdir" 2>/dev/null; then
        _ir_stamp_lock "$lockdir"
        return 0
    fi
    return 1
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
    local got=()
    while true; do
        _ir_quiet_defer_nonbenchmark
        local i
        for (( i=1; i<=budget; i++ )); do
            (( ${#got[@]} >= want )) && break
            local slot="$IR_LOCK_ROOT/cpu/slot-$i"
            if _ir_try_lock "$slot"; then
                got+=("$slot")
                _ir_record_held "$slot"
            fi
        done
        if (( ${#got[@]} >= want )); then
            return 0
        fi
        # Couldn't get enough — release the partials and either retry or fail.
        for slot in "${got[@]}"; do
            _ir_release_one "$slot"
        done
        got=()
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

# _ir_lock_owned_by <lockdir> <pid> <owner-token> — the lock's pid and winpid
# records both match. Off Windows the token and the record are both empty.
_ir_lock_owned_by() {
    local lockdir="$1" pid="$2" token="$3"
    [[ "$(_ir_lock_holder "$lockdir" || echo "")" == "$pid" ]] || return 1
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
        rm -rf "$lockdir" 2>/dev/null || true
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
        local rel="${safe//__/\/}"
        _ir_release_one "$IR_LOCK_ROOT/$rel"
    done
    rm -f "$heldroot/.winpid" 2>/dev/null || true
    rmdir "$heldroot" 2>/dev/null || true
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
        if ! _ir_holder_alive "$pid" "$pdir/.winpid"; then
            local f
            for f in "$pdir"/*; do
                [[ -e "$f" ]] || continue
                local safe rel lockdir
                safe="$(basename "$f")"
                rel="${safe//__/\/}"
                lockdir="$IR_LOCK_ROOT/$rel"
                # Only nuke if the lock still attributes to the dead holder.
                if _ir_lock_owned_by "$lockdir" "$pid" "$token"; then
                    rm -rf "$lockdir" 2>/dev/null || true
                fi
            done
            rm -rf "$pdir" 2>/dev/null || true
        fi
    done
}

# Print human-readable lock state. Used by ir-acquire --info.
ir_print_lock_state() {
    ir_sweep_stale
    local budget
    budget="$(ir_cpu_budget)"
    local in_use=0
    local i
    local cpu_holders=()
    for (( i=1; i<=budget; i++ )); do
        local slot="$IR_LOCK_ROOT/cpu/slot-$i"
        if [[ -d "$slot" ]]; then
            local h
            h="$(_ir_lock_holder "$slot" || echo "?")"
            cpu_holders+=("$h")
            in_use=$(( in_use + 1 ))
        fi
    done
    local gpu_holder=""
    [[ -d "$IR_LOCK_ROOT/gpu/lock" ]] && gpu_holder="$(_ir_lock_holder "$IR_LOCK_ROOT/gpu/lock" || echo "?")"
    local perf_holder=""
    [[ -d "$IR_LOCK_ROOT/perf/lock" ]] && perf_holder="$(_ir_lock_holder "$IR_LOCK_ROOT/perf/lock" || echo "?")"

    echo "cpu budget: $budget ($in_use in use, $(( budget - in_use )) free)"
    if (( ${#cpu_holders[@]} > 0 )); then
        local uniq
        uniq="$(printf '%s\n' "${cpu_holders[@]}" | sort -u | tr '\n' ' ' | sed 's/ $//')"
        echo "  cpu holders (pids): $uniq"
    fi
    if [[ -n "$gpu_holder" ]]; then
        echo "gpu lock: held by pid $gpu_holder"
    else
        echo "gpu lock: free"
    fi
    if [[ -n "$perf_holder" ]]; then
        echo "perf lock: held by pid $perf_holder"
    else
        echo "perf lock: free"
    fi
}
