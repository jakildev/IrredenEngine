# gh-function.sh — the exported `gh` shell function that routes a dispatched
# role's shell `gh` calls through the accounting launcher. Sourced, never
# executed.
#
# A function, not a PATH prefix: a pane is `exec $SHELL -i`, and the native-
# Windows ~/.bashrc prepends the mingw64 dir holding gh.exe after tmux has
# supplied the environment, so any PATH entry installed before it loses.
# Function lookup precedes PATH lookup, and `export -f` carries the function
# into every child bash (the agent's Bash tool) even when it reads the rc
# files again.
#
# Accounting off (or no launcher configured): plain `command gh`, so an
# inherited function never changes a non-fleet shell's behavior. Accounting
# on: the launcher resolves the caller's current PATH, so a stub a test
# prepends later still wins uncounted.

gh() {
    if [[ "${FLEET_GH_ACCOUNTING:-}" == "1" && -n "${FLEET_GH_LAUNCHER:-}" ]]; then
        "$BASH" "$FLEET_GH_LAUNCHER" "$@"
    else
        command gh "$@"
    fi
}
export -f gh
