# Bash programmable completion for fleet-run, fleet-build, and fleet-debug.
# Bash 3.2+ (macOS default). Uses compgen, not mapfile.
#
# fleet-run: words not starting with "-" complete built executable basenames
# (same set as "fleet-run-targets --plain"). Leading "-" completes flags.
# After "--targets", completes fleet-run-targets flags when appropriate.
#
# fleet-build: after "--target", completes curated CMake demo/test names
# (same as "fleet-run-targets --plan --plain").
#
# install.sh symlinks this file into:
#   ${XDG_DATA_HOME:-$HOME/.local/share}/bash-completion/completions/{fleet-run,fleet-build,fleet-debug}
# Requires bash-completion to load ~/.local/share/bash-completion/completions/*
# (many distros and Homebrew bash-completion@2 do). Otherwise: source this file.
#
# zsh (macOS default): use completions/irreden-fleet.zsh (sourced from ~/.zshrc);
# zsh does not read the bash-completion completions dir by default.

_irreden_fleet_script_dir() {
    # Directory containing fleet-run-targets (…/scripts/fleet).
    # BASH_SOURCE may be a symlink in ~/.local/share/bash-completion/completions/;
    # resolve so ../ still lands on scripts/fleet.
    local f compdir
    f="${BASH_SOURCE[0]}"
    while [[ -L "$f" ]]; do
        compdir="$(cd "$(dirname "$f")" && pwd)"
        f="$(readlink "$f")"
        [[ "$f" != /* ]] && f="$compdir/$f"
    done
    cd "$(dirname "$f")/.." && pwd
}

_irreden_fleet_run_targets_cmd() {
    if command -v fleet-run-targets >/dev/null 2>&1; then
        echo "fleet-run-targets"
    else
        local d
        d="$(_irreden_fleet_script_dir)"
        if [[ -x "$d/fleet-run-targets" ]]; then
            echo "$d/fleet-run-targets"
        fi
    fi
}

_irreden_fleet_built_words() {
    local cmd
    cmd="$(_irreden_fleet_run_targets_cmd)"
    [[ -n "$cmd" ]] || return 0
    "$cmd" --plain 2>/dev/null
}

_irreden_fleet_plan_words() {
    local cmd
    cmd="$(_irreden_fleet_run_targets_cmd)"
    [[ -n "$cmd" ]] || return 0
    "$cmd" --plan --plain 2>/dev/null
}

_irreden_fleet_debug_words() {
    {
        _irreden_fleet_built_words
        _irreden_fleet_plan_words
    } | LC_ALL=C sort -u
}

_irreden_fleet_run_targets_mode() {
    local i
    for ((i = 1; i < COMP_CWORD; i++)); do
        if [[ "${COMP_WORDS[i]}" == --targets ]]; then
            return 0
        fi
    done
    return 1
}

_irreden_fleet_run_first_exe_slot() {
    # True (status 0) if the word being completed is the executable name:
    # first non-option token, not after "--", not in "--targets" mode.
    local i a skip=0
    for ((i = 1; i < COMP_CWORD; i++)); do
        a="${COMP_WORDS[i]}"
        [[ -z "$a" ]] && continue
        if ((skip)); then
            skip=0
            continue
        fi
        if [[ "$a" == -- ]]; then
            return 1
        fi
        if [[ "$a" == --targets ]]; then
            return 1
        fi
        case "$a" in
            --build-dir | --timeout | -t) skip=1 ;;
            -*) ;;
            *) return 1 ;; # already saw first positional before cursor
        esac
    done
    return 0
}

_irreden_fleet_run_complete() {
    local cur="${COMP_WORDS[COMP_CWORD]}"
    local prev="${COMP_WORDS[COMP_CWORD - 1]}"

    if _irreden_fleet_run_targets_mode; then
        if [[ "$cur" == -* ]]; then
            COMPREPLY=($(compgen -W "--help --plain --plan --built --build-dir --scope --all" -- "$cur"))
            return 0
        fi
        if [[ "$prev" == --build-dir || "$prev" == --scope ]]; then
            return 1
        fi
        return 1
    fi

    if [[ "$cur" == -* ]]; then
        COMPREPLY=($(compgen -W "--help --build-dir --timeout --targets -t" -- "$cur"))
        return 0
    fi

    case "$prev" in
        --build-dir | --timeout | -t) return 1 ;;
    esac

    if _irreden_fleet_run_first_exe_slot; then
        COMPREPLY=($(compgen -W "$(_irreden_fleet_built_words | tr '\n' ' ')" -- "$cur"))
        return 0
    fi

    return 1
}

_irreden_fleet_build_complete() {
    local cur="${COMP_WORDS[COMP_CWORD]}"
    local prev="${COMP_WORDS[COMP_CWORD - 1]}"

    if [[ "$cur" == -* ]]; then
        COMPREPLY=($(compgen -W "--target -j --parallel clean" -- "$cur"))
        return 0
    fi

    if [[ "$prev" == --target ]]; then
        COMPREPLY=($(compgen -W "$(_irreden_fleet_plan_words | tr '\n' ' ')" -- "$cur"))
        return 0
    fi

    return 1
}

_irreden_fleet_debug_first_target_slot() {
    local i a skip=0
    for ((i = 1; i < COMP_CWORD; i++)); do
        a="${COMP_WORDS[i]}"
        [[ -z "$a" ]] && continue
        if ((skip)); then
            skip=0
            continue
        fi
        if [[ "$a" == -- ]]; then
            return 1
        fi
        case "$a" in
            --build-dir | --debugger) skip=1 ;;
            --batch | --no-build) ;;
            -*) ;;
            *) return 1 ;;
        esac
    done
    return 0
}

_irreden_fleet_debug_complete() {
    local cur="${COMP_WORDS[COMP_CWORD]}"
    local prev="${COMP_WORDS[COMP_CWORD - 1]}"

    if [[ "$cur" == -* ]]; then
        COMPREPLY=($(compgen -W "--help --batch --no-build --build-dir --debugger" -- "$cur"))
        return 0
    fi

    case "$prev" in
        --build-dir | --debugger) return 1 ;;
    esac

    if _irreden_fleet_debug_first_target_slot; then
        COMPREPLY=($(compgen -W "$(_irreden_fleet_debug_words | tr '\n' ' ')" -- "$cur"))
        return 0
    fi

    return 1
}

complete -F _irreden_fleet_run_complete fleet-run
complete -F _irreden_fleet_build_complete fleet-build
complete -F _irreden_fleet_debug_complete fleet-debug
