# Plan: quota Q2 — centralized cross-device polling (one poller per account)

- **Issue:** #2220
- **Model:** opus
- **Date:** 2026-07-03
- **Epic:** #1394 — see `.fleet/plans/issue-1394.md` for full context
- **Blocked by:** #2219

## Scope

Collapse N per-host `fleet-state-scout` daemons into **one authoritative GitHub poller per account** plus N−1 followers that consume the leader's already-fetched state over the LAN instead of calling `gh` themselves. In steady-state idle polling, only the leader's per-account REST/GraphQL counters move; followers make zero GitHub read calls yet keep a byte-identical `~/.fleet/state/` so every consumer (dispatcher, roles, `fleet-pr`/`fleet-issue` wrappers) stays oblivious.

Out of scope (stays per-host, unchanged): all **mutations** — claim label add/remove, PR/issue comments, label transitions, `gh pr merge/edit/review`, ingest label writes. These are cheap and correctly host-scoped. This ticket touches only the **polling reads** in `collect_state()`. Depends on **Q1** (conditional-REST poller core + ETag cache); Q2 reuses that conditional-request helper for the intra-fleet LAN hop, which is why it is Blocked-by-Q1.

## Verified current state (the shareable-vs-host-local split)

The scout's per-tick snapshot is assembled in `collect_state()` and written atomically to `~/.fleet/state/state.json` in `tick_once()`.

- **GitHub-derived → shareable, byte-identical on every host:** the eight `fetch_specs` per repo — `prs, needs_plan, plan_review, human_approved, closed_fleet_queued, recent_merged_prs, tasks, epics` — plus the per-item detail caches `prs/`, `diffs/`, `issues/` refreshed by `refresh_all_details()`. Every one is a `gh` call against the two repos. These are exactly the reads that multiply per host. Claim ownership is likewise **GitHub-derived**: it is parsed back out of the `fleet:claim-<host>-<agent>` labels in `fetch_task_queue`, not from any local tracker — so the *observation* of claims is shareable even though claim *writes* are host-local.
- **Host-local → must stay per-host:** `repos.<key>.path`, the local clone path; `clone_freshness`, a `git rev-parse` of *this host's* checkout vs its own `origin/master`; and everything derived downstream per host — the projections/slices, `seen-hashes/`, and per-role `triggers/`, because each host must fire triggers for *its own* running roles.
- **Reconcile locality (the sharp edge):** `tick_once` spawns three side-effects on projection change — `fleet-claim cleanup --gh` + `fleet-claim reconcile --apply` and `fleet-queue-ingest`. `cleanup` and `ingest` are **global** (stale-label reaping, human:approved→queued); running them on all N hosts multiplies mutation calls and races. `reconcile --apply` is conservative **host-local** + TTL-gated auto-fixes — it reconciles *this host's* own claim tracker, so it must keep firing per-host. These fire only on projection *change*, so they never move counters during idle polling.
- **Existing scout-down signal to extend:** consumers already treat `generated_at` older than ~5 min as "scout is down" (`docs/agents/FLEET-CACHE.md`, `docs/agents/FLEET-RUNTIME.md`). The dispatcher watchdog, however, keys off **local file mtime** (`scout_unhealthy()` → `STATE_FILE.stat().st_mtime`, `SCOUT_STALENESS_SECONDS = 120`). Host identity is already canonical via `host_from_uname`/`derive_host` (`fleet-claim`); `~/.config/irreden/host.toml` is the established per-host config surface (`setup-windows.sh`).

## Approach (single committed mechanism)

**Follower-pull over a leader-served HTTP endpoint, conditional GET, static leader flag.**

1. **Leader selection — static, boring.** Add `[fleet] poll_role = "leader" | "follower"` (default `"leader"`) and `poll_port` (default `8477`) to `~/.config/irreden/host.toml`. The scout reads it once at startup. No election, no heartbeat contest — one line per host, matching fleet's static-config conventions. Home desktop = leader; laptop/second host = follower.

2. **Leader path — unchanged fetch + serve.** The leader runs `collect_state()` exactly as today and additionally starts a tiny stdlib `http.server` thread (daemon thread, torn down on the existing `SIGTERM`/`TERMINATE` path) bound to `0.0.0.0:poll_port`, serving a **read-only bundle of the whole `~/.fleet/state/` dir** (state.json + projections + `prs/`/`diffs/`/`issues/`) as one payload with an **ETag equal to the snapshot's `generated_at`**. Everything the followers' `fleet-pr`/`fleet-issue` wrappers read is included, so followers never fall back to `gh` on a per-item drill-in.

3. **Follower path — consume, don't fetch.** On follower hosts, replace the `collect_state()` GitHub fan-out with a **conditional GET** (reusing Q1's ETag/If-None-Match helper) against `http://<leader-host>:<poll_port>`. A `304` (nothing changed) costs one tiny LAN round-trip and **zero GitHub quota**; a `200` yields the fresh bundle, which the follower unpacks and writes into its own `~/.fleet/state/` via the existing `write_atomic()` — **preserving the leader's `generated_at` verbatim** (do not restamp). The follower then still runs the local half of `tick_once` — projections, slices, `triggers/` — off the merged state, so consumers and local roles behave identically. `cleanup`/`ingest` spawns are guarded to run only when this host is the authoritative poller for the tick (leader, or a follower currently self-polling). `clone_freshness` and `repos.<key>.path` are recomputed locally (never taken from the bundle).

4. **Leader failure / staleness — extend the existing generated_at signal.** Because the follower writes the leader's `generated_at` untouched, a dead leader means its `generated_at` stops advancing and every consumer's existing 5-min guard trips automatically — *provided staleness is judged by the in-file `generated_at`, not local mtime*. **The one required consumer change:** switch `fleet-dispatcher`'s `scout_unhealthy()` from `st_mtime` to reading the in-file `generated_at`, so a follower that keeps re-writing a *stale* bundle each tick can't mask the death by bumping mtime. Additionally the follower's own tick applies a `LEADER_STALE_SECONDS` threshold: if the leader is unreachable, or its `generated_at` is already stale beyond the window, the follower **self-polls GitHub directly for that tick** (identical to today's solo behavior). This handles both leader-crash and laptop-away-from-home with no election — a follower off-LAN is just a temporary N=1 solo poller, which is exactly the pre-Q2 behavior and introduces no multiplication (the home leader is typically offline when the laptop is away).

**Why this mechanism over the alternatives:** shared FS (NFS/SMB) is fragile across mac + WSL2 + native-Windows path/permission/locking semantics and dies when the laptop leaves the LAN; **syncthing** adds a third-party daemon to babysit on three heterogeneous hosts; **committing state to a git ref every 30s** spends the very GitHub quota being conserved and pollutes history; **rsync-on-tick** needs SSH keys + reachable sshd across three OSes. The **leader-served HTTP + conditional-GET follower-pull** wins because it (a) reuses Q1's conditional-request machinery for a free `304` no-change poll — the reason Q2 sequences after Q1; (b) is pure Python stdlib, no new dependency or daemon; (c) is **follower-pull**, so the leader needs no follower inventory (zero-config to add a host) and only one inbound LAN port; (d) degrades cleanly to solo self-polling when off-LAN.

## Affected files

- `scripts/fleet/fleet_poll_topology.py` — **new** module (co-located with the scout like `fleet_gh_poll.py`): `read_poll_config()` (host.toml `[fleet]` section), `generated_at` staleness helpers, bundle build/serialize/apply, `LeaderServer` (stdlib `ThreadingHTTPServer`), and `fetch_from_leader()` (the LAN-hop conditional GET).
- `scripts/fleet/fleet_gh_poll.py` — add a general `conditional_get_url()` (raw If-None-Match GET against an arbitrary URL, no gh token) so the LAN hop reuses Q1's conditional-request machinery verbatim.
- `scripts/fleet/fleet-state-scout` — primary. Read `poll_role`/`poll_port` at startup; leader starts the daemon HTTP server thread serving the state-dir bundle with `generated_at` as ETag; follower replaces `collect_state()`'s GitHub fan-out with a conditional GET + local unpack (preserving `generated_at`), retains the local projection/slice/trigger half of `tick_once`, and self-polls on leader-unreachable/stale; guard `cleanup` + `ingest` spawns + `refresh_all_details` to the authoritative-poller tick.
- `scripts/fleet/fleet-dispatcher` — change `scout_unhealthy()` to key staleness off in-file `generated_at` instead of `st_mtime` (mtime fallback when unparseable); the watchdog re-arm logic is unchanged.
- `scripts/fleet/setup-windows.sh` and `scripts/fleet/fleet-up.conf.sample` — emit/document the new `[fleet] poll_role` / `poll_port` host.toml keys.
- `scripts/fleet/fleet-up` — log leader-vs-follower mode at scout launch. Note the one inbound LAN port on the leader.
- `docs/agents/FLEET-CACHE.md` / `docs/agents/FLEET-RUNTIME.md` — document the leader/follower model, the LAN-hop conditional GET, the "follower preserves leader `generated_at`" rule, and the staleness-via-`generated_at` clarification.
- `scripts/fleet/tests/test_poll_topology.py` — follower with a mocked leader endpoint produces byte-identical `state.json`; a `304` yields zero `gh` invocations; leader-unreachable falls back to self-poll; a frozen leader `generated_at` trips the staleness guard.

## Acceptance criteria

- With 3 hosts up (1 leader + 2 followers) idling on unchanged repos, **only the leader's per-account REST/GraphQL counters advance**; both followers make **zero `gh` read calls** across a full poll interval.
- Each follower's `~/.fleet/state/state.json` + `projections/*.json` + per-item caches stay fresh (`generated_at` within the window) and are schema-identical to the leader's; dispatcher, roles, and `fleet-pr`/`fleet-issue` wrappers run unchanged with no config awareness of leader/follower.
- Killing the leader is detected by every host **within the existing ~5-min `generated_at` staleness window** (and the dispatcher's 120 s watchdog), surfacing the standard `scout cache stale or missing — run fleet-up` path — no new failure mode.
- A follower taken off-LAN (laptop away) transparently self-polls GitHub and keeps working solo, with no multiplication when the leader is offline.

## Gotchas

- **mtime masks death.** If the follower's re-write bumps `state.json` mtime while `generated_at` is frozen, mtime-based staleness would report a healthy-but-lying scout. The dispatcher must switch to in-file `generated_at`; do not restamp `generated_at` on the follower's write.
- **Don't over-centralize side-effects.** `reconcile --apply` is host-local claim hygiene and must stay per-host; only `cleanup --gh` + `ingest` go authoritative-poller-only. Getting this backwards either strands follower claims or re-multiplies mutation calls.
- **Per-item cache warmth.** The served bundle must include `prs/`/`diffs/`/`issues/`, or follower `fleet-pr view`/`fleet-issue view` cache-misses fall back to live `gh` and quietly re-introduce per-host calls.
- **Recompute, don't copy, host-local fields.** `repos.<key>.path` and `clone_freshness` must be regenerated locally after unpacking the bundle — the leader's clone path and git head are wrong on a follower.
- **First-tick bootstrap.** `fleet-up` waits for the first `state.json`; a follower whose leader isn't up yet must self-poll on tick 1 so the wait doesn't hang. The self-poll fallback covers this.
- **Two-leader misconfig.** If two hosts are both set `poll_role = "leader"`, quota multiplication silently returns. Log the mode loudly at startup.
- **Bind scope + firewall.** Bind `0.0.0.0` (not `127.0.0.1`) or cross-host followers can't reach it; the endpoint is read-only state but is unauthenticated on the LAN — acceptable for a home LAN.

## Sibling + in-flight reconciliation

- **Q1 (conditional-REST poller core) is the hard dependency.** Q2 consumes Q1's ETag/conditional-GET helper for the LAN hop. This PR is stacked on Q1's branch (#2219 / PR #2227).
- **No open PR currently touches the scout, `fleet-up`, or `fleet-dispatcher`** in these paths. #2197 (dispatcher-assigned planning) will edit `fleet-dispatcher` but in the class-routing/claim path, not `scout_unhealthy()` — disjoint surfaces.
- **#1394 is the umbrella** (fleet:epic + fleet:needs-human). Q2 is pure polling-topology code — the human-owned identity/auth work stays out of scope.
- **Shared FLEET-doc surface with Q3:** Q3 edits FLEET.md's rate-limit section; Q2 edits FLEET-CACHE/FLEET-RUNTIME — disjoint files, no conflict.
