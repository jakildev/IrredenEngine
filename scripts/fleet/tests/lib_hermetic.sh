# Poison-environment helper for hermetic bash tests that stub `gh` on PATH.
# Sourced, never executed — the name deliberately avoids the test_*.sh
# pattern so anything that globs for tests skips it:
#
#     source "$(dirname "$0")/lib_hermetic.sh"
#     TMPROOT=$(mktemp -d)
#     hermetic_poison_gh_env "$TMPROOT"
#
# A suite makes itself hermetic by writing a `gh` bash stub into a directory
# it prepends to PATH. That is invisible to a subject reaching `gh` through
# Python `subprocess` on native Windows: mingw64 python resolves executables
# via CreateProcess + PATHEXT, which skips an extension-less shell script and
# falls through to the real gh.exe further down PATH (scripts/fleet/CLAUDE.md
# "Native Windows Python/PATHEXT" rule) — measured live: an escaped
# subprocess call from a hermetic suite mutated real GitHub issues on the
# Windows fleet host.
#
# hermetic_poison_gh_env poisons the real binary's credentials, config, and
# default host, so a call that escapes the PATH stub fails before it can
# authenticate against or mutate live GitHub. It is a fail-closed backstop
# beside the stub, not a replacement for it — production `gh` resolution is
# untouched, and a suite still needs its own stub to exercise the normal
# path.
#
# Call it after the suite's own temp root exists: GH_CONFIG_DIR nests inside
# that root, so the suite's own cleanup trap removes it and no state rides
# outside the sandbox.
hermetic_poison_gh_env() {
    local root="${1:?hermetic_poison_gh_env requires the suites temp root}"
    export GH_CONFIG_DIR="$root/hermetic-gh-config"
    mkdir -p "$GH_CONFIG_DIR"
    export GH_TOKEN="hermetic-test-invalid"
    export GH_HOST="invalid.invalid"
}
