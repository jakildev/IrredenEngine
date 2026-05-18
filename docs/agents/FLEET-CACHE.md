# Fleet state cache

The `fleet-state-scout` daemon (started by `fleet-up`) polls GitHub
+ git on a 60-second tick and writes a shared cache under
`~/.fleet/state/`. Every fleet role reads this cache instead of
running its own `gh` / `git` queries — see the role-specific
section below for which files apply.

## Layout

| Path | Producer | Reader | Purpose |
|---|---|---|---|
| `state.json` | scout | every role | List-shape state: open PRs (with labels, reviews, mergeable), needs-plan / human-approved issues (number + title + labels + updatedAt), parsed `TASKS.md` rows. ~32 KB. |
| `projections/<role>.json` | scout (slicers) | the named role | Pre-filtered per-role slice with full records (e.g. sonnet-author's `tasks_open` + `feedback_prs`). ~5 KB. **Prefer this over `state.json`** when you only need your own role's items. |
| `prs/<repo>/<N>.json` | scout | `fleet-pr view`/`comments` | Full PR detail: body, conversation comments, review summaries, inline review threads, files-changed list. Refreshed only when the list query's `updatedAt` advances. |
| `diffs/<repo>/<N>-<sha>.diff` | scout | `fleet-pr diff` | Raw `gh pr diff` output, keyed by head SHA. Refreshed on rebase only; old SHAs garbage-collected. |
| `issues/<repo>/<N>.json` | scout | `fleet-issue view` | Full issue detail (body + comments + labels + state). Cached for issues in `needs_plan` / `human_approved`. |
| `repos.json` | `fleet-up` | reviewer / queue-mgr / merger roles | One-shot owner/repo slug map: `{"engine": "jakildev/IrredenEngine", "game": "jakildev/irreden"}`. |
| `triggers/<role>` | scout | `fleet-babysit` | Empty file touched whenever this role's projection changed. Drives `fleet-babysit`'s long-back-off wake-up. |
| `seen-hashes/<role>` | scout | scout | Hash of the last projection — internal trigger-detection state. |

`<repo>` keys: `engine`, `game`. Match the `repos.<key>` slots in
`state.json`.

## Source of truth for list-y queries

When the cache is fresh, do **NOT** bypass it for `gh pr list`,
`gh issue list --label …`, or `git show origin/master:TASKS.md`.
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
| `fleet-issue view <N> [--repo engine\|game]` | `gh issue view <N> --comments` |

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
| sonnet-author | `tasks_open` (filtered to `[sonnet]` engine tasks), `feedback_prs` |
| opus-worker | `tasks_open` (filtered to `[opus]` tasks, both repos), `needs_plan`, `feedback_prs` |
| sonnet-reviewer | `candidate_prs` (review-skip filter applied) |
| opus-reviewer | `flagged_prs` (`fleet:has-nits` / `fleet:needs-fix`) |
| queue-manager | `needs_plan`, `human_approved`, `tasks_done`, `needs_flip` (in-progress tasks whose linked issue closed) |
| merger | `prs` (engine, approved or non-MERGEABLE only) |

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
`origin` remote. Reviewer / queue-manager / merger roles that need
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
