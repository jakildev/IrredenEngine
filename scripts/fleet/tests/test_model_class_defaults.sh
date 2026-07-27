#!/usr/bin/env bash
# Tests for fleet-common.sh's model-class defaults and fleet_model_tag.
#
# The class defaults are version-free CLI tier aliases so a new model
# release needs no fleet edit; these tests pin the two properties that
# make that safe: (1) the defaults stay alias-shaped (a full claude-* id
# creeping back in reintroduces the per-release bump), and (2) the label
# derivation handles both alias and full-id shapes without a per-version
# map — including ids that don't exist yet.

set -euo pipefail

SCRIPT_DIR=$(cd "$(dirname "$0")/.." && pwd)
# shellcheck source=/dev/null
source "$SCRIPT_DIR/tests/lib_assert.sh"
# shellcheck source=/dev/null
source "$SCRIPT_DIR/fleet-common.sh"

echo "T1: class defaults are version-free aliases"
assert_eq "$FLEET_OPUS_CLASS_DEFAULT"   "opus[1m]" "opus class default is the opus[1m] alias"
assert_eq "$FLEET_SONNET_CLASS_DEFAULT" "sonnet"   "sonnet class default is the bare sonnet alias"
assert_eq "${FLEET_FABLE_CANDIDATES_DEFAULT[0]}" "fable[1m]" "fable ladder rung 0 is fable[1m]"
assert_eq "${FLEET_FABLE_CANDIDATES_DEFAULT[${#FLEET_FABLE_CANDIDATES_DEFAULT[@]}-1]}" \
    "opus[1m]" "fable ladder floor is the opus alias"
for _m in "$FLEET_OPUS_CLASS_DEFAULT" "$FLEET_SONNET_CLASS_DEFAULT" \
    "${FLEET_FABLE_CANDIDATES_DEFAULT[@]}"; do
    case "$_m" in
        claude-*) bad "default '$_m' is a pinned full id, not an alias" ;;
        *) ok "default '$_m' is alias-shaped" ;;
    esac
done

echo "T2: fleet_model_tag derives labels for full ids"
assert_eq "$(fleet_model_tag 'claude-opus-4-8[1m]')" "opus 4.8 1m" "claude-opus-4-8[1m]"
assert_eq "$(fleet_model_tag 'claude-fable-5[1m]')"  "fable 5 1m"  "claude-fable-5[1m]"
assert_eq "$(fleet_model_tag 'claude-sonnet-5')"     "sonnet 5"    "claude-sonnet-5"
# A release that postdates this test must still label cleanly.
assert_eq "$(fleet_model_tag 'claude-opus-5[1m]')"   "opus 5 1m"   "claude-opus-5[1m] (no map entry needed)"
assert_eq "$(fleet_model_tag 'claude-opus-6-2[1m]')" "opus 6.2 1m" "claude-opus-6-2[1m] (future id)"

echo "T3: fleet_model_tag passes aliases through readably"
assert_eq "$(fleet_model_tag 'opus[1m]')"  "opus 1m"  "opus[1m] alias"
assert_eq "$(fleet_model_tag 'fable[1m]')" "fable 1m" "fable[1m] alias"
assert_eq "$(fleet_model_tag 'sonnet')"    "sonnet"   "bare sonnet alias"

summarize "model-class defaults + fleet_model_tag"
