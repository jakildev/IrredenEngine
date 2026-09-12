# Fleet state cache

`fleet-state-scout` (started by `fleet-up`) polls GitHub and git on a
60-second tick into `~/.fleet/state/`. Every role reads this cache instead
of running its own `gh` / `git` list queries.

## Layout

| Path | Producer | Reader | Purpose |
|---|---|---|---|
| `state.json` | scout | every role | open PRs (labels, reviews, mergeable), needs-plan / approved issues, open `fleet:queued` rows; compact JSON, under 256 KB (below); per-section field sets and caps in "`state.json` sections" |
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

### `state.json` sections

Under `repos.<engine|game>`, every section `emit_state` writes, with its
field set and cap. Re-check this table against the size alert whenever a
section grows a field — that is the omission this table exists to close.

| Section | Fields | Cap |
|---|---|---|
| `prs` | `number, title, headRefName, headRefOid, baseRefName, author, labels, mergeable, isDraft, reviews[], updatedAt, closes_issues, schema` | open PRs, up to `OPEN_PR_FETCH_LIMIT` (200); `reviews[]` keeps a body only on the latest review per PR |
| `needs_plan` | `number, title, labels, updatedAt, blocked` | `fleet:needs-plan` open issues, one REST page (100) |
| `plan_review` | `number, title, labels, updatedAt` | `fleet:plan-review` open issues, one REST page (100) |
| `human_approved` | `number, title, labels, updatedAt, epic, blocked` | `human:approved` + `fleet:agent-approved` open issues, deduped; up to 300 each (600 combined) |
| `closed_fleet_queued` | `number` | closed `fleet:queued` issues, newest 100 |
| `recent_merged_prs` | `number, title, headRefName, baseRefName, mergedAt` | newest 30 merged PRs |
| `epics` | `number, title, labels, updatedAt, checklist, managed` | open `fleet:epic` issues, one REST page (100) |
| `tasks.open` / `tasks.in_progress` | `status, title, summary, id, model, effort, labels, owner, area, blocked_by, blocked, needs_gl_host, needs_host, backend_symmetric, issue, updatedAt, epic` | open `fleet:queued` issues minus `fleet:needs-human`/`fleet:plan-review`/`fleet:gated`, up to 200 (2 REST pages) |
| `tasks.done` | `id` | one record per `closed_fleet_queued` entry — same 100-item cap, not an independent population |
| `tasks.plan_gated` | bare issue numbers, not issue-shaped records | same pre-filter population as `tasks.open`/`tasks.in_progress` |

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
