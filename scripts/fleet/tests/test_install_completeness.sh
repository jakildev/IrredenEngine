#!/usr/bin/env bash
# scripts/fleet/tests/test_install_completeness.sh — install.sh tool-registry
# completeness (#2826).
#
# install.sh:144-157 documents a three-step contract per tool (a `_SRC`/`_DEST`
# var pair, a chmod-loop entry, and an `ln -sf` install block) but presupposes
# an unstated step 0: the tool has a `_SRC` var at all. A tool with none
# satisfies steps 1-3 vacuously and is never symlinked into `~/bin` on any
# host (`review-fleet-feedback` and `fleet-queue-backfill-model-labels` sat
# in that state for ~70 days). This suite checks all four steps against
# install.sh as text — it never executes install.sh.
#
# Enumerates tracked files via `git ls-files` rather than a filesystem walk —
# a pool worktree nests other checkouts and a find-style walk picks them up
# (#2791). That enumeration needs a git working tree, which `fleet-positive-control`'s
# git-archive staging deliberately does not provide; falling back to a
# `find -maxdepth 1` scoped to scripts/fleet/ itself is safe there because the
# walk never descends into a subdirectory, so it cannot cross into a nested
# checkout (the thing #2791 actually guards against).

set -euo pipefail

SCRIPT_DIR=$(cd "$(dirname "$0")/.." && pwd)
REPO_ROOT=$(cd "$SCRIPT_DIR/../.." && pwd)
INSTALL_SH="$SCRIPT_DIR/install.sh"

source "$(dirname "$0")/lib_assert.sh"

if [[ ! -f "$INSTALL_SH" ]]; then
    echo "SKIP: $INSTALL_SH not found" >&2
    exit 3  # skip status — a missing subject must not score as a pass (#2786)
fi

# Allowlist: tracked top-level executables intentionally absent from the
# _SRC registry — each invoked by path, not by name, so the registration
# contract does not apply to them.
ALLOWLIST=(
    "install.sh"                  # bootstrap, run by path
    "classify-auto-rereview.sh"   # invoked by path from .github/workflows/auto-rereview.yml
    "fleet-guard-worktree-edit"   # PreToolUse hook, invoked by absolute path from
                                   # .claude/settings.json and fleet-up
)

is_allowlisted() {
    local name="$1" a
    for a in "${ALLOWLIST[@]}"; do
        [[ "$name" == "$a" ]] && return 0
    done
    return 1
}

# tracked_top_level <fleet-dir-relative-to-repo-root> — top-level (no
# subdirectory) executable files under <dir>, one basename per line.
tracked_top_level() {
    local dir="$1"
    if git -C "$REPO_ROOT" rev-parse --is-inside-work-tree >/dev/null 2>&1; then
        git -C "$REPO_ROOT" ls-files -s -- "$dir" \
            | awk '$1 == "100755" {print $4}' \
            | while IFS= read -r p; do
                rel="${p#"$dir"/}"
                [[ "$rel" == */* ]] && continue
                echo "$rel"
            done
    else
        # No .git here (fleet-positive-control's staged tree) — find is safe
        # because it never recurses past $dir itself.
        find "$REPO_ROOT/$dir" -maxdepth 1 -type f -perm -u+x \
            | while IFS= read -r p; do basename "$p"; done
    fi
}

# declared_src_basenames <install_sh> [--top-level-only] — the basename from
# every `<NAME>_SRC="$SCRIPT_DIR/<path>"` declaration. With --top-level-only,
# drops any pointing into a subdirectory (e.g. completions/*).
declared_src_basenames() {
    local install="$1" top_only="${2:-}"
    grep -oE '_SRC="\$SCRIPT_DIR/[^"]+"' "$install" \
        | sed -E 's#.*SCRIPT_DIR/##; s#"$##' \
        | while IFS= read -r p; do
            if [[ "$top_only" == "--top-level-only" && "$p" == */* ]]; then
                continue
            fi
            basename "$p"
        done
}

# declared_src_varnames <install_sh> — the <NAME> prefixes (without _SRC) of
# every `<NAME>_SRC="$SCRIPT_DIR/<path>"` declaration, one per line.
declared_src_varnames() {
    grep -oE '^[A-Za-z_]+_SRC="\$SCRIPT_DIR/[^"]+"' "$1" | sed -E 's/_SRC=.*//'
}

# step0_missing <install_sh> <fleet-dir> — tracked top-level tools with no
# _SRC var (and not allowlisted), one per line.
step0_missing() {
    local install="$1" dir="$2"
    comm -23 \
        <(tracked_top_level "$dir" | sort -u) \
        <(declared_src_basenames "$install" --top-level-only | sort -u) \
        | while IFS= read -r name; do
            is_allowlisted "$name" || echo "$name"
        done
}

# steps123_missing_chmod <install_sh> — declared _SRC vars (excluding
# completions/) absent from the chmod loop, one var name per line.
steps123_missing_chmod() {
    local install="$1" chmod_line needle
    chmod_line=$(grep -m1 '^for src in ' "$install") || chmod_line=""
    while IFS= read -r var; do
        [[ -z "$var" ]] && continue
        if grep -qE "^${var}_SRC=\"\\\$SCRIPT_DIR/completions/" "$install"; then
            continue  # sourced, not executed — exempt from chmod
        fi
        needle="\"\$${var}_SRC\""
        [[ "$chmod_line" == *"$needle"* ]] || echo "$var"
    done < <(declared_src_varnames "$install")
}

# steps123_missing_install <install_sh> — declared _SRC vars with no
# `ln -sf "$<VAR>_SRC"` occurrence anywhere in the file, one var name per line.
steps123_missing_install() {
    local install="$1"
    while IFS= read -r var; do
        [[ -z "$var" ]] && continue
        grep -qF "ln -sf \"\$${var}_SRC\"" "$install" || echo "$var"
    done < <(declared_src_varnames "$install")
}

# --- T1: step 0 — every tracked top-level tool has a _SRC var --------------
echo "T1: step 0 — registry completeness against the live tree"
missing0=$(step0_missing "$INSTALL_SH" scripts/fleet)
if [[ -z "$missing0" ]]; then
    ok "no un-registered top-level scripts/fleet tool"
else
    bad "un-registered tool(s): $(echo "$missing0" | tr '\n' ' ')"
fi

# --- T2: steps 1-3 — every declared _SRC var reaches the chmod loop --------
echo "T2: steps 1-3 — every declared _SRC var is in the chmod loop"
missing_chmod=$(steps123_missing_chmod "$INSTALL_SH")
if [[ -z "$missing_chmod" ]]; then
    ok "every declared _SRC var is chmod'd (or completions-exempt)"
else
    bad "_SRC var(s) missing from chmod loop: $(echo "$missing_chmod" | tr '\n' ' ')"
fi

# --- T3: steps 1-3 — every declared _SRC var has an ln -sf install block ---
echo "T3: steps 1-3 — every declared _SRC var has an ln -sf block"
missing_install=$(steps123_missing_install "$INSTALL_SH")
if [[ -z "$missing_install" ]]; then
    ok "every declared _SRC var has an install (ln -sf) block"
else
    bad "_SRC var(s) missing an install block: $(echo "$missing_install" | tr '\n' ' ')"
fi

# --- T4: negative fixture — a phase step naming an unrelated tool must not
# false-fire step 0 (sanity: the allowlist doesn't over-match) -------------
echo "T4: allowlisted tools do not appear in step-0 output"
assert_absent "$missing0" "install.sh" "install.sh itself never reported as un-registered"
assert_absent "$missing0" "fleet-guard-worktree-edit" "fleet-guard-worktree-edit never reported as un-registered"

# --- Negative controls: mutate a scratch copy, confirm the check fires -----
TMPROOT=$(mktemp -d)
cleanup() { rm -rf "$TMPROOT"; }
trap cleanup EXIT
SCRATCH="$TMPROOT/install.sh"

echo "T5: negative control — removing review-fleet-feedback's _SRC var"
grep -v '^REVIEW_FLEET_FEEDBACK_SRC=' "$INSTALL_SH" > "$SCRATCH"
miss=$(step0_missing "$SCRATCH" scripts/fleet)
assert_contains "$miss" "review-fleet-feedback" \
    "step-0 check fires when review-fleet-feedback's _SRC var is deleted"

echo "T6: negative control — removing fleet-heartbeat's ln -sf install block"
awk '
    /^if \[\[ -f "\$FLEET_HEARTBEAT_SRC" \]\]; then/ { skip=1 }
    skip && /^fi$/ { skip=0; next }
    !skip { print }
' "$INSTALL_SH" > "$SCRATCH"
miss=$(steps123_missing_install "$SCRATCH")
assert_contains "$miss" "FLEET_HEARTBEAT" \
    "install-block check fires when fleet-heartbeat's ln -sf block is deleted"

summarize "install.sh registry completeness"
