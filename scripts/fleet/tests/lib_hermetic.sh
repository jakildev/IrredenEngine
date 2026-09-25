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
# via CreateProcess, which appends only `.exe` to a bare name, so it skips an
# extension-less shell script and falls through to the real gh.exe further
# down PATH (scripts/fleet/CLAUDE.md "An extensionless PATH stub" rule) —
# measured live: an escaped subprocess call from a hermetic suite mutated
# real GitHub issues on the Windows fleet host.
#
# The fix has three parts:
#   - the subject resolves `gh` once via `shutil.which("gh") or "gh"` (honors
#     PATH order and PATHEXT; a passthrough on POSIX — fleet-plan-lint's
#     precedent) and reuses that handle in every `subprocess.run([...])`;
#   - the suite writes its stub as a `#!/usr/bin/env python3` script plus a
#     `gh.bat` twin (`python3 "%~dp0gh" %*`) beside the extensionless one;
#   - the suite calls hermetic_poison_gh_env right after creating its temp
#     root.
#
# A native-Windows `shutil.which()` hit on the `gh.bat` twin still isn't
# safe to hand to `subprocess.run([...])` as-is: CreateProcess routes a
# `.bat` target through cmd.exe, which re-parses the whole argv for its own
# metacharacters (`&`, `|`, `^`, ...) before the batch body's `%*` sees
# anything — a REST query string's `&per_page=` silently splits into two
# commands. A subject detects the twin (`.bat` beside this exact
# `#!/usr/bin/env python3` extensionless sibling) and execs the sibling
# through `sys.executable` instead, bypassing cmd.exe entirely
# (`fleet-decisions`'s `_gh_argv`).
#
# hermetic_poison_gh_env poisons the real binary's credentials, config, and
# default host, so a call that escapes the PATH stub fails before it can
# authenticate against or mutate live GitHub. It is a fail-closed backstop
# beside the stub, not a replacement for it — it covers suites whose subject
# has not taken the resolution fix yet, and a suite still needs its own stub
# to exercise the normal path.
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
