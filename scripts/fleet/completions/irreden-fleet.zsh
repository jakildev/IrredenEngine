# Zsh: fleet-run / fleet-build / fleet-debug / ir-run / ir-build completion via bashcompinit + fleet-run.bash
#
# Install: scripts/fleet/install.sh symlinks this file and fleet-run.bash into
#   ~/.zsh/completions/ and idempotently appends a marked source line to
#   ~/.zshrc (unless --no-zshrc / IRREDEN_INSTALL_SKIP_ZSHRC).
#
# Manual use: source this file from ~/.zshrc (runs compinit -C if needed):
#   [[ -r ~/.zsh/completions/irreden-fleet.zsh ]] && source ~/.zsh/completions/irreden-fleet.zsh
#
# Requires zsh's bashcompinit (ships with zsh). Does not use bash-completion.

emulate -L zsh

# bashcompinit's `complete` calls compdef; that requires compinit first.
if ! (( $+functions[compdef] )); then
    autoload -Uz compinit && compinit -C
fi

autoload -Uz bashcompinit
bashcompinit -c

local _fleet_bash="${${(%):-%x}:A:h}/fleet-run.bash"
if [[ ! -r $_fleet_bash ]]; then
    print -u2 "irreden-fleet.zsh: missing fleet-run.bash next to this file (expected: $_fleet_bash)"
    return 1
fi

source "$_fleet_bash"
