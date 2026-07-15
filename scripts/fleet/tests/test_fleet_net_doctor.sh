#!/usr/bin/env bash
# Tests for fleet-net-doctor (socket/ephemeral-port pressure diagnostics).
#
# Hermetic: netstat / ss / sysctl / ps / sleep are PATH-shimmed to fixture
# readers — no live socket tables, no live processes, no real waiting. The
# netstat shim counts invocations so the reaper probe's second census can
# serve a different fixture (draining vs frozen).
#
# Covers:
#   - healthy census → VERDICT: OK, exit 0
#   - high zombies + draining second census → WARN, exit 1
#   - high zombies + frozen second census → CRITICAL (reaper wedged), exit 2
#   - free ephemeral ports below the floor → CRITICAL, exit 2
#   - hung-process scan: day-old etime + mid-line `gh issue` match (the
#     anchored-^gh regression), fresh process NOT flagged, zero-padded etime
#     ("08:23") parsed as base 10
#   - Linux ss spelling normalization (TIME-WAIT → TIME_WAIT)

set -euo pipefail

SCRIPT_DIR=$(cd "$(dirname "$0")/.." && pwd)
DOCTOR="$SCRIPT_DIR/fleet-net-doctor"
source "$(dirname "$0")/lib_assert.sh"

if [[ ! -x "$DOCTOR" ]]; then
    echo "test setup: fleet-net-doctor not found at $DOCTOR" >&2
    exit 1
fi

TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
SHIM="$TMP/bin"
FIX="$TMP/fix"
mkdir -p "$SHIM" "$FIX"

# --- PATH shims ---------------------------------------------------------------
# netstat: serves $FIX/netstat.out; from the 2nd call on, serves
# $FIX/netstat2.out when present (reaper-probe recount).
cat >"$SHIM/netstat" <<EOF
#!/usr/bin/env bash
count_file="$FIX/.netstat-calls"
n=\$(cat "\$count_file" 2>/dev/null || echo 0)
echo \$((n + 1)) >"\$count_file"
if (( n + 1 >= 2 )) && [[ -f "$FIX/netstat2.out" ]]; then
    cat "$FIX/netstat2.out"
else
    cat "$FIX/netstat.out"
fi
EOF
cat >"$SHIM/ss" <<EOF
#!/usr/bin/env bash
cat "$FIX/ss.out"
EOF
cat >"$SHIM/ps" <<EOF
#!/usr/bin/env bash
cat "$FIX/ps.out"
EOF
printf '#!/usr/bin/env bash\nexit 0\n' >"$SHIM/sleep"
printf '#!/usr/bin/env bash\necho 0\n' >"$SHIM/sysctl"
chmod +x "$SHIM"/*
export PATH="$SHIM:$PATH"

# Deterministic knobs for every case; individual tests override as needed.
export FLEET_NET_DOCTOR_OS=Darwin
export FLEET_NET_DOCTOR_PORT_RANGE="49152 65535"
export FLEET_NET_DOCTOR_PROBE_SECS=0
export FLEET_NET_DOCTOR_WARN_ZOMBIES=4000
export FLEET_NET_DOCTOR_CRIT_FREE=1000
export HUNG_MINUTES=30

# macOS netstat line for a local socket in STATE with local port PORT.
ns_line() {
    printf 'tcp4       0      0  192.168.1.5.%s      140.82.116.3.443       %s\n' "$2" "$1"
}

reset_calls() { rm -f "$FIX/.netstat-calls" "$FIX/netstat2.out"; }

# --- T1: healthy census → OK -------------------------------------------------
echo "T1: healthy census"
reset_calls
{
    echo "Active Internet connections (including servers)"
    echo "Proto Recv-Q Send-Q  Local Address          Foreign Address        (state)"
    ns_line ESTABLISHED 50001
    ns_line TIME_WAIT 50002
} >"$FIX/netstat.out"
: >"$FIX/ps.out"
rc=0; out=$("$DOCTOR") || rc=$?
assert_eq "$rc" "0" "exit 0 when healthy"
assert_contains "$out" "VERDICT: OK" "verdict OK"
assert_contains "$out" "zombie sockets:      1 (TIME_WAIT=1 FIN_WAIT_1=0 LAST_ACK=0)" "state census"
assert_contains "$out" "none older than 30m" "no hung processes"

# --- T2: high zombies, second census drains → WARN ---------------------------
echo "T2: zombie pressure, reaper draining"
reset_calls
{ ns_line TIME_WAIT 50001; ns_line TIME_WAIT 50002; ns_line TIME_WAIT 50003; } >"$FIX/netstat.out"
{ ns_line TIME_WAIT 50001; } >"$FIX/netstat2.out"
rc=0; out=$(FLEET_NET_DOCTOR_WARN_ZOMBIES=2 "$DOCTOR") || rc=$?
assert_eq "$rc" "1" "exit 1 on WARN"
assert_contains "$out" "REAPER: draining (3 -> 1" "probe sees the drain"
assert_contains "$out" "VERDICT: WARN" "verdict WARN"

# --- T3: high zombies, second census frozen → CRITICAL ------------------------
echo "T3: zombie pressure, reaper wedged"
reset_calls
{ ns_line TIME_WAIT 50001; ns_line TIME_WAIT 50002; ns_line TIME_WAIT 50003; } >"$FIX/netstat.out"
cp "$FIX/netstat.out" "$FIX/netstat2.out"
rc=0; out=$(FLEET_NET_DOCTOR_WARN_ZOMBIES=2 "$DOCTOR") || rc=$?
assert_eq "$rc" "2" "exit 2 when reaper is wedged"
assert_contains "$out" "REAPER: TIME_WAIT not draining (3 -> 3" "probe sees the freeze"
assert_contains "$out" "reaper wedged" "critical verdict names the wedge"

# --- T4: free ports below the floor → CRITICAL --------------------------------
echo "T4: ephemeral ports nearly exhausted"
reset_calls
{ ns_line ESTABLISHED 49152; ns_line ESTABLISHED 49153; ns_line ESTABLISHED 49154; } >"$FIX/netstat.out"
# Range of 5 ports, 3 used, floor of 3 → free (2) < floor → CRITICAL.
rc=0; out=$(FLEET_NET_DOCTOR_PORT_RANGE="49152 49156" FLEET_NET_DOCTOR_CRIT_FREE=3 "$DOCTOR") || rc=$?
assert_eq "$rc" "2" "exit 2 on port exhaustion"
assert_contains "$out" "ephemeral ports use: 3 / 5" "port census"
assert_contains "$out" "EADDRNOTAVAIL" "verdict names the failure signature"

# --- T5: hung-process scan ----------------------------------------------------
echo "T5: hung git/ssh/gh processes"
reset_calls
{ ns_line ESTABLISHED 50001; } >"$FIX/netstat.out"
cat >"$FIX/ps.out" <<'EOF'
67734 05-18:38:42 /opt/homebrew/opt/git/libexec/git-core/git fetch --update-head-ok origin HEAD
92844 02-20:30:56 /usr/bin/ssh -o SendEnv=GIT_PROTOCOL git@github.com git-upload-pack 'jakildev/IrredenEngine.git'
71980 08:23:00 gh issue list --repo jakildev/IrredenEngine --state open --label fleet:needs-plan
50000 00:29 git fetch origin master
EOF
rc=0; out=$("$DOCTOR") || rc=$?
assert_eq "$rc" "0" "hung processes alone do not change the verdict"
assert_contains "$out" "HUNG (05-18:38:42): 67734" "day-old git fetch flagged"
assert_contains "$out" "HUNG (02-20:30:56): 92844" "old ssh-to-github flagged"
assert_contains "$out" "HUNG (08:23:00): 71980" "mid-line gh call flagged (zero-padded etime)"
assert_absent "$out" "HUNG (00:29)" "fresh process not flagged"
assert_contains "$out" "3 found — rerun with --kill-hung to reap" "reap hint with count"

# --- T6: Linux ss spelling normalization ---------------------------------------
echo "T6: Linux census via ss"
reset_calls
: >"$FIX/ps.out"
cat >"$FIX/ss.out" <<'EOF'
TIME-WAIT  0 0 10.0.0.2:50001 140.82.116.3:443
FIN-WAIT-1 0 0 10.0.0.2:50002 140.82.116.3:443
LAST-ACK   0 0 10.0.0.2:50003 140.82.116.3:443
ESTAB      0 0 10.0.0.2:50004 140.82.116.3:443
EOF
rc=0; out=$(FLEET_NET_DOCTOR_OS=Linux "$DOCTOR") || rc=$?
assert_eq "$rc" "0" "exit 0 (below thresholds)"
assert_contains "$out" "(TIME_WAIT=1 FIN_WAIT_1=1 LAST_ACK=1)" "ss states normalized to BSD names"

summarize "fleet-net-doctor tests"
