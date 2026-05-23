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

# Engine root: walk up from this file. Same approach as scripts/fleet/fleet-common.sh.
_ir_helpers_resolve_engine_root() {
    local here
    here="$(cd "$(dirname "${BASH_SOURCE[1]}")" && pwd)"
    # bin/ scripts source this from lib/; lib/ is at $engine/engine/tools/lib/.
    # Walk up to find the marker file CMakeLists.txt at engine root.
    while [[ "$here" != "/" ]]; do
        if [[ -f "$here/CMakePresets.json" && -d "$here/engine" ]]; then
            echo "$here"
            return 0
        fi
        here="$(dirname "$here")"
    done
    echo "ir-tools: cannot resolve engine root from $(dirname "${BASH_SOURCE[1]}")" >&2
    return 1
}

IR_ENGINE_ROOT="$(_ir_helpers_resolve_engine_root)"
IR_TOOLS_DIR="$IR_ENGINE_ROOT/engine/tools"
IR_DEFAULTS_TOML="$IR_TOOLS_DIR/concurrency.toml"
IR_HOST_TOML="${IR_HOST_TOML:-$HOME/.config/irreden/host.toml}"

# Lock dir lives in a runtime-temp location so it survives across shells but
# clears on reboot. XDG_RUNTIME_DIR is Linux-only; macOS doesn't set it.
if [[ -n "${XDG_RUNTIME_DIR:-}" ]]; then
    IR_LOCK_ROOT="${XDG_RUNTIME_DIR}/irreden/locks"
else
    IR_LOCK_ROOT="/tmp/irreden-${USER:-$(id -un)}/locks"
fi
IR_CACHE_ROOT="${XDG_CACHE_HOME:-$HOME/.cache}/irreden"

mkdir -p "$IR_LOCK_ROOT/cpu" "$IR_LOCK_ROOT/gpu" "$IR_LOCK_ROOT/perf" "$IR_CACHE_ROOT"

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

# ---------------------------------------------------------------------------
# Lock primitives — atomic mkdir, PID-death recovery
# ---------------------------------------------------------------------------
#
# A "lock" is a directory under $IR_LOCK_ROOT/{cpu,gpu,perf}/. mkdir is the
# atomic operation: only one caller can create a given dir, the rest see
# EEXIST. After creating it we write our PID into the dir; on acquire
# contention, we re-check and reclaim if the holder PID is gone.
#
# Held resources are tracked in a per-process list at
# $IR_LOCK_ROOT/.held/$pid/ — the trap in ir-acquire walks it on exit.

_ir_pid_alive() {
    local pid="$1"
    [[ -n "$pid" ]] || return 1
    kill -0 "$pid" 2>/dev/null
}

_ir_lock_holder() {
    local lockdir="$1"
    [[ -f "$lockdir/pid" ]] || return 1
    cat "$lockdir/pid" 2>/dev/null
}

# Try to create a lock dir. If it exists, check whether the holder is dead;
# if so, reclaim. Returns 0 on success, 1 if the lock is held by a live PID.
_ir_try_lock() {
    local lockdir="$1"
    if mkdir "$lockdir" 2>/dev/null; then
        echo "$$" > "$lockdir/pid"
        date +%s > "$lockdir/acquired_at"
        return 0
    fi
    local holder
    holder="$(_ir_lock_holder "$lockdir" || echo "")"
    if [[ -n "$holder" ]] && _ir_pid_alive "$holder"; then
        return 1
    fi
    # Stale — reclaim. Use rm -rf to nuke any pid/acquired_at files left by
    # the dead holder, then re-create atomically. The re-create may still
    # lose to a concurrent reclaimer; that's correct (the other wins).
    rm -rf "$lockdir" 2>/dev/null || true
    if mkdir "$lockdir" 2>/dev/null; then
        echo "$$" > "$lockdir/pid"
        date +%s > "$lockdir/acquired_at"
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

_ir_record_held() {
    local lockdir="$1"
    mkdir -p "$IR_LOCK_ROOT/.held/$$" 2>/dev/null || true
    # Use the basename plus the parent dir name so we can reconstruct the
    # full path on release (cpu/slot-3, gpu/lock, etc.).
    # Path encoding: '/' → '__'; resource paths must not contain '__'.
    local rel="${lockdir#$IR_LOCK_ROOT/}"
    local safe
    safe="${rel//\//__}"
    : > "$IR_LOCK_ROOT/.held/$$/$safe"
}

_ir_release_one() {
    local lockdir="$1"
    # Only release if we hold it (defensive against double-release).
    local holder
    holder="$(_ir_lock_holder "$lockdir" || echo "")"
    if [[ "$holder" == "$$" ]]; then
        rm -rf "$lockdir" 2>/dev/null || true
    fi
    local rel="${lockdir#$IR_LOCK_ROOT/}"
    local safe="${rel//\//__}"
    rm -f "$IR_LOCK_ROOT/.held/$$/$safe" 2>/dev/null || true
}

# Release every lock held by this process. Trap target.
ir_release_all() {
    local heldroot="$IR_LOCK_ROOT/.held/$$"
    [[ -d "$heldroot" ]] || return 0
    local f
    for f in "$heldroot"/*; do
        [[ -e "$f" ]] || continue
        local safe
        safe="$(basename "$f")"
        local rel="${safe//__/\/}"
        _ir_release_one "$IR_LOCK_ROOT/$rel"
    done
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
        local pid
        pid="$(basename "$pdir")"
        if ! _ir_pid_alive "$pid"; then
            local f
            for f in "$pdir"/*; do
                [[ -e "$f" ]] || continue
                local safe rel lockdir
                safe="$(basename "$f")"
                rel="${safe//__/\/}"
                lockdir="$IR_LOCK_ROOT/$rel"
                # Only nuke if the lock still attributes to the dead holder.
                local holder
                holder="$(_ir_lock_holder "$lockdir" || echo "")"
                if [[ "$holder" == "$pid" ]]; then
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
