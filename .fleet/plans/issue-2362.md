# Plan: fleet-rebase: timeout-guard network git ops + stale-lock detection (+ fleet-wide net-call audit)

- **Issue:** #2362
- **Model:** opus — cross-script bash with portability subtleties (shadow functions, coreutils-vs-System32 `timeout`, ssh-config idempotency), but the approach is fully specified below; no novel design remains.
- **Date:** 2026-07-13

## Verified current state (audit @ master `3035f07b`)

Root cause per the 07-13 thread comment: host TCP connections to GitHub intermittently black-hole (silent death, no RST), so any unguarded network call hangs forever. Three fleet outages in 4 days. Audit of every named surface:

- **`fleet-rebase`** — 13 unguarded network sites: `git fetch` L250, L265, L303, L309, L363; `git ls-remote` L266, L272; `git push` L421; `gh pr view` L176, L203; `gh pr edit` L223, L224, L297; `gh pr list` L285. Lock (L103–129): `mkdir $STATE_DIR/fleet-rebase.lock.d` + pid file; the stale-break (L110–115) fires only when the holder pid is **dead** — an alive-but-hung holder defers forever with the benign L125 message. Confirms the outage-1 mechanism.
- **`fleet-claim`** — ~49 `gh` network invocations plus `git fetch`/`git push --force-with-lease`. Runs **inline in the dispatcher main loop** (`reservation-role`, `planning-claim`), so one hung `gh issue view` is a fleet-wide outage — exactly outage 2 (18 h).
- **`fleet-clone-freshness.sh`** — `advance_main_clone`'s `git fetch origin master` (L116) is `|| true`-guarded against *failure* but not against *hanging*; invoked by the dispatcher at startup and per-tick. Outage 3.
- **`fleet-dispatcher`** — already resolves `TIMEOUT_CMD` (L372–377, `timeout` → `gtimeout` → empty) but applies it **only** to tmux send-keys, and its `command -v timeout` probe has the latent Windows System32 bug. Its remaining network exposure is via child `fleet-claim` and the sourced `advance_main_clone`.
- **Already guarded — no work needed:** `fleet-queue-ingest`'s embedded python has `timeout=15` on all subprocess `gh` calls; `fleet_gh_poll.py` has subprocess + urllib timeouts; the scout never fetches (rev-parse only).
- **`~/.fleet/alerts/` exists** (created by fleet-up) — an alert = file drop + loud log line for the human/babysit.

## Approach

One task, one PR — the lib and its consumers must land together to be testable.

1. **New shared lib `scripts/fleet/fleet-net.sh`** (sourced, not executed):
   - Resolve `FLEET_TIMEOUT_CMD` once: coreutils `timeout` → `gtimeout` → empty. Verify coreutils-ness (`--version` output) before trusting a bare `timeout` on PATH — Windows System32 `timeout.exe` is an interactive wait, not a command runner. Degrade to unguarded passthrough when none resolves.
   - Shadow functions `gh()` and `git()` that call `command gh` / `command git` internally (no recursion). `git()` scans past global flags (`-C <path>`, `-c <k=v>`, `--foo`) to find the subcommand and applies the timeout **only** to network subcommands (`fetch`, `push`, `pull`, `ls-remote`, `clone`, `remote`, `fetch-pack`, `send-pack`); everything else passes through untouched — never time-limit a local `rebase`/`checkout`.
   - Budget: `FLEET_NET_TIMEOUT` default **120 s**, env-overridable. `timeout` exit 124 flows into existing `|| true` / `rc=$?` handling as an ordinary failure — the desired skip-and-continue degrade the call sites already implement.
   - Why shadow instead of per-site edits: fleet-claim alone has ~49 sites; per-site churn is unreviewable, and a shadow guards *future* call sites by construction.
2. **Source `fleet-net.sh`** in `fleet-rebase`, `fleet-claim`, and `fleet-dispatcher` (early — before the dispatcher sources `fleet-clone-freshness.sh`, so `advance_main_clone`'s fetch resolves the shadow at call time). Missing-lib fallback: skip sourcing (graceful degrade). Retire the dispatcher's inline `TIMEOUT_CMD` resolution in favor of the lib so two copies don't drift; the send-keys guard keeps working off the same var (and inherits the coreutils probe, fixing its latent Windows bug).
3. **`fleet-rebase` hung-lock escalation:** `acquire_lock` additionally stamps `date +%s > "$LOCK_DIR/started"`. On the defer path with an **alive** holder: if `now − started > FLEET_REBASE_LOCK_HUNG_SECS` (default **1800**), log a loud `HUNG-LOCK:` line and write `~/.fleet/alerts/fleet-rebase-hung-lock` instead of only the benign message. Do **not** auto-break an alive holder. Missing `started` file (pre-fix holder during rollout) → skip the age check.
4. **`install.sh` host-protection section** (idempotent, mirrors the zshrc snippet pattern):
   - (a) ssh keepalives for `Host github.com` as a **marked** block in `~/.ssh/config`, with a `--no-ssh-config` opt-out. If any existing `Host github.com` block is present (e.g. a manual edit), **skip and warn** — never merge into or duplicate a user-owned block.
   - (b) `git config --global http.lowSpeedLimit 1000` / `http.lowSpeedTime 60` (idempotent by nature).
   - (c) If neither coreutils `timeout` nor `gtimeout` resolves: install the python shim (`scripts/fleet/timeout-shim.py` → `~/bin/timeout`, via all three tool-registry edits) and print the coreutils recommendation.
5. **Tests** (hermetic `tests/test_*.sh`):
   - `test_fleet_net_guard.sh` — subcommand classification (incl. `-C`/`-c` flag skip), network op gets the prefix / local op doesn't, exit-124 propagation, empty-`FLEET_TIMEOUT_CMD` passthrough, non-coreutils `timeout` rejected by the probe.
   - `test_fleet_rebase_hung_lock.sh` — lock held by a live `sleep` holder with a backdated `started` → loud message + alert file, still defers; fresh holder → benign message only; dead holder → stale-break unchanged; missing `started` → no escalation.

## Affected files
- `scripts/fleet/fleet-net.sh` — new shared net-guard lib
- `scripts/fleet/fleet-rebase` — source lib; `started` stamp; hung-lock escalation
- `scripts/fleet/fleet-claim` — source lib
- `scripts/fleet/fleet-dispatcher` — source lib early; drop the inline `TIMEOUT_CMD` resolution
- `scripts/fleet/install.sh` — host-protection section + registry entries for the shim
- `scripts/fleet/timeout-shim.py` — new (ships only as the (c) fallback)
- `scripts/fleet/tests/test_fleet_net_guard.sh`, `tests/test_fleet_rebase_hung_lock.sh` — new

## Gotchas
- **Activation lag:** dispatcher/rebase/claim run from the main clone's master — the fix is inert until the PR merges and the main clone advances; a `fleet-up` bounce activates it.
- Shadow wrappers must use `command git` / `command gh` internally or they recurse.
- Windows System32 `timeout.exe` shadows coreutils in some PATH orders — the coreutils probe is load-bearing.
- Exit 124 aliases with ordinary failure — verified the touched call sites use boolean success checks only.
- `~/.ssh/config` may not exist / need 600 perms — create with `umask 077`.
