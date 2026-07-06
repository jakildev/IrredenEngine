#!/usr/bin/env bash
# Tests for the install-symlink freshness helpers in fleet-common.sh (#2262):
#   fleet_main_clone_root       — worktree -> main-clone resolution
#   fleet_install_stale         — stamp-vs-source mtime staleness check
#   fleet_install_refresh       — run install.sh + bump stamp (even on failure)
#   fleet_install_maybe_refresh — guarded, mkdir-locked entry point
#
# Uses a throwaway "main clone" tree with a STUB install.sh that records each
# invocation (so we can assert invoked / not-invoked) and stamps like the real
# one. mtimes are set explicitly with `touch -t` (POSIX, portable across BSD +
# GNU) so staleness is deterministic without sleeps.

set -euo pipefail

SCRIPT_DIR=$(cd "$(dirname "$0")/.." && pwd)
HELPER="$SCRIPT_DIR/fleet-common.sh"

if [[ ! -f "$HELPER" ]]; then
    echo "SKIP: helper not found at $HELPER" >&2
    exit 0
fi
if ! command -v git >/dev/null 2>&1; then
    echo "SKIP: git not available" >&2
    exit 0
fi

# shellcheck source=/dev/null
source "$HELPER"

PASS=0
FAIL=0
TMPROOT=""
cleanup() { [[ -n "$TMPROOT" && -d "$TMPROOT" ]] && rm -rf "$TMPROOT"; }
trap cleanup EXIT
TMPROOT=$(mktemp -d)

ok()   { echo "  ok: $1";   PASS=$((PASS+1)); }
fail() { echo "  FAIL: $1"; FAIL=$((FAIL+1)); }

# --- throwaway main clone + stub install.sh ---------------------------------
ROOT="$TMPROOT/clone"
mkdir -p "$ROOT/scripts/fleet/tests" \
         "$ROOT/scripts/fleet/__pycache__" \
         "$ROOT/engine/tools/bin" \
         "$ROOT/.claude/commands" \
         "$ROOT/creations/game/.claude/commands"

export FLEET_INSTALL_STAMP="$TMPROOT/state/.install-stamp"
export FLEET_INSTALL_LOCK="$TMPROOT/state/.install-refresh.lock"
export INSTALL_LOG="$TMPROOT/install-invocations.log"
mkdir -p "$TMPROOT/state"
: > "$INSTALL_LOG"

# Stub install.sh: mimic the real one's contract — record the call, stamp on
# success. STUB_EXIT lets a test force a non-zero exit (Windows-degradation).
cat > "$ROOT/scripts/fleet/install.sh" <<'STUB'
#!/usr/bin/env bash
echo "invoked $*" >> "$INSTALL_LOG"
mkdir -p "$(dirname "$FLEET_INSTALL_STAMP")"
touch "$FLEET_INSTALL_STAMP"
exit "${STUB_EXIT:-0}"
STUB
chmod +x "$ROOT/scripts/fleet/install.sh"

# Seed source files. All OLD (2026-01-01) so a MIDDLE-dated stamp is fresh.
touch -t 202601010000 "$ROOT/scripts/fleet/install.sh"
touch -t 202601010000 "$ROOT/scripts/fleet/fleet-existing-tool"
touch -t 202601010000 "$ROOT/engine/tools/bin/ir-foo"
touch -t 202601010000 "$ROOT/.claude/commands/role-worker.md"
touch -t 202601010000 "$ROOT/creations/game/.claude/commands/role-game-architect.md"

log_count() { wc -l < "$INSTALL_LOG" | tr -d ' '; }

# Monotonic mtimes: every stamp/touch advances a shared minute counter, so a
# `stamp_fresh` is strictly newer than all prior touches (fresh baseline) and a
# following `make_newer` is strictly newer than that stamp (stale). Fixed
# timestamps leaked staleness across tests (a stamp older than a leftover file);
# a strictly-increasing clock can't. Seeds above sit at minute 00, before any.
_ts_seq=0
# Sets $TS (not echo — a $(...) subshell wouldn't persist the counter). <60 calls.
_next_ts() { _ts_seq=$((_ts_seq + 1)); TS=$(printf '2026010100%02d' "$_ts_seq"); }
stamp_fresh() { _next_ts; touch -t "$TS" "$FLEET_INSTALL_STAMP"; }
make_newer()  { _next_ts; touch -t "$TS" "$1"; }

# --- T1: stamp missing -> stale ---------------------------------------------
echo "T1: missing stamp is stale"
rm -f "$FLEET_INSTALL_STAMP"
if fleet_install_stale "$ROOT"; then ok "stale when stamp absent"; else fail "not stale with no stamp"; fi

# --- T2: stamp newer than every source -> fresh -----------------------------
echo "T2: all sources older than stamp -> fresh"
stamp_fresh
if fleet_install_stale "$ROOT"; then fail "reported stale when current"; else ok "fresh when nothing newer"; fi

# --- T3: a scripts/fleet file newer than the stamp -> stale -----------------
echo "T3: newer scripts/fleet tool -> stale"
make_newer "$ROOT/scripts/fleet/fleet-review-verdict"   # the real stranding case
if fleet_install_stale "$ROOT"; then ok "stale when a tool is newer"; else fail "missed a newer tool"; fi

# --- T4: maybe_refresh on stale -> runs install.sh, stamps, becomes fresh ----
echo "T4: maybe_refresh refreshes when stale"
before=$(log_count)
fleet_install_maybe_refresh "$ROOT"
after=$(log_count)
[[ "$after" -gt "$before" ]] && ok "install.sh invoked on stale" || fail "install.sh not invoked ($before->$after)"
if fleet_install_stale "$ROOT"; then fail "still stale after refresh"; else ok "fresh after refresh (stamp bumped)"; fi
[[ ! -d "$FLEET_INSTALL_LOCK" ]] && ok "lock released after refresh" || fail "lock left behind"

# --- T5: maybe_refresh when fresh -> install.sh NOT invoked ------------------
echo "T5: maybe_refresh is a no-op when fresh"
before=$(log_count)
fleet_install_maybe_refresh "$ROOT"
after=$(log_count)
[[ "$after" -eq "$before" ]] && ok "install.sh not invoked when fresh" || fail "re-ran install.sh needlessly ($before->$after)"

# --- T6: each source set independently triggers staleness -------------------
echo "T6: ir-* and role-*.md also trigger staleness"
stamp_fresh
make_newer "$ROOT/engine/tools/bin/ir-bar"
if fleet_install_stale "$ROOT"; then ok "newer ir-* is stale"; else fail "missed a newer ir-* tool"; fi
stamp_fresh
make_newer "$ROOT/creations/game/.claude/commands/role-game-worker.md"
if fleet_install_stale "$ROOT"; then ok "newer game role-*.md is stale"; else fail "missed a newer game role file"; fi

# --- T7: scoping — non-source churn does NOT false-positive -----------------
echo "T7: subdir / non-matching files do not trigger staleness"
stamp_fresh
make_newer "$ROOT/scripts/fleet/tests/test_new.sh"        # depth 2 -> ignored
make_newer "$ROOT/scripts/fleet/__pycache__"              # dir mtime bump -> ignored
make_newer "$ROOT/engine/tools/bin/not-an-ir-tool"        # wrong name -> ignored
make_newer "$ROOT/.claude/commands/not-a-role.md"         # wrong name -> ignored
if fleet_install_stale "$ROOT"; then fail "false-positive on non-source churn"; else ok "ignores test/pycache/non-matching churn"; fi

# --- T8: Windows-degradation — failed install.sh still bumps the stamp ------
echo "T8: a failing install.sh still stamps (no per-dispatch re-run)"
stamp_fresh
make_newer "$ROOT/scripts/fleet/fleet-win-tool"
before=$(log_count)
STUB_EXIT=1 fleet_install_maybe_refresh "$ROOT"   # simulate symlink-incapable host
after=$(log_count)
[[ "$after" -gt "$before" ]] && ok "install.sh attempted on stale" || fail "install.sh not attempted"
if fleet_install_stale "$ROOT"; then fail "still stale after a failed refresh (would re-run every dispatch)"; else ok "stamp bumped despite non-zero exit"; fi
before=$(log_count)
fleet_install_maybe_refresh "$ROOT"               # nothing newer now
after=$(log_count)
[[ "$after" -eq "$before" ]] && ok "no re-run on a symlink-incapable host once stamped" || fail "re-ran despite fresh stamp"

# --- T9: stale lock (crashed holder) is stolen, refresh still happens -------
echo "T9: a stale lock is stolen"
stamp_fresh
make_newer "$ROOT/scripts/fleet/fleet-after-crash"
mkdir -p "$FLEET_INSTALL_LOCK"
touch -t 202601010000 "$FLEET_INSTALL_LOCK"        # >1 min old -> stealable
before=$(log_count)
FLEET_INSTALL_WAIT_SECS=0 FLEET_INSTALL_WAIT_MAX=2 fleet_install_maybe_refresh "$ROOT"
after=$(log_count)
[[ "$after" -gt "$before" ]] && ok "stole stale lock and refreshed" || fail "did not steal a stale lock"
[[ ! -d "$FLEET_INSTALL_LOCK" ]] && ok "stolen lock released after refresh" || fail "lock left behind after steal"

# --- T10: a live lock is respected — proceed without a double-refresh --------
echo "T10: a live (fresh) lock is not stolen; caller proceeds without refresh"
stamp_fresh
make_newer "$ROOT/scripts/fleet/fleet-contended"
mkdir -p "$FLEET_INSTALL_LOCK"                     # fresh mtime -> a live peer's lock
before=$(log_count)
FLEET_INSTALL_WAIT_SECS=0 FLEET_INSTALL_WAIT_MAX=2 fleet_install_maybe_refresh "$ROOT"
after=$(log_count)
[[ "$after" -eq "$before" ]] && ok "did not double-run install.sh under a live lock" || fail "ran install.sh while a peer held the lock"
[[ -d "$FLEET_INSTALL_LOCK" ]] && ok "live peer's lock left intact" || fail "removed a lock we did not own"
rmdir "$FLEET_INSTALL_LOCK"

# --- T11: fleet_main_clone_root returns a plain (non-worktree) root as-is ----
echo "T11: main-clone resolution — non-worktree root unchanged"
mkdir -p "$ROOT/.git"                               # a real clone has .git as a DIR
resolved=$(fleet_main_clone_root "$ROOT")
[[ "$resolved" == "$ROOT" ]] && ok "plain clone root returned unchanged" || fail "mangled a non-worktree root: $resolved"

echo ""
echo "PASS: $PASS  FAIL: $FAIL"
[[ $FAIL -eq 0 ]]
