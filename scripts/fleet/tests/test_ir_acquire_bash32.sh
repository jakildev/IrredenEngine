#!/usr/bin/env bash
# engine/tools/bin/ir-acquire (and the tools around it) must survive macOS's
# stock /bin/bash 3.2, where expanding an empty array as "${arr[@]}" under
# `set -u` is an "unbound variable" abort (bash 4.4+ allows it). The
# `#!/usr/bin/env bash` shebang picks Homebrew bash in an interactive shell,
# so only launchd, cron, minimal-profile ssh and `bash -lc` wrappers reach
# 3.2 — and there every `fleet-run` reads LOCK-FAILED.
#
# Phase 1 runs the tool under /bin/bash and needs a 3.x /bin/bash to mean
# anything; elsewhere it prints a note and skips. Phases 2 and 3 are static
# checks that run on every host, so a Linux CI run still catches a
# reintroduced bare expansion: phase 2 of the arrays that are empty on the
# no-flag path, phase 3 of any array another site in the same file guards.

set -uo pipefail

SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)
REPO_ROOT=$(cd "$SCRIPT_DIR/../../.." && pwd)
ACQUIRE="$REPO_ROOT/engine/tools/bin/ir-acquire"
BUILD="$REPO_ROOT/engine/tools/bin/ir-build"
HELPERS="$REPO_ROOT/engine/tools/lib/concurrency_helpers.sh"

# shellcheck source=lib_assert.sh
source "$SCRIPT_DIR/lib_assert.sh"

if [[ ! -x "$ACQUIRE" ]]; then
    echo "SKIP: ir-acquire not found at $ACQUIRE" >&2
    exit 3
fi

LOCK_ROOT=$(mktemp -d "${TMPDIR:-/tmp}/ir-acquire-bash32.XXXXXX")
trap 'rm -rf "$LOCK_ROOT"' EXIT

echo "Phase 1: ir-acquire under /bin/bash 3.x"
sys_major=""
if [[ -x /bin/bash ]]; then
    sys_major=$(/bin/bash -c 'echo "${BASH_VERSINFO[0]}"' 2>/dev/null || true)
fi
if [[ "$sys_major" != "3" ]]; then
    echo "  note: /bin/bash is not bash 3.x (major='${sys_major:-none}'); skipping the 3.2 runtime cases"
else
    run_acquire() {  # run_acquire <label> <ir-acquire args...>
        local label="$1" out rc=0
        shift
        out=$(IR_LOCK_ROOT="$LOCK_ROOT" /bin/bash "$ACQUIRE" "$@" 2>&1) || rc=$?
        assert_eq "$rc" "0" "ir-acquire $label exits 0 under /bin/bash 3.x"
        assert_absent "$out" "unbound variable" "ir-acquire $label: no unbound-variable abort"
    }
    run_acquire "gpu (no flags)"            gpu -- /usr/bin/true
    run_acquire "gpu --nonblock"            --nonblock gpu -- /usr/bin/true
    run_acquire "gpu --timeout 5"           --timeout 5 gpu -- /usr/bin/true
    run_acquire "gpu --nonblock --timeout"  --nonblock --timeout 5 gpu -- /usr/bin/true
    run_acquire "perf (no flags)"           perf -- /usr/bin/true
    run_acquire "cpu 1 (no flags)"          cpu 1 -- /usr/bin/true
fi

echo "Phase 2: no bare expansion of a possibly-empty array"
bare_acquire=$(grep -nE '[^+]"\$\{acquire_args\[@\]\}"' "$ACQUIRE" || true)
assert_eq "$bare_acquire" "" "ir-acquire expands acquire_args only through \${arr[@]+...}"
bare_forward=$(grep -nE '[^+]"\$\{forward_args\[@\]\}"' "$BUILD" || true)
assert_eq "$bare_forward" "" "ir-build expands forward_args only through \${arr[@]+...}"
bare_got=$(grep -nE '[^+]"\$\{got\[@\]\}"' "$HELPERS" || true)
assert_eq "$bare_got" "" "concurrency_helpers expands got only through \${arr[@]+...}"

# An optional `--repo` array is empty on the default-repository path of every
# wrapper that takes one.
OPTIONAL_REPO_ARRAYS=(
    "fleet-transition:repo_args"
    "fleet-review-verdict:repo_args"
    "fleet-pr-checkout-detached:repo_args"
    "fleet-pr-clear-feedback-labels:repo_args"
    "fleet-pr-claim-feedback:claim_repo_args"
    "fleet-pr-claim-feedback:checkout_repo_args"
)
for pair in "${OPTIONAL_REPO_ARRAYS[@]}"; do
    tool="${pair%%:*}"
    arr="${pair##*:}"
    bare=$(grep -nE "[^+]\"\\\$\{${arr}\[@\]\}\"" "$REPO_ROOT/scripts/fleet/$tool" || true)
    assert_eq "$bare" "" "$tool expands $arr only through \${arr[@]+...}"
done

echo "Phase 3: an array guarded at one site is guarded at every site in that file"
# A `${arr[@]+"${arr[@]}"}` expansion marks arr as possibly empty; a bare
# "${arr[@]}" of the same name in the same file aborts on that same empty
# array.
mixed=""
scanned=0
while IFS= read -r f; do
    case "$f" in *.py|*.md|*.json) continue ;; esac
    scanned=$((scanned + 1))
    guarded=$(grep -IoE '\$\{[A-Za-z_][A-Za-z0-9_]*\[@\]\+"' "$f" 2>/dev/null \
        | sed -e 's/^\${//' -e 's/\[@\]+"$//' | sort -u || true)
    [[ -z "$guarded" ]] && continue
    for arr in $guarded; do
        hits=$(grep -InE "(^|[^+])\"\\\$\{${arr}\[@\]\}\"" "$f" | grep -vE '^[0-9]+:[[:space:]]*#' || true)
        [[ -n "$hits" ]] && mixed+="${f#"$REPO_ROOT"/} [$arr] $hits"$'\n'
    done
done < <(find "$REPO_ROOT/engine/tools" "$REPO_ROOT/scripts/fleet" -type f | sort)
if (( scanned == 0 )); then
    bad "phase 3 scanned zero files under engine/tools and scripts/fleet"
else
    assert_eq "$mixed" "" "no array is expanded both guarded and bare in one file ($scanned files scanned)"
fi

summarize "ir-acquire bash 3.2 tests"
