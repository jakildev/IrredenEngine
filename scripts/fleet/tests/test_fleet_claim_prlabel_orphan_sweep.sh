#!/usr/bin/env bash
# Tests for the confirmed-orphan fast path in `fleet-claim cleanup --gh`'s
# PR-label sweep, for fleet:reviewing-* / fleet:resolving-* claims.
#
# These claims drop a local marker ($CLAIMS_DIR/_prlabel-<tag>-<agent>, content =
# the PR number) on acquire and remove it on release. fleet-down wipes
# ~/.fleet/claims, so a same-host reviewing/resolving label with NO matching
# marker is a claim whose owning session died/restarted while the GitHub label
# survived (the #2137/#2138 stuck-reviewer shape) — swept after a short grace
# rather than the 30-min TTL. A marker that matches keeps the claim; cross-host
# labels can't be vouched for locally and stay on the full TTL.
#
# The gh stub reports every label as added STUB_AGE seconds ago, so age alone
# never trips the 1800s TTL — anything swept is the orphan fast path.

set -uo pipefail
SCRIPT_DIR=$(cd "$(dirname "$0")/.." && pwd)
FLEET_CLAIM="$SCRIPT_DIR/fleet-claim"
[[ -x "$FLEET_CLAIM" ]] || { echo "test setup: fleet-claim not found"; exit 1; }

PASS=0; FAIL=0
ok()  { PASS=$((PASS+1)); echo "  ok: $1"; }
bad() { FAIL=$((FAIL+1)); echo "  FAIL: $1"; }
removed_has()    { grep -qF "$1" "$REMOVED_FILE" 2>/dev/null; }
TMPROOT=""; cleanup(){ [[ -n "$TMPROOT" && -d "$TMPROOT" ]] && rm -rf "$TMPROOT"; }
trap cleanup EXIT
TMPROOT=$(mktemp -d)

export FLEET_CLAIMS_DIR="$TMPROOT/claims"
export FLEET_STATE_DIR="$TMPROOT/state"
export FLEET_ORPHANS_DIR="$TMPROOT/orphans"
export FLEET_TEST_HOST="mac"
mkdir -p "$FLEET_CLAIMS_DIR" "$FLEET_STATE_DIR" "$FLEET_ORPHANS_DIR"
export PRS_JSON="$TMPROOT/prs.json"
REMOVED_FILE="$TMPROOT/removed.log"; export REMOVED_FILE

STUB_DIR="$TMPROOT/bin"; mkdir -p "$STUB_DIR"
cat > "$STUB_DIR/gh" <<'GHSTUB'
#!/usr/bin/env bash
case "$1" in
    pr) case "$2" in list) cat "$PRS_JSON"; exit 0 ;; *) exit 0 ;; esac ;;
    issue)
        case "$2" in
            edit) shift 2; n="$1"; shift
                while [[ $# -gt 0 ]]; do
                    case "$1" in --remove-label) printf '%s\t%s\n' "$n" "$2" >> "$REMOVED_FILE"; shift 2 ;; *) shift ;; esac
                done; exit 0 ;;
            *) exit 0 ;;
        esac ;;
    api)
        printf '%s ' "$@" | grep -q 'events' && \
          python3 -c "import time;print(time.strftime('%Y-%m-%dT%H:%M:%SZ', time.gmtime(time.time()-${STUB_AGE:-600})))"
        exit 0 ;;
    *) exit 0 ;;
esac
GHSTUB
chmod +x "$STUB_DIR/gh"
export PATH="$STUB_DIR:$PATH"

# --- Phase 1: aged labels (STUB_AGE=600 > grace 120, < TTL 1800) -----------
: > "$REMOVED_FILE"
# opus-reviewer's marker points at 901 (its live review). sonnet-reviewer and
# merger have NO markers (dead/restarted sessions).
printf '901\n' > "$FLEET_CLAIMS_DIR/_prlabel-reviewing-opus-reviewer"
cat > "$PRS_JSON" <<'JSON'
[
  {"number":900,"labels":[{"name":"fleet:reviewing-mac-opus-reviewer"}]},
  {"number":901,"labels":[{"name":"fleet:reviewing-mac-opus-reviewer"}]},
  {"number":902,"labels":[{"name":"fleet:reviewing-mac-sonnet-reviewer"}]},
  {"number":903,"labels":[{"name":"fleet:resolving-mac-merger"}]},
  {"number":904,"labels":[{"name":"fleet:reviewing-linux-opus-reviewer"}]}
]
JSON
echo "=== aged reviewing/resolving sweep (STUB_AGE=600) ==="
STUB_AGE=600 "$FLEET_CLAIM" cleanup --gh --repo jakildev/IrredenEngine 2>&1 | sed 's/^/    /'
removed_has $'900\tfleet:reviewing-mac-opus-reviewer'   && ok "same-host marker-mismatch (900 vs marker 901) swept" || bad "900 not swept"
removed_has $'901\tfleet:reviewing-mac-opus-reviewer'   && bad "901 (marker matches) wrongly swept"            || ok "same-host live claim (marker matches 901) kept"
removed_has $'902\tfleet:reviewing-mac-sonnet-reviewer' && ok "same-host no-marker orphan (902) swept"          || bad "902 not swept"
removed_has $'903\tfleet:resolving-mac-merger'          && ok "resolving same-host no-marker orphan (903) swept" || bad "903 not swept"
removed_has $'904\tfleet:reviewing-linux-opus-reviewer' && bad "cross-host (904) wrongly swept"                || ok "cross-host claim kept (TTL, can't vouch)"

# --- Phase 2: fresh orphan (STUB_AGE=30 < grace 120) -> kept ---------------
: > "$REMOVED_FILE"
rm -f "$FLEET_CLAIMS_DIR"/_prlabel-*
cat > "$PRS_JSON" <<'JSON'
[ {"number":910,"labels":[{"name":"fleet:reviewing-mac-opus-reviewer"}]} ]
JSON
echo "=== fresh no-marker reviewing label (STUB_AGE=30) ==="
STUB_AGE=30 "$FLEET_CLAIM" cleanup --gh --repo jakildev/IrredenEngine 2>&1 | sed 's/^/    /'
removed_has $'910\tfleet:reviewing-mac-opus-reviewer' && bad "fresh no-marker label swept inside grace" || ok "fresh no-marker label spared by grace (claim/marker race)"

echo
echo "PASS: $PASS  FAIL: $FAIL"
[[ "$FAIL" -eq 0 ]]
