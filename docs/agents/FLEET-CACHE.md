# Fleet state cache

The `fleet-state-scout` daemon (started by `fleet-up`) polls GitHub
+ git on a 60-second tick and writes a shared cache under
`~/.fleet/state/`. Every fleet role reads this cache instead of
running its own `gh` / `git` queries — see the role-specific
section below for which files apply.

## Layout

| Path | Producer | Reader | Purpose |
|---|---|---|---|
| `state.json` | scout | every role | List-shape state: open PRs (with labels, reviews, mergeable), needs-plan / human-approved issues (number + title + labels + updatedAt), open `fleet:queued` issue rows. ~32 KB. |
| `projections/<role>.json` | scout (slicers) | the named role | Pre-filtered per-role slice with full records (e.g. the worker lane's `tasks_open` + `feedback_prs`). ~5 KB. **Prefer this over `state.json`** when you only need your own role's items. |
| `prs/<repo>/<N>.json` | scout | `fleet-pr view`/`comments` | Full PR detail: body, conversation comments, review summaries, inline review threads, files-changed list. Refreshed only when the list query's `updatedAt` advances. |
| `diffs/<repo>/<N>-<sha>.diff` | scout | `fleet-pr diff` | Raw `gh pr diff` output, keyed by head SHA. Refreshed on rebase only; old SHAs garbage-collected. |
| `issues/<repo>/<N>.json` | scout | `fleet-issue view` | Full issue detail (body + comments + labels + state). Cached for issues in `needs_plan` / `human_approved`. |
| `repos.json` | `fleet-up` | reviewer / merger roles | One-shot owner/repo slug map: `{"engine": "jakildev/IrredenEngine", "game": "jakildev/irreden"}`. |
| `triggers/<role>` | scout | `fleet-babysit` | Empty file touched whenever this role's projection changed. Drives `fleet-babysit`'s long-back-off wake-up. |
| `seen-hashes/<role>` | scout | scout | Hash of the last projection — internal trigger-detection state. |

`<repo>` keys: `engine`, `game`. Match the `repos.<key>` slots in
`state.json`.

## Source of truth for list-y queries

When the cache is fresh, do **NOT** bypass it for `gh pr list` or
`gh issue list --label …`.
One Read replaces what used to be 3-6 fan-out gh/git calls per role
per startup.

`state.json` and `projections/<role>.json` carry an ISO-8601
`generated_at` field. If it is more than ~5 minutes old, the scout
daemon isn't running — print
`scout cache stale or missing — run fleet-up` and exit. Do not
silently fall back to direct `gh` / `git` calls (that defeats the
cache and re-introduces the rate-limit burst).

## Per-item drill-ins

Use the `fleet-pr` and `fleet-issue` wrappers for per-item lookups.
Both read the corresponding cache file and fall back to live `gh`
on cache miss (with a `cache miss for <repo>#<N>; falling back to
gh` line on stderr so misses are visible in pane logs):

| Wrapper | Replaces |
|---|---|
| `fleet-pr view <N> [--repo engine\|game]` | `gh pr view <N> --comments` |
| `fleet-pr diff <N> [--repo engine\|game]` | `gh pr diff <N>` |
| `fleet-pr comments <N> [--repo engine\|game]` | `gh pr view <N> --comments` + `gh api repos/.../pulls/<N>/comments` + `gh api repos/.../pulls/<N>/reviews` (merged) |
| `fleet-issue view <N> [--repo engine\|game]` | `gh issue view <N> --repo <slug> --json number,title,state,labels,body,comments` |

### What stays direct

- **All writes**: `gh pr edit`, `gh pr comment`, `gh pr create`,
  `gh pr merge`, `gh pr review`, `gh issue create`, `gh issue edit`.
  Wrappers are read-only.
- **`gh pr diff <N> --name-only`**: file-list extraction, used by
  reviewers for cross-host smoke labeling. The wrapper doesn't
  replicate this flag.
- **`gh pr list --state merged ...`**: closed-PR queries; not in
  scope for the open-PR cache.
- **`gh api repos/.../issues/<N>/timeline`**: label-strip event
  history; not cached.

## Per-role projections

Each role has a pre-filtered slice at
`~/.fleet/state/projections/<role>.json` carrying full records of
just the items that role works on:

| Role | Slice keys |
|---|---|
| worker | `tasks_open` (all classes, both repos), `needs_plan`, `feedback_prs` |
| sonnet-reviewer | `candidate_prs` (review-skip filter applied) |
| opus-reviewer | `flagged_prs` (`fleet:has-nits` / `fleet:needs-fix` / `fleet:needs-opus-recheck`), `plan_review` (`fleet:plan-review` issues awaiting a plan verdict, both repos) |
| merger | `prs` (engine + game, approved or non-MERGEABLE only; each tagged with its `repo`) |

**opus-reviewer:** review bodies longer than 2 KB are stored as
head + tail with an `…[truncated]…` separator (the verdict line
typically lives in the tail); for full-body context fetch with
`fleet-pr view <N>`.

**Read the projection first.** It's ~5 KB vs. ~32 KB for full
state.json — significant per-iteration token savings. Fall back to
state.json only when you need cross-role data (e.g. sonnet-reviewer
looking up an upstream PR by `headRefName`).

## Repo slug discovery

`fleet-up` writes `~/.fleet/state/repos.json` once at startup with
the engine and game owner/repo slugs derived from each repo's
`origin` remote. Reviewer / merger roles that need
the `--repo <slug>` flag should read this file rather than running
`gh repo view --json nameWithOwner --jq .nameWithOwner` per pane.
If `repos.json` is missing (rare — only happens if `fleet-up`
couldn't parse the engine remote URL), fall back to the live
discovery.

## Staleness recovery

If a role's startup finds `state.json` (or `projections/<role>.json`)
missing or older than 5 minutes:

1. Print `scout cache stale or missing — run fleet-up` (or the
   role-specific equivalent) to the pane log.
2. Exit cleanly. Don't silently fall back to direct `gh` / `git`
   calls — silent fallback re-introduces the rate-limit burst the
   cache was built to prevent.

`fleet-babysit` will relaunch the role on its normal cadence; if
the scout is genuinely down, the human can `fleet-up` to restart
it.

## Centralized cross-device polling (leader / follower)

When more than one host runs the fleet on the **same GitHub account**
(e.g. a home desktop + a laptop), each host's scout would otherwise
poll GitHub independently, multiplying the per-account API quota drain
by N (#1394). Q2 collapses that to **one authoritative poller** plus
followers that consume its already-fetched cache over the LAN.

Set the topology per host in `~/.config/irreden/host.toml`:

```toml
[fleet]
poll_role  = "leader"    # polls GitHub; serves its ~/.fleet/state bundle on the LAN
poll_port  = 8477        # the leader's one inbound TCP port
# on every OTHER host on the same account:
# poll_role   = "follower"
# poll_port   = 8477              # must match the leader
# leader_host = "192.168.1.10"    # the leader's LAN address (required on a follower)
```

Missing `[fleet]` / missing file ⇒ **leader**, so a single-host fleet
is unchanged. Exactly one host per account should be the leader.

- **Leader** — polls GitHub exactly as before and additionally serves
  a read-only bundle (`state.json` + the `prs/` `diffs/` `issues/`
  detail caches) at `http://0.0.0.0:<poll_port>/state`, with the
  snapshot's `generated_at` as the ETag.
- **Follower** — replaces its GitHub fan-out with a conditional GET of
  the leader's bundle (reusing the Q1 If-None-Match machinery), writes
  the caches to its own `~/.fleet/state/`, and **preserves the leader's
  `generated_at` verbatim** — re-deriving only the host-local
  `clone_freshness` and `repos.<key>.path`. It recomputes its own
  projections + triggers, so every consumer (roles, dispatcher,
  `fleet-pr`/`fleet-issue`) runs identically with no leader/follower
  awareness. Followers make **zero GitHub read calls** while the leader
  is reachable.
- **Leader down / off-LAN** — a follower that can't reach the leader,
  or that sees the leader's `generated_at` gone stale, transparently
  **self-polls GitHub for that tick** (a temporary solo poller — the
  pre-Q2 behavior). No election, no new failure mode.

**Staleness is judged by the in-file `generated_at`, never file
mtime.** A follower re-writes `state.json` every tick (bumping mtime)
while copying the leader's `generated_at`, so a dead leader keeps mtime
fresh while `generated_at` freezes; the dispatcher watchdog
(`scout_unhealthy`) and every role's 5-minute guard key off
`generated_at` so the death still surfaces. **Global mutations** —
`fleet-claim cleanup --gh` and `fleet-queue-ingest` — run only on the
authoritative poller (leader, or a self-polling follower); host-local
`fleet-claim reconcile --apply` keeps running on every host.

## Degraded fetches — `degraded` field in state.json

When a `gh` fetch fails (network error, auth failure, rate-limit exhaustion),
the scout preserves the **previous snapshot's data** for that section instead
of writing an empty array. The state is still written with a fresh
`generated_at` (so staleness checks pass), but a top-level `"degraded"` list
is added:

```json
{
  "generated_at": "2026-06-11T20:00:00Z",
  "degraded": ["engine.prs", "engine.tasks"],
  "repos": { ... }
}
```

Each entry names a section that fell back to last-known-good data. When no
previous snapshot exists for a failed section, the empty fallback is used and
the section is still listed in `degraded`.

**What roles should do when they see `degraded`:**

- Treat listed sections as potentially stale (data is from the prior tick).
- For **task pickup**, stale `prs[]` means the in-flight cross-check may miss
  very recently opened PRs — rely on `fleet-claim`'s live duplicate-PR
  backstop as usual.
- For **feedback scans**, a degraded `prs[]` may cause flagged PRs to be
  missed this tick; they will surface on the next successful tick.
- **Do not** treat a `degraded`-marked snapshot as "no work" and exit — the
  preserved data is still the best available picture of the world.

The scout already suppresses `reconcile --apply` and `fleet-queue-ingest`
auto-fires during degraded ticks to avoid comparing live claims against
potentially incomplete issue lists.

## Main-clone freshness — `clone_freshness` field in state.json

The scout, `fleet-claim`, and `fleet-queue-ingest` all run from the **main
clone**'s working tree (`~/src/IrredenEngine`) via `~/bin` symlinks, and import
their python parsers from it. When the main clone's checked-out `master` lags
`origin/master`, that code is stale — a merged fleet-script fix (e.g. a
`blocked_by` parser change) stays inert. `fleet-up` and the dispatcher advance
the clone (`fleet-clone-freshness.sh advance_main_clone`, a guarded ff-only),
and the scout records the result as a top-level `clone_freshness` (rev-parse
only, no fetch):

```json
{
  "generated_at": "2026-06-14T20:00:00Z",
  "clone_freshness": {"head": "<sha>", "origin_master_head": "<sha>", "behind": 0, "fresh": true},
  "repos": { ... }
}
```

`fresh: false` (behind > 0) means the running fleet-script code is stale. The
`fleet-claim` claim path additionally refuses with a loud message when the
clone is behind (`assert_clone_fresh`; opt out with `FLEET_SKIP_CLONE_FRESHNESS=1`).
A persistently `false` value usually means the clone is off-master, dirty, or
diverged so the guarded advance can't fast-forward it — fix the clone by hand
(`git -C ~/src/IrredenEngine merge --ff-only origin/master`). (#1810)

### Convention: degrade, never silent-empty

Any fleet script that fetches GitHub data (the scout, `fleet-validate-stack`,
ad-hoc `gh` wrappers) must surface a failed or degraded fetch: warn on
stderr at minimum, and write an explicit error marker into any artifact it
emits. Emitting empty results on failure is forbidden — an all-empty
`state.json` with a fresh `generated_at` is indistinguishable from "no work
anywhere". This fired live during a GraphQL rate-limit exhaustion: the scout
wrote an empty-but-fresh cache while 27 PRs were open, and the whole fleet
would have idled on it.
