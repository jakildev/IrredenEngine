#!/usr/bin/env bash
# Tests for fleet-gh-token — mint/cache of a GitHub App installation token.
#
# curl is stubbed via a PATH shim (records its args so the JWT payload can be
# inspected, and emits a canned "<body>\n<code>" response the script parses the
# same way it parses a real curl `-w '\n%{http_code}'` run). openssl runs for
# real against a throwaway RSA key, so the JWT is genuinely signed and the
# payload-shape assertions exercise the real construction path — no live GitHub
# credentials needed.
#
# Covers: unconfigured guarded-empty, cache hit (no mint), cache miss (mint +
# cache write), JWT payload shape (iss + 10-min window), stale-margin boundary
# (re-mint inside / serve outside), and the lock stale-owner cleanup +
# live-owner fallback paths.

set -euo pipefail

SCRIPT_DIR=$(cd "$(dirname "$0")/.." && pwd)
GHT="$SCRIPT_DIR/fleet-gh-token"
[[ -x "$GHT" ]] || { echo "test setup: fleet-gh-token not executable at $GHT" >&2; exit 1; }

source "$(dirname "$0")/lib_assert.sh"

TMPROOT=$(mktemp -d)
cleanup() { [[ -n "$TMPROOT" && -d "$TMPROOT" ]] && rm -rf "$TMPROOT"; }
trap cleanup EXIT
assert_no_path() { [[ -e "$1" ]] && bad "$2" || ok "$2"; }

# --- throwaway RSA signing key + isolated conf/state ---
KEY="$TMPROOT/app-key.pem"
openssl genrsa -out "$KEY" 2048 >/dev/null 2>&1
chmod 600 "$KEY"
EMPTY_CONF="$TMPROOT/nonexistent.conf"   # deliberately absent → never sourced

APP_ID="123456"
INSTALL_ID="7891011"

# curl PATH shim: dump args (for JWT extraction), then emit the canned response.
mkdir -p "$TMPROOT/bin"
cat > "$TMPROOT/bin/curl" <<'SH'
#!/usr/bin/env bash
printf '%s\n' "$@" > "$CURL_STUB_ARGS"
printf '%s\n%s' "$CURL_STUB_BODY" "${CURL_STUB_CODE:-201}"
SH
chmod +x "$TMPROOT/bin/curl"

# Run the script "configured" against an isolated state dir. LOCK_TRIES (shell
# var, inherited by the command-substitution subshell) tunes the retry budget so
# the contention path doesn't burn a real ~10s wait.
LOCK_TRIES=""
run_ght() {
    FLEET_UP_CONF="$EMPTY_CONF" \
    FLEET_STATE_DIR="$1" \
    FLEET_GH_APP_ID="$APP_ID" \
    FLEET_GH_APP_INSTALLATION_ID="$INSTALL_ID" \
    FLEET_GH_APP_KEY_PATH="$KEY" \
    FLEET_GH_TOKEN_LOCK_TRIES="${LOCK_TRIES:-50}" \
    PATH="$TMPROOT/bin:$PATH" \
    "$GHT"
}
write_cache() { # write_cache <state-dir> <token> <seconds-from-now>
    python3 - "$1/gh-app-token.json" "$2" "$3" <<'PY'
import json, sys, time
path, token, delta = sys.argv[1], sys.argv[2], int(sys.argv[3])
json.dump({"token": token, "expires_at": time.time() + delta}, open(path, "w"))
PY
}

echo "fleet-gh-token tests"

# --- Case 1: unconfigured → prints nothing, exit 0 (guarded-empty) ---
sd="$TMPROOT/s1"; mkdir -p "$sd"
set +e
out=$(env -u FLEET_GH_APP_ID -u FLEET_GH_APP_INSTALLATION_ID -u FLEET_GH_APP_KEY_PATH \
      FLEET_UP_CONF="$EMPTY_CONF" FLEET_STATE_DIR="$sd" PATH="$TMPROOT/bin:$PATH" \
      "$GHT" 2>/dev/null); rc=$?
set -e
assert_eq "$out" "" "unconfigured -> prints nothing"
assert_eq "$rc" "0" "unconfigured -> exit 0"

# --- Case 2: cache hit (fresh) → serves cache, never mints ---
sd="$TMPROOT/s2"; mkdir -p "$sd"
write_cache "$sd" "ghs_CACHED" 100000
export CURL_STUB_ARGS="$TMPROOT/args_c2"
export CURL_STUB_BODY='{"token":"ghs_SHOULDNOTMINT","expires_at":"2099-01-01T00:00:00Z"}'
rm -f "$CURL_STUB_ARGS"
set +e; out=$(run_ght "$sd" 2>/dev/null); rc=$?; set -e
assert_eq "$out" "ghs_CACHED" "cache hit -> serves cached token"
assert_eq "$rc" "0" "cache hit -> exit 0"
assert_no_path "$CURL_STUB_ARGS" "cache hit -> no mint (curl not called)"

# --- Case 3: cache miss → mints, writes cache, and the JWT is well-formed ---
sd="$TMPROOT/s3"; mkdir -p "$sd"
export CURL_STUB_ARGS="$TMPROOT/args_c3"
export CURL_STUB_BODY='{"token":"ghs_MINTED","expires_at":"2099-01-01T00:00:00Z"}'
rm -f "$CURL_STUB_ARGS"
set +e; out=$(run_ght "$sd" 2>/dev/null); rc=$?; set -e
assert_eq "$out" "ghs_MINTED" "cache miss -> mints and prints token"
assert_eq "$rc" "0" "cache miss -> exit 0"
cached=$(python3 -c "import json;print(json.load(open('$sd/gh-app-token.json'))['token'])")
assert_eq "$cached" "ghs_MINTED" "cache miss -> writes minted token to cache"
want_epoch=$(python3 -c "import calendar,time;print(calendar.timegm(time.strptime('2099-01-01T00:00:00Z','%Y-%m-%dT%H:%M:%SZ')))")
got_epoch=$(python3 -c "import json;print(json.load(open('$sd/gh-app-token.json'))['expires_at'])")
assert_eq "$got_epoch" "$want_epoch" "cache miss -> stores parsed epoch expiry"
# JWT shape from the recorded Authorization header.
jwt=$(grep -m1 '^Authorization: Bearer ' "$CURL_STUB_ARGS" | sed 's/^Authorization: Bearer //')
[[ -n "$jwt" ]] && ok "mint sends an Authorization: Bearer JWT" || bad "no Bearer JWT in curl args"
claims=$(python3 - "$jwt" <<'PY'
import sys, json, base64
seg = sys.argv[1].split('.')[1]
seg += '=' * (-len(seg) % 4)
d = json.loads(base64.urlsafe_b64decode(seg))
print(d['iss'], d['exp'] - d['iat'])
PY
)
assert_eq "${claims% *}" "$APP_ID" "JWT iss == App id"
assert_eq "${claims#* }" "600" "JWT exp-iat == 600s (10-min GitHub max)"

# --- Case 4: stale-margin boundary ---
sd="$TMPROOT/s4"; mkdir -p "$sd"
export CURL_STUB_ARGS="$TMPROOT/args_c4"
export CURL_STUB_BODY='{"token":"ghs_REMINTED","expires_at":"2099-01-01T00:00:00Z"}'
write_cache "$sd" "ghs_STALE" 100   # inside the 300s margin → treat as miss
rm -f "$CURL_STUB_ARGS"
set +e; out=$(run_ght "$sd" 2>/dev/null); set -e
assert_eq "$out" "ghs_REMINTED" "inside stale margin -> re-mints"
write_cache "$sd" "ghs_FRESH" 400   # outside the 300s margin → serve cache
rm -f "$CURL_STUB_ARGS"
set +e; out=$(run_ght "$sd" 2>/dev/null); set -e
assert_eq "$out" "ghs_FRESH" "outside stale margin -> serves cache"
assert_no_path "$CURL_STUB_ARGS" "outside stale margin -> no mint"

# --- Case 5: lock held by a dead owner → cleaned up, then mints ---
sd="$TMPROOT/s5"; mkdir -p "$sd"
export CURL_STUB_ARGS="$TMPROOT/args_c5"
export CURL_STUB_BODY='{"token":"ghs_AFTERCLEAN","expires_at":"2099-01-01T00:00:00Z"}'
mkdir -p "$sd/gh-app-token.lock"
dead=999999; while kill -0 "$dead" 2>/dev/null; do dead=$((dead + 1)); done
echo "$dead" > "$sd/gh-app-token.lock/pid"
rm -f "$CURL_STUB_ARGS"
set +e; out=$(run_ght "$sd" 2>/dev/null); rc=$?; set -e
assert_eq "$out" "ghs_AFTERCLEAN" "stale-owner lock -> cleaned and mints"
assert_eq "$rc" "0" "stale-owner lock -> exit 0"
assert_no_path "$sd/gh-app-token.lock" "stale-owner lock -> lock released at exit"

# --- Case 6: lock held by a LIVE owner → fall back to still-valid cache, no mint ---
sd="$TMPROOT/s6"; mkdir -p "$sd"
export CURL_STUB_ARGS="$TMPROOT/args_c6"
export CURL_STUB_BODY='{"token":"ghs_SHOULDNOTMINT2","expires_at":"2099-01-01T00:00:00Z"}'
mkdir -p "$sd/gh-app-token.lock"
echo "$$" > "$sd/gh-app-token.lock/pid"      # this shell — definitely alive
write_cache "$sd" "ghs_WITHINMARGIN" 100      # inside margin but still real-valid
rm -f "$CURL_STUB_ARGS"
LOCK_TRIES=2
set +e; out=$(run_ght "$sd" 2>/dev/null); rc=$?; set -e
LOCK_TRIES=""
assert_eq "$out" "ghs_WITHINMARGIN" "live-owner contention -> serves still-valid cache"
assert_eq "$rc" "0" "live-owner contention -> exit 0"
assert_no_path "$CURL_STUB_ARGS" "live-owner contention -> no mint"

# --- Case 7: group/other-readable signing key → warns on stderr, still serves ---
sd="$TMPROOT/s7"; mkdir -p "$sd"
LOOSE_KEY="$TMPROOT/loose-key.pem"
cp "$KEY" "$LOOSE_KEY"; chmod 644 "$LOOSE_KEY"
write_cache "$sd" "ghs_WARNKEY" 100000        # cache hit → no mint needed
export CURL_STUB_ARGS="$TMPROOT/args_c7"
export CURL_STUB_BODY='{"token":"x","expires_at":"2099-01-01T00:00:00Z"}'
rm -f "$CURL_STUB_ARGS"
set +e
err=$(FLEET_UP_CONF="$EMPTY_CONF" FLEET_STATE_DIR="$sd" \
      FLEET_GH_APP_ID="$APP_ID" FLEET_GH_APP_INSTALLATION_ID="$INSTALL_ID" \
      FLEET_GH_APP_KEY_PATH="$LOOSE_KEY" PATH="$TMPROOT/bin:$PATH" \
      "$GHT" 2>&1 >/dev/null); rc=$?
set -e
assert_eq "$rc" "0" "loose-perm key -> still exit 0 (warn, not fail)"
case "$err" in *"group/other-readable"*) ok "loose-perm key -> warns on stderr";; *) bad "loose-perm key -> missing warning: [$err]";; esac

summarize "fleet-gh-token tests"
