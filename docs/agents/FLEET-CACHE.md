# Fleet state cache

`fleet-state-scout` (started by `fleet-up`) polls GitHub and git on a
60-second tick into `~/.fleet/state/`. Every role reads this cache instead
of running its own `gh` / `git` list queries.

## Layout

| Path | Producer | Reader | Purpose |
|---|---|---|---|
| `state.json` | scout | every role | open PRs (labels, reviews, mergeable), needs-plan / approved issues, open `fleet:queued` rows; compact JSON, under 256 KB (below) |
| `projections/<role>.json` | scout | the named role | pre-filtered per-role slice, ~5 KB; prefer it over `state.json` |
| `prs/<repo>/<N>.json` | scout | `fleet-pr view`; `fleet-pr comments` on `gh` failure | full PR detail; refreshed when the list's `updatedAt` advances |
| `diffs/<repo>/<N>-<sha>.diff` | scout | `fleet-pr diff` | raw diff keyed by head SHA; old SHAs GC'd |
| `issues/<repo>/<N>.json` | scout | `fleet-issue view` on `gh` failure | full issue detail for needs-plan / approved issues; resilience only |
| `repos.json` | `fleet-up` | reviewer / merger | `{"engine": "jakildev/IrredenEngine", "game": "jakildev/irreden"}` |
| `triggers/<role>` | scout | `fleet-babysit` | touched when the role's projection changed |
| `seen-hashes/<role>` | scout | scout, `fleet-debug triggers` | last-projection fingerprint: a bare 16-hex hash, or `{"fmt":2,"kinds":{…}}` for `PER_KIND_TRIGGER_ROLES` (`worker`); an unparseable file reads as all-new and fires once |

`<repo>` keys: `engine`, `game`.

## Freshness

`state.json` and every projection carry `generated_at`. Older than ~5
minutes, or missing → print `scout cache stale or missing — run fleet-up`
and exit; no direct `gh` / `git` fallback (that re-introduces the
rate-limit burst the cache prevents); `fleet-babysit` relaunches on
cadence and the human restarts the scout with `fleet-up`. Staleness is
judged by `generated_at`, never mtime
([`FLEET-RUNTIME.md`](FLEET-RUNTIME.md) § Startup).

### Size invariant: `state.json` under 256 KB

Every role reads `state.json` with the Read tool, which hard-errors above
256 KB. The scout emits it compact (`separators=(",", ":")`,
`sort_keys=True`; the human-read caches keep `indent=2`; reflate with
`jq .`) and keeps a review body only on the latest review per PR, capped at
`REVIEW_BODY_HEAD` + `REVIEW_BODY_TAIL` (the tail is sized so the opus
reviewer's `Opus recheck required` phrase test still fires — re-measure
before lowering). Any change to a `prs[]` record's shape bumps
`PR_RECORD_SCHEMA` in `fleet-state-scout`, or the 304 path keeps serving
the on-disk shape until an unrelated list change flips the ETag. The
scout warns at 7/8 of the cap and writes
`${FLEET_ALERTS_DIR:-~/.fleet/alerts}/state-scout-state-size` while the
condition holds; check it after adding a field.

## Per-item drill-ins

| Wrapper | Policy | Replaces |
|---|---|---|
| `fleet-pr view <N> [--repo engine\|game]` | snapshot (stamp on stderr; `cache miss … falling back to gh` on a miss) | `gh pr view <N> --comments` |
| `fleet-pr diff <N> [--repo engine\|game]` | snapshot | `gh pr diff <N>` |
| `fleet-pr comments <N> [--repo engine\|game]` | live-first | `gh pr view --json comments,reviews` + the review-comments API |
| `fleet-issue view <N> [--repo engine\|game]` | live-first | `gh issue view --json number,title,state,labels,body,comments` |

Live-first exists because those callers were woken by the very item they
need (the verdict review that stamped a label, the `## Plan` comment),
which postdates the snapshot. On a `gh` failure they serve the cached
record with `serving cached snapshot from <_cached_at>; newer
comments/reviews may be missing` on stderr — one attempt, no retry loop.
The two scripts share no code; `test_fleet_cache_reader_freshness.sh` pins
the mirrored wording. Stays direct: all writes, `gh pr diff --name-only`,
`gh pr list --state merged`, and the issue timeline API.

## Per-role projections

| Role | Slice keys |
|---|---|
| worker | `tasks_open` (all classes, both repos), `needs_plan`, `feedback_prs`, `semantic_conflict_prs` |
| sonnet-reviewer | `candidate_prs` (review-skip filter applied) |
| opus-reviewer | `flagged_prs` (`fleet:needs-opus-recheck`), `plan_review` (both repos) |
| smoke-worker | `smoke_pending_prs` (host-agnostic; the dispatcher applies the host) |
| merger | `prs` (engine + game, approved or non-MERGEABLE, tagged with `repo`) |

For the target-bound lanes the slice is the dispatcher's input
([`FLEET.md § Who takes the claim`](FLEET.md)); the merger, epic steward,
and target-less runs read it directly, falling back to `state.json` only
for cross-role data (a reviewer resolving an upstream PR by
`headRefName`). Review bodies over 2 KB are stored head + tail with
`…[truncated]…`; fetch the full body with `fleet-pr comments <N>`, not
`fleet-pr view`.

## Repo slug discovery

`fleet-up` writes `repos.json` from each repo's `origin`. Roles needing
`--repo <slug>` read it instead of `gh repo view`; if it is missing, fall
back to live discovery.

## Centralized cross-device polling

Several hosts on one GitHub account share one poller. Per host in
`~/.config/irreden/host.toml`:

```toml
[fleet]
poll_role  = "leader"    # polls GitHub; serves ~/.fleet/state on the LAN
poll_port  = 8477
# follower: poll_role = "follower", poll_port = 8477, leader_host = "192.168.1.10"
```

Missing file or section ⇒ leader. The leader serves `state.json` + the
detail caches at `http://0.0.0.0:<poll_port>/state` (ETag =
`generated_at`); a follower does a conditional GET, writes its own
`~/.fleet/state/` preserving the leader's `generated_at` (re-deriving
only `clone_freshness` and `repos.<key>.path`), recomputes projections
and triggers, makes zero GitHub read calls while the leader is reachable,
and self-polls for a tick when it is not. Global mutations (`fleet-claim
cleanup --gh`, `fleet-queue-ingest`) run only on the authoritative poller;
`fleet-claim reconcile --apply` runs on every host.

## Degraded fetches

When a `gh` fetch fails, the scout keeps the previous snapshot's data for
that section, writes a fresh `generated_at`, and lists the section in a
top-level `"degraded": ["engine.prs", …]`. Treat listed sections as one
tick stale (rely on `fleet-claim`'s live duplicate-PR backstop for
pickup; flagged PRs surface next tick) and never read a degraded snapshot
as "no work". Ingest and `reconcile --apply` are suppressed during
degraded ticks. **Degrade, never silent-empty:** any fleet script that
fetches GitHub data warns on stderr and writes an explicit error marker
into what it emits — an all-empty artifact with a fresh timestamp is
indistinguishable from "no work anywhere".

## Main-clone freshness

The scout, `fleet-claim`, and ingest run from the main clone via `~/bin`
symlinks, so a `master` behind `origin/master` runs stale fleet code.
`fleet-up` and the dispatcher fast-forward it
(`fleet-clone-freshness.sh advance_main_clone`); the scout records
`clone_freshness: {head, origin_master_head, behind, fresh}`; `fleet-claim
claim` refuses while `fresh` is false (`assert_clone_fresh`;
`FLEET_SKIP_CLONE_FRESHNESS=1` opts out). A persistently false value means
the clone is off-master, dirty, or diverged:
`git -C ~/src/IrredenEngine merge --ff-only origin/master`.

### Daemon source staleness

A fresh clone does not imply a fresh daemon. `fleet-dispatcher` is bash —
a function body is parsed once at exec — and `fleet-state-scout` binds its
modules once at import; `fleet-up` launches both under `nohup` and they
live for days, so a merged fix can sit inert on the running fleet with
nothing to indicate it. Both daemons self-reload at the tick boundary:
each hashes its own load-time source surface once per tick — the script
itself plus, for the dispatcher, its `source`d files and `$FLEET_CONF`;
for the scout, the import closure it bound at boot — and `exec`s
itself in place when that hash moves. `exec` keeps the pid, so pid files,
the dispatcher's singleton lock (adopted back from its own previous
image), and the tmux pane survive; daemon-lifetime counters (run window,
dispatch counts, cap-defer state) reset, because they describe the old
image.

Three gates stand between a moved surface and the `exec`: a two-tick
debounce (a torn read taken mid-`git checkout` never re-execs a
half-written file), a syntax gate under the *running* interpreter, so a
broken file cannot replace a working image with one that dies at parse
(`bash -n` for the dispatcher; for the scout, the on-disk image's own
`--print-surface` run in a fresh interpreter — an image that cannot import
cannot report, and its answer is also the revision the reload is compared
against, which is what lets a merged change that drops an imported module
reload instead of wedging the old image on a path it can no longer read),
and an oscillation cap.
`FLEET_RELOAD_MAX` (default 3) is the number of attempts **permitted**
per `FLEET_RELOAD_WINDOW_SECONDS` (default 900) — 3 allows three reloads
and refuses the fourth, 1 allows one, `0` disables self-reload entirely —
and a refused attempt still counts toward the window, so a surface that
keeps moving holds the cap shut until the window drains. A cap refusal is
reported once per distinct surface hash and re-arms when the surface
moves again; a *syntax* refusal is reported once per distinct reason but
re-probed every `FLEET_RELOAD_REPROBE_TICKS` ticks (default 10), because
the file to repair is often one the running daemon cannot see — a module
the new image adds is outside the running image's surface, so fixing it
moves nothing that daemon hashes.

Only the load-time surface reloads. Sibling executables (`fleet-claim`,
`fleet-labels`, `fleet-rebase`) and `fleet_task_class.py` are spawned
fresh per call, so they already pick up merged fixes and are deliberately
in neither surface. `exec` preserves the environment, so a change to the
`$FLEET_CONF` **file** hot-reloads while an env-var-only override still
needs a `fleet-up` restart. The scout does not fetch its own source — new
code reaches its disk only via the dispatcher's `advance_main_clone` or a
human pull, so a dispatcher-down fleet does not self-heal.

To check what a daemon is actually running, compare its revision against
the on-disk source:

```bash
fleet-dispatcher --print-surface     # per-file + aggregate hash, on-disk truth
fleet-state-scout --print-surface
grep 'started (pid=' ~/.fleet/logs/dispatcher.log | tail -1   # rev= of the running image
```

The dispatcher stamps `rev=` into every `started (pid=…)` line, so
consecutive `started` lines with the **same pid and different revs** are
the reload audit trail. The scout publishes the same aggregate as a
top-level `scout_source_rev` in `state.json`; if that differs from
`fleet-state-scout --print-surface`'s aggregate, the running scout has not
loaded what is on disk.
