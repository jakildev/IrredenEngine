#!/usr/bin/env bash
# Tests for setup-windows.sh's pane-git prerequisite.
#
# setup-windows.sh runs in MSYS2 bash, whose own `git` is MSYS2's pacman git;
# the agent panes run Git Bash and resolve Git for Windows' git. The fixture
# models that split: the MSYS2-side `git` stub on PATH is new (2.46.0), while
# the fake Git Bash resolves whatever the pane fixture puts first — including
# a `~/.bashrc` that prepends an older git, the documented Windows .bashrc
# shape. Setup must grade the pane's git through fleet-pr-overlap's floor and
# stop before cloning, writing config, or editing ~/.bashrc.
#
# Hermetic: a sandbox copy of setup-windows.sh (+ fleet-pr-overlap), a temp
# HOME and FLEET_CLONE, and stubs for every prerequisite tool.

set -uo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$SCRIPT_DIR/lib_assert.sh"

SETUP_SRC="$SCRIPT_DIR/../setup-windows.sh"
OVERLAP_SRC="$SCRIPT_DIR/../fleet-pr-overlap"
for subject in "$SETUP_SRC" "$OVERLAP_SRC"; do
    if [[ ! -f "$subject" ]]; then
        echo "SKIP: subject not found: $subject" >&2
        exit 3
    fi
done

TMPROOT="$(mktemp -d "${TMPDIR:-/tmp}/setup-windows-git.XXXXXX")"
cleanup() { rm -rf "$TMPROOT"; }
trap cleanup EXIT
REAL_BASH="$BASH"

STUB="$TMPROOT/stub-bin"
MSYS_GIT_LOG="$TMPROOT/msys-git.log"
mkdir -p "$STUB"
for tool in tmux jq claude pgrep ruff; do
    printf '#!/usr/bin/env bash\nexit 0\n' > "$STUB/$tool"
done
printf '#!/usr/bin/env bash\necho MINGW64_NT-10.0-19045\n' > "$STUB/uname"
cat > "$STUB/git" <<EOF
#!/usr/bin/env bash
printf '%s\n' "\$*" >> "$MSYS_GIT_LOG"
[[ "\${1:-}" == "--version" ]] && echo "git version 2.46.0"
exit 0
EOF
chmod +x "$STUB"/*

# The soft-dependency fixture: PATH is exactly $BARE, so a host ruff or
# clang-format can't leak in. It holds the prerequisite stubs minus ruff, a
# symlink farm of the real tools the script runs, and pacman/pipx stubs that
# log every call (pacman exits $STUB_PACMAN_RC).
BARE="$TMPROOT/bare-bin"
INSTALL_LOG="$TMPROOT/install.log"
PYTHON_ABS="$(command -v python3)"
mkdir -p "$BARE"
for tool in tmux jq claude pgrep git uname; do cp "$STUB/$tool" "$BARE/$tool"; done
for tool in cat dirname grep head mkdir sed tr; do ln -s "$(command -v "$tool")" "$BARE/$tool"; done
ln -s "$REAL_BASH" "$BARE/bash"
cat > "$BARE/pacman" <<EOF
#!$REAL_BASH
printf 'pacman %s\n' "\$*" >> "$INSTALL_LOG"
exit "\${STUB_PACMAN_RC:-0}"
EOF
cat > "$BARE/pipx" <<EOF
#!$REAL_BASH
printf 'pipx %s\n' "\$*" >> "$INSTALL_LOG"
exit 0
EOF
chmod +x "$BARE/pacman" "$BARE/pipx"

# make_git <dir> <version-output> — a pane-side git that only answers --version.
make_git() {
    mkdir -p "$1"
    printf '#!%s\n[[ "${1:-}" == "--version" ]] && echo "%s"\nexit 0\n' "$REAL_BASH" "$2" > "$1/git"
    chmod +x "$1/git"
}

# A fake Git Bash: runs the probe script (its last argument) under the real
# bash with PATH limited to the pane fixture's bin dir, the way a pane's
# login shell sees Git for Windows first.
make_git_bash() {  # make_git_bash <path> <pane-bin>
    mkdir -p "$(dirname "$1")"
    cat > "$1" <<EOF
#!$REAL_BASH
script="\${@: -1}"
PATH="$2" exec "$REAL_BASH" --norc --noprofile -c "\$script"
EOF
    chmod +x "$1"
}

# run_case <name> [--no-overlap] [--floor X.Y] [--bare] [VAR=val...] -- <setup args...>
run_case() {
    local name="$1"; shift
    local sandbox="$TMPROOT/$name" copy_overlap=1 floor="" path="$STUB:$PATH" envs=()
    while [[ $# -gt 0 && "$1" != "--" ]]; do
        case "$1" in
            --no-overlap) copy_overlap=0 ;;
            --bare) path="$BARE"; envs+=("FLEET_SETUP_PYTHON=$PYTHON_ABS") ;;
            --floor) floor="$2"; shift ;;
            *) envs+=("$1") ;;
        esac
        shift
    done
    shift
    mkdir -p "$sandbox/scripts" "$sandbox/home"
    cp "$SETUP_SRC" "$sandbox/scripts/setup-windows.sh"
    if (( copy_overlap )); then
        cp "$OVERLAP_SRC" "$sandbox/scripts/fleet-pr-overlap"
        cp "$SCRIPT_DIR/../fleet_branch_match.py" "$sandbox/scripts/fleet_branch_match.py"
        cp "$SCRIPT_DIR/../fleet_github.py" "$sandbox/scripts/fleet_github.py"
        # perl, not `sed -i`: BSD sed takes the expression as a backup suffix
        # and leaves the floor unchanged, so the floor cases pass vacuously.
        [[ -n "$floor" ]] && FLOOR="$floor" perl -pi -e \
            's/^MERGE_TREE_MIN_VERSION = .*/MERGE_TREE_MIN_VERSION = "$ENV{FLOOR}"/' \
            "$sandbox/scripts/fleet-pr-overlap"
    fi
    [[ -f "$TMPROOT/bashrc.$name" ]] && cp "$TMPROOT/bashrc.$name" "$sandbox/home/.bashrc"
    : > "$MSYS_GIT_LOG"
    : > "$INSTALL_LOG"
    OUT="$(env PATH="$path" HOME="$sandbox/home" FLEET_CLONE="$sandbox/clone" \
        FLEET_GIT_BASH="$sandbox/git-bash/bin/bash.exe" ${envs[@]+"${envs[@]}"} \
        "$REAL_BASH" "$sandbox/scripts/setup-windows.sh" "$@" 2>&1 | tr -d '\r')"
    RC=$?
    SANDBOX="$sandbox"
}

assert_no_mutation() {  # assert_no_mutation <label> <bashrc-before-or-empty>
    local label="$1" before="$2" after=""
    [[ -f "$SANDBOX/home/.bashrc" ]] && after="$(cat "$SANDBOX/home/.bashrc")"
    assert_eq "$after" "$before" "$label: ~/.bashrc untouched"
    [[ ! -e "$SANDBOX/home/.fleet" && ! -e "$SANDBOX/home/.config" ]] \
        && ok "$label: no fleet-up.conf / host.toml written" || bad "$label: config written"
    [[ ! -e "$SANDBOX/clone" ]] && ok "$label: no clone created" || bad "$label: clone dir created"
    assert_absent "$(cat "$MSYS_GIT_LOG")" "clone" "$label: no git clone"
    assert_absent "$(cat "$MSYS_GIT_LOG")" "fetch" "$label: no git fetch"
}

PANE_NEW="$TMPROOT/pane-new"   # what Git Bash finds before ~/.bashrc
PANE_OLD="$TMPROOT/pane-old"   # what ~/.bashrc puts first
make_git "$PANE_NEW" "git version 2.46.0.windows.1"
make_git "$PANE_OLD" "git version 2.34.1.windows.1"
OLD_RC="export PATH=\"$PANE_OLD:\$PATH\""

echo "1. the incident host: MSYS2 git new, pane git 2.34.1.windows.1 via ~/.bashrc"
printf '%s\n' "$OLD_RC" > "$TMPROOT/bashrc.too_old"
make_git_bash "$TMPROOT/too_old/git-bash/bin/bash.exe" "$PANE_NEW"
run_case too_old --
[[ "$RC" != "0" ]] && ok "setup exits non-zero" || bad "setup accepted the incident host (rc=0)"
assert_contains "$OUT" "$PANE_OLD/git" "names the pane git binary"
assert_contains "$OUT" "2.34.1.windows.1" "names the installed pane version"
assert_contains "$OUT" ">= 2.38" "names the floor"
assert_contains "$OUT" "Update Git for Windows" "remedy: update Git for Windows"
assert_absent "$OUT" "2.46.0" "does not grade the MSYS2 git"
assert_no_mutation "too_old" "$OLD_RC"

echo "2. pane git at the floor passes --check without writing"
make_git "$TMPROOT/pane-floor" "git version 2.38.0.windows.1"
printf '%s\n' "export PATH=\"$TMPROOT/pane-floor:\$PATH\"" > "$TMPROOT/bashrc.at_floor"
make_git_bash "$TMPROOT/at_floor/git-bash/bin/bash.exe" "$PANE_NEW"
run_case at_floor -- --check
assert_eq "$RC" "0" "setup --check exits 0"
assert_contains "$OUT" "ok: pane git git version 2.38.0.windows.1" "reports the pane git it graded"
assert_contains "$OUT" "nothing written" "--check says it wrote nothing"
assert_no_mutation "at_floor" "export PATH=\"$TMPROOT/pane-floor:\$PATH\""

echo "3. distinct prerequisite diagnostics"
make_git_bash "$TMPROOT/no_python/git-bash/bin/bash.exe" "$PANE_NEW"
run_case no_python FLEET_SETUP_PYTHON=ir-no-such-python -- --check
[[ "$RC" != "0" ]] && ok "missing python: non-zero" || bad "missing python: rc=0"
assert_contains "$OUT" "MISSING: ir-no-such-python" "missing python is named"
assert_no_mutation "no_python" ""

make_git_bash "$TMPROOT/no_overlap/git-bash/bin/bash.exe" "$PANE_NEW"
run_case no_overlap --no-overlap -- --check
[[ "$RC" != "0" ]] && ok "missing overlap script: non-zero" || bad "missing overlap script: rc=0"
assert_contains "$OUT" "fleet-pr-overlap — run setup-windows.sh from a full engine checkout" \
    "missing overlap script is named"

run_case no_git_bash --
[[ "$RC" != "0" ]] && ok "missing Git Bash: non-zero" || bad "missing Git Bash: rc=0"
assert_contains "$OUT" "MISSING: Git Bash at" "missing Git Bash is named"
assert_no_mutation "no_git_bash" ""

mkdir -p "$TMPROOT/pane-empty"
make_git_bash "$TMPROOT/no_pane_git/git-bash/bin/bash.exe" "$TMPROOT/pane-empty"
run_case no_pane_git --
[[ "$RC" != "0" ]] && ok "missing pane git: non-zero" || bad "missing pane git: rc=0"
assert_contains "$OUT" "MISSING: git on the Git Bash PATH" "missing pane git is named"
assert_no_mutation "no_pane_git" ""

make_git "$TMPROOT/pane-garbled" "not a version string"
make_git_bash "$TMPROOT/unreadable/git-bash/bin/bash.exe" "$TMPROOT/pane-garbled"
run_case unreadable --
[[ "$RC" != "0" ]] && ok "unreadable version: non-zero" || bad "unreadable version: rc=0"
assert_contains "$OUT" "could not read the pane git version" "unreadable version is its own diagnostic"
assert_contains "$OUT" "not a version string" "unreadable diagnostic quotes the reading"
assert_absent "$OUT" "an old git" "unreadable is not reported as too old"
assert_no_mutation "unreadable" ""

echo "4. the floor has one owner"
assert_eq "$(grep -c '2\.38' "$SETUP_SRC")" "0" "setup-windows.sh carries no copy of the floor"
printf '%s\n' "$OLD_RC" > "$TMPROOT/bashrc.lowered_floor"
make_git_bash "$TMPROOT/lowered_floor/git-bash/bin/bash.exe" "$PANE_NEW"
run_case lowered_floor --floor 2.30 -- --check
assert_eq "$RC" "0" "lowering fleet-pr-overlap's floor admits 2.34.1 — setup reads the floor from it"
make_git_bash "$TMPROOT/raised_floor/git-bash/bin/bash.exe" "$PANE_NEW"
printf '%s\n' "export PATH=\"$TMPROOT/pane-floor:\$PATH\"" > "$TMPROOT/bashrc.raised_floor"
run_case raised_floor --floor 2.40 -- --check
[[ "$RC" != "0" ]] && ok "raising the floor rejects 2.38.0 — same single owner" || bad "raised floor ignored"
assert_contains "$OUT" ">= 2.40" "the printed floor follows fleet-pr-overlap"

echo "5. soft dependencies install only after every check, never under --check"
printf '%s\n' "$OLD_RC" > "$TMPROOT/bashrc.soft_too_old"
make_git_bash "$TMPROOT/soft_too_old/git-bash/bin/bash.exe" "$PANE_NEW"
run_case soft_too_old --bare --
[[ "$RC" != "0" ]] && ok "missing soft tools + old pane git: non-zero" || bad "old pane git accepted (rc=0)"
assert_contains "$OUT" "missing (soft): ruff" "missing ruff is reported"
assert_contains "$OUT" "missing (soft): clang-format" "missing clang-format is reported"
assert_eq "$(cat "$INSTALL_LOG")" "" "rejected host: neither pacman nor pipx ran"
assert_no_mutation "soft_too_old" "$OLD_RC"

make_git_bash "$TMPROOT/soft_check/git-bash/bin/bash.exe" "$PANE_NEW"
run_case soft_check --bare -- --check
assert_eq "$RC" "0" "missing soft tools: --check exits 0"
assert_contains "$OUT" "nothing written" "--check says it wrote nothing"
assert_eq "$(cat "$INSTALL_LOG")" "" "--check: neither pacman nor pipx ran"
assert_no_mutation "soft_check" ""

make_git_bash "$TMPROOT/soft_install/git-bash/bin/bash.exe" "$PANE_NEW"
run_case soft_install --bare FLEET_CPU_BUDGET=8 FLEET_ROLES=pool-1 --
assert_eq "$RC" "0" "full run with missing soft tools exits 0"
assert_contains "$(cat "$INSTALL_LOG")" "pacman -S --needed --noconfirm mingw-w64-x86_64-ruff" \
    "full run installs ruff via pacman"
assert_contains "$(cat "$INSTALL_LOG")" "mingw-w64-x86_64-clang-tools-extra" \
    "full run installs clang-format via pacman"
assert_absent "$(cat "$INSTALL_LOG")" "pipx" "pacman success: no pipx fallback"
gate_line="$(printf '%s\n' "$OUT" | grep -n 'ok: pane git' | cut -d: -f1)"
install_line="$(printf '%s\n' "$OUT" | grep -n 'installed ruff via pacman' | cut -d: -f1)"
[[ -n "$gate_line" && -n "$install_line" ]] && (( gate_line < install_line )) \
    && ok "the install follows the pane-git gate" \
    || bad "install not after the gate (gate line '$gate_line', install line '$install_line')"

make_git_bash "$TMPROOT/soft_pipx/git-bash/bin/bash.exe" "$PANE_NEW"
run_case soft_pipx --bare STUB_PACMAN_RC=1 FLEET_CPU_BUDGET=8 FLEET_ROLES=pool-1 --
assert_eq "$RC" "0" "pacman failing: full run still exits 0"
assert_contains "$(cat "$INSTALL_LOG")" "pipx install ruff" "pacman failing: ruff falls back to pipx"
assert_contains "$OUT" "installed ruff via pipx" "pipx install is reported"
assert_contains "$OUT" "WARN: clang-format not installed" "clang-format has no fallback: warns"

echo "6. usage"
run_case bad_arg -- --bogus
assert_eq "$RC" "2" "an unknown argument is a usage error"

summarize "setup-windows pane-git prerequisite tests"
