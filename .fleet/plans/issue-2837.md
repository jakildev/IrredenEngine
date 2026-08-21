## Plan: the per-record cache readers serve a stale snapshot with no warning

- **Issue:** #2837
- **Model:** opus — two shared fleet-infra readers every role calls, with a
  GitHub-quota discipline (#1394) constraining the fix shape
- **Date:** 2026-08-09 (re-plan; supersedes the 2026-08-04 plan bounced by the
  06:07 plan review. Plan-review cleared 2026-08-09T04:27:02Z.)

Widened beyond the issue title by the 2026-08-04T17:32 scope comment:
`fleet-issue view` carries the same defect, and its lane (plan review) fails
the same structural way.

### Scope

Two scripts — `scripts/fleet/fleet-pr`, `scripts/fleet/fleet-issue` — their
regression tests, and the non-gated doc surfaces that state the wrapper
contract: `docs/agents/FLEET-CACHE.md` § "Per-item drill-ins",
`docs/agents/FLEET-FEEDBACK-HANDLING.md` § "Reading the feedback", both
scripts' module docstrings, `scripts/fleet/fleet-help`'s two index entries,
and one sentence in `docs/agents/skills/start-next-task.md`. No change to the
scout, to `state.json`'s shape, or to any other `fleet-*` tool; no
scout-cadence change; `fleet-pr view` / `diff` keep their cache reads (they
gain only a stderr snapshot line).

**No gated edits required.** One cosmetic residual:
`.claude/skills/start-next-task/SKILL.md:44-46` describes the old direction
("cache-aware … falls back to `gh issue view`"); that file is gated and the
sentence is a resilience description rather than a contract, so the PR body
flags it as a one-line human-applied follow-up rather than blocking on it.

### Verified current state (2026-08-09)

- Neither script has changed since the issue was filed. The defect is live.
- Zero open PRs in either repo touch either file.
- Record keys, confirmed against live records and the scout's fetch sites
  (`fleet-state-scout:985` PRs, `:1020` issues):
  - per-PR: `_cached_at, body, comments, files, headRefOid, inlineComments,
    number, reviews, updatedAt`
  - per-issue: `_cached_at, body, comments, labels, number, state, title,
    updatedAt`
- **Reader enumeration.** Per-record cache readers are **exactly two**:
  `fleet-pr` (`prs/`, `diffs/`) and `fleet-issue` (`issues/`). Every other
  `.fleet/state` reader touches `state.json`'s top-level arrays, which sit
  under the `generated_at` staleness contract already enforced at the protocol
  layer. `fleet_poll_topology.py` names the subdirs for follower sync only.
  The two scripts share **no code** — each is standalone by install design,
  with deliberately duplicated `repo_slug` / `DEFAULT_SLUGS` helpers. What
  they share is the *policy*.

### Root cause

No staleness axis in either script. `fleet-pr`'s `read_cached_pr()` returns
`None` only on `FileNotFoundError` / `JSONDecodeError`; `fleet-issue`'s
`cmd_view` is the same shape. A present-but-stale record is indistinguishable
from a current one, and in both trigger lanes (feedback pickup, plan review)
the triggering event postdates the snapshot **by construction**.
FLEET-CACHE.md's producer contract — per-record refresh only when the list
query's `updatedAt` advances — explains the window: scout-tick timing, not a
missing producer mechanism.

### Design: live-first, cache-on-failure

The bounced plan's probe-then-fallback guard is strictly dominated at these
call sites: the fallback is itself one `gh` invocation, so the guard costs 1
call when fresh and 2 when stale, versus 1 unconditional call for always-live
— and the guard additionally needs a comparison field, clock semantics, and a
probe-failure degradation path. Live-first dissolves all three at once.

- **`fleet-pr comments <N>` and `fleet-issue view <N>` fetch from `gh` at
  invocation.** On `gh` failure (nonzero exit — offline, rate-limited), fall
  back to the cached record with a loud stderr warning naming the snapshot.
  One live attempt, no retry.
- **Both paths render through the same code.** `cmd_comments`'s live path
  fetches `gh pr view <N> --json comments,reviews` plus the inline-comments
  endpoint the scout already uses, assembles the same detail-dict shape, and
  feeds the **existing** print loops — `[path:line]` inline items are
  preserved on the live path and the output format is identical across
  live/cached. (Today's miss path `exec`s `gh pr view --comments`, a
  *different* format that drops inline items — so the current fallback already
  violates the one-item-per-output-line checklist contract; this closes that
  too.)
- **`fleet-pr view` / `diff` stay snapshot reads**, with one always-on stderr
  line so a snapshot read is never silent. Display of an in-file timestamp
  only — no freshness computation, no mtime (consistent with the
  `lint_state_mtime.py` ratchet).
- **The per-issue cache becomes the resilience layer, not dead weight.**
  After this change `issues/<repo>/<N>.json` has no primary reader — its role
  is degraded-mode serving when `gh` fails. FLEET-CACHE.md's table says so, so
  the producer doesn't read as write-only.

**#1394 (no polling) holds:** both commands run once per dispatched iteration
— one-shot call sites, not loops. Live-first adds at most two API calls at
those sites and removes zero caching from any polling path.

### Steps

1. `scripts/fleet/fleet-pr` — restructure `cmd_comments` to live-first; on any
   `gh` failure fall back to `read_cached_pr` plus the stderr snapshot warning;
   when both miss, exit nonzero. Update the module docstring.
2. `scripts/fleet/fleet-pr` — always-on stderr snapshot line for `cmd_view` and
   `cmd_diff`, sourced from `_cached_at` (diff: the head SHA in the filename).
3. `scripts/fleet/fleet-issue` — reorder `cmd_view` to live-first: the existing
   fallback block becomes the primary path; the cache read becomes the failure
   path with the same stderr snapshot warning. Docstring likewise.
4. Docs: `FLEET-CACHE.md` (miss-policy paragraph, wrapper-table rows, layout
   rows, the opus-reviewer full-body-recovery note at §"Per-role projections"),
   `FLEET-FEEDBACK-HANDLING.md` § "Reading the feedback",
   `docs/agents/skills/start-next-task.md`, and `fleet-help`'s two index
   entries.
5. Tests in `scripts/fleet/tests/`, using `HOME=$(mktemp -d)` with a crafted
   stale record and a PATH-shimmed `gh`:
   - stale record + shim returning a newer review → the newer review appears
     (**fails on master**: master never invokes `gh` when the record parses);
   - shim exiting nonzero → cached data served AND stderr names `_cached_at`;
   - inline-comment fixture on the live path → the `[path:line]` item renders;
   - the same stale-record pair for `fleet-issue view`, with a `## Plan`
     comment postdating the snapshot (the observed plan-review failure mode);
   - parity assertion: both scripts' failure-path warning shape checked by the
     same test, so the mirrored policies can't drift silently.

### Verification

- Positive control: step 5's first bullet fails against current `master`.
- Live repro of the original window: stamp a comment on a PR the scout has
  already cached, then run `fleet-pr comments <N>` before the next scout tick;
  it appears where today it is silently absent.
- `scripts/fleet/tests/run_all.sh` green otherwise; `ruff check scripts/` clean.
- Confirm `fleet-pr view` / `diff` behavior is unchanged except the stderr
  line, and that `read_cached_pr` has no other caller.

### Gotchas

- **No freshness comparison anywhere** — not `updatedAt` probes, not
  `_cached_at` TTLs, not `st_mtime` (`lint_state_mtime.py` hard-fails new mtime
  freshness reads on the scout cache; `_cached_at` is local wall-clock and
  skew-sensitive). Live-first needs none of them — that is the point of the
  shape.
- One live attempt, then the cache — never retry-until-fresh (#1394).
- Keep the scripts standalone; do not introduce a shared module for ~10 lines
  of policy. The standalone-with-mirrored-helpers architecture is deliberate;
  let step 5's parity test hold the mirror.
- `cmd_comments`'s live path replaces an `exec`-style fallback with in-process
  rendering — preserve exit codes: live failure + cache miss together must exit
  nonzero like today's cache-miss + `gh`-failure path.
- Symlinked installs pick the fix up on merge; panes mid-iteration keep the
  loaded copy (#2768's reload gap) — no immediate fleet-wide effect.

### Acceptance-criteria mapping

- **AC 1** (current-or-flagged, naming snapshot time): live-first = current as
  of invocation; the `gh`-failure path emits the stderr warning naming
  `_cached_at`. Applies to both commands.
- **AC 2** (repro the window): Verification bullet 2, plus the master-failing
  regression test as the durable form.
- **AC 3** (tool and docs agree): step 4 covers every surface that states the
  contract, and names the one gated sentence as a human-applied residual.
- **AC 4** (no new polling loop): one-shot live calls at one-shot call sites;
  the failure path never retries.
