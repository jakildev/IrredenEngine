#!/usr/bin/env bash
# Tests for fleet-gh-shim — the ~/bin/gh shim that refreshes an inherited
# GitHub App token per call and execs the real gh.
#
# Both neighbours are PATH stubs: a fake real `gh` that prints the GH_TOKEN it
# received, its argv and exits with a chosen code, and a fake `fleet-gh-token`
# that prints a fresh token (or nothing, the unconfigured path). The shim is
# installed into a scratch bin as `gh`, ahead of the fake, exactly as
# install.sh places it ahead of the package manager's gh.
#
# Covers: a stale App token is replaced; a personal gho_/ghp_ token and an
# unset GH_TOKEN pass through untouched; an unconfigured helper keeps the
# inherited token; FLEET_GH_SHIM=0 passthrough; argv and exit code
# passthrough; the shim never execs itself when it is the first gh on PATH.
set -euo pipefail
SCRIPT_DIR=$(cd "$(dirname "$0")/.." && pwd)
source "$(dirname "$0")/lib_preflight.sh"
SHIM="$SCRIPT_DIR/fleet-gh-shim"
[[ -x "$SHIM" ]] || { echo "test setup: fleet-gh-shim not executable at $SHIM" >&2; exit 1; }
source "$(dirname "$0")/lib_assert.sh"
TMPROOT=$(mktemp -d)
cleanup() { [[ -n "$TMPROOT" && -d "$TMPROOT" ]] && rm -rf "$TMPROOT"; }
trap cleanup EXIT

mkdir -p "$TMPROOT/shim" "$TMPROOT/real" "$TMPROOT/helper"
ln -s "$SHIM" "$TMPROOT/shim/gh"
cat > "$TMPROOT/real/gh" <<'SH'
#!/usr/bin/env bash
printf 'token=%s argc=%s args=%s\n' "${GH_TOKEN-<unset>}" "$#" "$*"
exit "${FAKE_GH_EXIT:-0}"
SH
cat > "$TMPROOT/helper/fleet-gh-token" <<'SH'
#!/usr/bin/env bash
[[ "${HELPER_EMPTY:-0}" == "1" ]] && exit 0
printf 'ghs_fresh\n'
SH
chmod +x "$TMPROOT/real/gh" "$TMPROOT/helper/fleet-gh-token"
export PATH="$TMPROOT/shim:$TMPROOT/real:$TMPROOT/helper:$PATH"

run_shim() { gh "$@"; }

out=$(GH_TOKEN=ghs_stale run_shim pr view 1)
[[ "$out" == "token=ghs_fresh argc=3 args=pr view 1" ]] \
    && ok "T1: a stale App token is replaced by the helper's fresh one, argv intact" \
    || bad "T1: stale App token replaced (got: $out)"

out=$(GH_TOKEN=gho_personal run_shim auth status)
[[ "$out" == "token=gho_personal argc=2 args=auth status" ]] \
    && ok "T2: a personal gho_ token passes through untouched" \
    || bad "T2: personal token untouched (got: $out)"

out=$(GH_TOKEN=ghp_classic run_shim x)
[[ "$out" == token=ghp_classic* ]] \
    && ok "T3: a classic ghp_ token passes through untouched" \
    || bad "T3: classic token untouched (got: $out)"

out=$(env -u GH_TOKEN bash -c 'gh x')
[[ "$out" == "token=<unset> argc=1 args=x" ]] \
    && ok "T4: no GH_TOKEN stays unset (keychain identity untouched)" \
    || bad "T4: unset stays unset (got: $out)"

out=$(HELPER_EMPTY=1 GH_TOKEN=ghs_stale run_shim x)
[[ "$out" == token=ghs_stale* ]] \
    && ok "T5: an unconfigured helper keeps the inherited token" \
    || bad "T5: unconfigured helper keeps inherited token (got: $out)"

out=$(FLEET_GH_SHIM=0 GH_TOKEN=ghs_stale run_shim x)
[[ "$out" == token=ghs_stale* ]] \
    && ok "T6: FLEET_GH_SHIM=0 is a plain passthrough" \
    || bad "T6: FLEET_GH_SHIM=0 passthrough (got: $out)"

out=$(GH_TOKEN=ghs_stale run_shim pr comment 7 --body "two words" --repo o/r)
[[ "$out" == "token=ghs_fresh argc=7 args=pr comment 7 --body two words --repo o/r" ]] \
    && ok "T7: an argument with a space arrives as one argv element" \
    || bad "T7: argv passthrough (got: $out)"

set +e
FAKE_GH_EXIT=3 GH_TOKEN=ghs_stale run_shim x >/dev/null
rc=$?
set -e
[[ "$rc" == "3" ]] && ok "T8: the real gh's exit code is the shim's" || bad "T8: exit code passthrough (got $rc)"

# The shim is first on PATH under the real name; it must skip itself and
# never recurse. A second copy of the shim ahead of it must be skipped too.
mkdir -p "$TMPROOT/shim2"; ln -s "$SHIM" "$TMPROOT/shim2/gh"
out=$(PATH="$TMPROOT/shim2:$PATH" GH_TOKEN=ghs_stale gh ok)
[[ "$out" == token=ghs_fresh* ]] \
    && ok "T9: two shim entries on PATH both resolve past themselves to the real gh" \
    || bad "T9: no self-exec (got: $out)"

set +e
out=$(PATH="$TMPROOT/shim:$TMPROOT/helper:/usr/bin:/bin" GH_TOKEN=ghs_stale gh x 2>&1)
rc=$?
set -e
[[ "$rc" == "127" && "$out" == *"no other gh on PATH"* ]] \
    && ok "T10: no real gh on PATH is a 127 with a message, not a loop" \
    || bad "T10: missing real gh (rc=$rc out=$out)"

summarize "fleet-gh-shim tests"
