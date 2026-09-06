#!/usr/bin/env bash
# Tests for fleet-common.sh's dispatch-target grammar: fleet_parse_target,
# fleet_repo_ns, the FLEET_TARGET_CLAIM / FLEET_TARGET_RELEASE tables, and
# fleet_release_assignment. This is the ONE definition of what a well-formed
# `<kind>:<repo>:<N>[:<extra>]` is — fleet-dispatcher (claim), fleet-dispatch-wrap
# (export), and fleet_task_class.py (produce) all read it — so a drift here
# would strand a claim the dispatcher took for a target the wrap then refuses.
#
# Hermetic: fleet-claim is stubbed on PATH and every call logged.

set -uo pipefail

SCRIPT_DIR=$(cd "$(dirname "$0")/.." && pwd)
# shellcheck source=scripts/fleet/tests/lib_assert.sh
source "$(dirname "$0")/lib_assert.sh"

COMMON="$SCRIPT_DIR/fleet-common.sh"
if [[ ! -f "$COMMON" ]]; then
    echo "SKIP: missing $COMMON" >&2
    exit 3
fi
# shellcheck source=/dev/null
source "$COMMON"
if ! declare -F fleet_parse_target >/dev/null; then
    bad "fleet-common.sh defines no fleet_parse_target (pre-target tree)"
    summarize "fleet-common dispatch-target tests"
fi

TMPROOT=$(mktemp -d)
cleanup() { rm -rf "$TMPROOT"; }
trap cleanup EXIT
export FLEET_CLAIM_LOG="$TMPROOT/fleet-claim.log"
mkdir -p "$TMPROOT/bin"
cat > "$TMPROOT/bin/fleet-claim" <<'EOF'
#!/usr/bin/env bash
printf '%s\n' "$*" >> "$FLEET_CLAIM_LOG"
exit 0
EOF
chmod +x "$TMPROOT/bin/fleet-claim"
export PATH="$TMPROOT/bin:$PATH"

parse() { fleet_parse_target "$1" && printf '%s|%s|%s|%s\n' \
    "$FLEET_TARGET_KIND" "$FLEET_TARGET_REPO" "$FLEET_TARGET_NUM" "$FLEET_TARGET_EXTRA"; }

echo "T1: well-formed targets parse into their parts"
assert_eq "$(parse task:engine:1969)" "task|engine|1969|" "task target"
assert_eq "$(parse stack:game:344:397)" "stack|game|344|397" "stack target carries its base PR as extra"
assert_eq "$(parse review:engine:3074)" "review|engine|3074|" "review target"
assert_eq "$(parse planreview:game:605)" "planreview|game|605|" "planreview target"

echo "T2: every kind in the claim table parses, and the two tables agree"
for kind in "${!FLEET_TARGET_CLAIM[@]}"; do
    fleet_parse_target "$kind:engine:1" \
        && ok "kind '$kind' parses" || bad "kind '$kind' refused by the parser"
    [[ -n "${FLEET_TARGET_RELEASE[$kind]+x}" ]] \
        && ok "kind '$kind' has a release arm" || bad "kind '$kind' has no release arm"
done
assert_eq "${#FLEET_TARGET_CLAIM[@]}" "${#FLEET_TARGET_RELEASE[@]}" "claim and release tables are the same size"

echo "T3: malformed targets are refused with every part left empty"
for bad_target in "" "bogus:engine:1" "task:other:1" "task:engine:abc" "task:engine:" "task" ":engine:1" "task::1"; do
    if fleet_parse_target "$bad_target"; then
        bad "'$bad_target' accepted"
    else
        [[ -z "$FLEET_TARGET_KIND$FLEET_TARGET_REPO$FLEET_TARGET_NUM$FLEET_TARGET_EXTRA" ]] \
            && ok "'$bad_target' refused, parts cleared" || bad "'$bad_target' refused but parts leaked"
    fi
done

echo "T4: fleet_repo_ns yields the global --repo flag for game only"
fleet_repo_ns game
assert_eq "${FLEET_NS[*]}" "--repo game" "game -> --repo game"
fleet_repo_ns engine
assert_eq "${#FLEET_NS[@]}" "0" "engine -> no flag"
fleet_repo_ns ""
assert_eq "${#FLEET_NS[@]}" "0" "empty -> no flag"

echo "T5: fleet_release_assignment routes each kind to its release arm"
release() { : > "$FLEET_CLAIM_LOG"; fleet_release_assignment "$1" pool-3; cat "$FLEET_CLAIM_LOG"; }
assert_eq "$(release task:engine:10)" "release 10" "task -> plain release (drops lock, label, reservation)"
assert_eq "$(release stack:game:344:397)" "--repo game release 344" "stack -> plain release, game namespaced"
assert_eq "$(release feedback:engine:50)" "amending-release 50 pool-3" "feedback -> amending-release under the agent"
assert_eq "$(release conflict:engine:2417)" "resolving-release 2417 pool-3" "conflict -> resolving-release"
assert_eq "$(release plan:game:7)" "--repo game planning-release 7 pool-3" "plan -> planning-release (#2197)"
assert_eq "$(release review:engine:3074)" "review-release 3074 pool-3" "review -> review-release"
assert_eq "$(release planreview:engine:605)" "review-release 605 pool-3" "planreview -> review-release"
assert_eq "$(release smoke:engine:3087)" "review-release 3087 pool-3" "smoke -> review-release"
assert_eq "$(release bogus:engine:1)" "" "a malformed target releases nothing"

summarize "fleet-common dispatch-target tests"
