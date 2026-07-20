## Plan: fleet/scout: stack-base guard validates base ancestry (offer + accept)

- **Issue:** #2447
- **Model:** opus
- **Date:** 2026-07-19

### Scope

Extend the shared stack-base predicate so both surfaces — the scout's offer
(`enrich_stackable_blocker_prs`) and `fleet-claim claim --stackable-on`'s
accept — reject a base whose head is missing the squash commit of a merged
blocker anywhere in the base's blocker ancestry, with a reason naming the
missing ancestor. Fixes the churn loop where a provably-unbuildable base is
offered, claimed, released, and re-offered every tick, defeating the
dispatcher's go-quiet gate (`_only_unclaimable_tasks`).

### Verified current state (all premises measured, 2026-07-19)

**Code (read this session):**

- `fleet_stack_base.unsafe_base_reason()` (`scripts/fleet/fleet_stack_base.py:68`)
  checks labels + empty-diff only. No ancestry input exists in the signature.
- `enrich_stackable_blocker_prs` (`fleet-state-scout:1541`) runs **after**
  `resolve_blocked_by` (`:1324`), which **mutates** `task["blocked_by"]`,
  dropping merged refs. The merged-ancestor identity the check needs is
  destroyed before the enrich runs — this is the "collapsed" data loss and it
  also means `resolve_blocked_by` is the natural place to stash the removed
  refs. Note `resolve_blocked_by` iterates `tasks.open` **only**; ancestor
  records sitting in `tasks.in_progress` keep their raw (uncollapsed)
  `blocked_by`.
- `fetch_recent_merged_prs` (`fleet-state-scout:529`) pulls REST `/pulls`
  (state=closed) and projects number/title/headRefName/baseRefName/mergedAt.
  The raw payload already carries `merge_commit_sha` — **verified live**:
  `gh api 'repos/jakildev/IrredenEngine/pulls?state=closed&…&per_page=3'`
  returns e.g. PR #2445 → `merge_commit_sha: 2200a82…`, which is the current
  master tip commit. Projecting it costs zero extra API calls.
- The scout's open-PR list projection (`prs[]`) carries no `headRefOid`; the
  per-PR detail cache does (`fleet-state-scout:850`). The list poll's raw
  payload includes the head oid, so projecting it into `prs[]` is also free.
- `fleet-claim`'s blocker check already fetches merged PRs live
  (`fleet-claim:1210`: `gh pr list --state merged --limit 30 --json
  headRefName` + `branch_matches_issue`). Adding `number,mergeCommit` to that
  same `--json` list yields the accept-side sha in the existing single call.

**Live repro (the downstream private repo's active instance, anonymized per
cross-repo isolation — chain T1→T2→T3→T4, T4 offered T3's head as base):**

- `git merge-base --is-ancestor <T1-squash> origin/<T3-head>` → **false**;
  same sha vs `origin/master` → **true**. The hole is real and live today.
- GitHub compare API on the same pair:
  `compare/<T3-head-oid>...<T1-squash>` → `status: diverged, ahead_by: 6`.
  Positive control (the base's fork-point sha, known-contained) →
  `status: behind, ahead_by: 0`. So the API verdict rule is
  **contained ⇔ `ahead_by == 0`** (status `behind` or `identical`), and it
  agrees with local `git merge-base --is-ancestor` on both the negative and
  the positive case. The compare route needs no local refs, which matters
  because the scout never fetches.
- T3's issue's raw `Blocked by:` names two entries (T1 merged, T2 open); the
  cached task record shows it collapsed to the single open one — confirming
  the walk must recover merged refs from the pre-collapse parse.

**Cost (measured on the live queue):** engine has 0 candidate offers this
tick, the downstream repo has 1 (the churn instance) with 1 merged frontier
ancestor, whose PR is inside the 30-entry `recent_merged_prs` window (sha
free). Cold cost = 1 compare call per new (offer × merged ancestor) pair,
memoizable forever on the immutable `(base-head-oid, sha)` key → steady-state
**0 added API calls per tick**. No per-offer `git fetch` is needed anywhere.

### Approach

**1. Pure logic in `fleet_stack_base.py` (shared, injected callables).**
Add `missing_ancestor_reason(base_issue, get_blocker_refs, ref_merged,
merged_sha, contains, max_depth=8)`:

- Walk same-repo blocker refs from the base PR's issue: recurse through
  **open** ancestors, collect refs classified **merged** (the frontier).
  Merged nodes are checked, never recursed: squash-merge history on master is
  linear, so if frontier ancestor C's squash is contained in the base head,
  everything merged before C is too — a deeper hole cannot exist without a
  frontier miss.
- For each frontier ancestor C: `merged_sha(C)` then `contains(sha)`.
  `False` → return `"missing merged ancestor #C"`.
- Fail closed: any `None` from a callable (record unknown, cross-repo ref,
  sha unresolvable, verdict unavailable), a depth-cap hit, or a cycle
  (visited set) → return an `"ancestry undeterminable (<why>)"` reason.
  Rationale: a suppressed offer degrades to the safe pre-stackable status quo
  (task waits for the merge), while fail-open reproduces the guaranteed-no-op
  dispatch loop this issue exists to kill. The #2442-style silent-withholding
  hazard is handled by logging, not by failing open (below).

**2. Scout (offer side).**

- `resolve_blocked_by`: before collapsing, stash the removed refs as
  `task["collapsed_blockers"]`. Shared helper for "is this ref merged"
  (closed-issue window ∪ merged-head branch match — the exact sets resolve
  already uses) so the walk can classify raw refs on `in_progress` records
  that resolve never visited.
- `fetch_recent_merged_prs`: project `mergeCommitSha` (only when `merged_at`
  is set — REST's `merge_commit_sha` on an unmerged PR is a test-merge sha,
  not usable).
- Open-PR list projection: add `headRefOid`.
- `enrich_stackable_blocker_prs`: after the existing `unsafe_base_reason`
  filter, run `missing_ancestor_reason` with in-memory callables; `contains`
  goes through the compare API via the scout's conditional-fetch layer, with
  a persistent memo (JSON beside the existing scout caches) keyed
  `(base_head_oid, sha) → bool` — immutable, so it never invalidates; a base
  rebase changes the oid and naturally re-keys. On a reason: skip the offer
  **and `log()` it** (task, base PR#, reason) — suppressions must be
  observable, never silent (the #2442 lesson).

**3. `fleet-claim` (accept side).** In the `--stackable-on` block after the
label/diff check: rebuild the walk live in an inline `python3` block
(imports `fleet_stack_base` + `fleet_blocked_by` via `FLEET_LIB_DIR`) —
`gh issue view` per open ancestor for raw `Blocked by:`, the existing merged
`gh pr list` call extended with `number,mergeCommit` for the frontier shas,
and `git fetch origin <base-branch>` + `git merge-base --is-ancestor` for
containment (claim has a repo checkout; measured agreement with the compare
API above). Refusal mirrors the existing message:
`"<pr> is not a stackable base (missing merged ancestor #C) — refusing claim"`.
Offer and accept agree by construction — both route the decision through the
same pure function.

**4. Tests (hermetic, per `scripts/fleet/CLAUDE.md` — mock misses raise,
tempfile cache dirs, no live GitHub, no live `~/.fleet`).**

- `tests/test_stack_base_guard.py`: unit tests for
  `missing_ancestor_reason` — the acceptance pair (base whose *parent's*
  collapsed blocker merged after the fork → rejected naming the ancestor;
  same base with `contains=True` → offered); hole two levels up (the
  T5-shape); candidate's own collapsed set empty while the hole is upstream
  (the #2447 signature); undeterminable inputs fail closed; cycle + depth
  cap terminate.
- `tests/test_enrich_stackable_blocker_prs.py`: existing cases stay green
  (fixtures gain the new windows/fields as needed); new: offer suppressed +
  log line emitted on a not-contained verdict; warm memo → zero fetcher
  calls (pins the steady-state-0 cost); missing `headRefOid`/sha →
  suppressed as undeterminable, logged.
- `tests/test_fleet_task_class.py` (integration, the addendum's A/B): run the
  enrichment over a slice shaped like the forensics (sole non-terminal entry
  rides the invalid base) and assert the resolver returns **`defer`**; flip
  the verdict to contained and assert it dispatches. Positive-fire in both
  directions; pins the observable go-quiet behavior (#1726/#1998 lineage).
- `tests/test_fleet_claim_stackable_live_resolve.sh`: mocked `gh`/`git` —
  refusal message names the ancestor; contained case proceeds to claim.

### Affected files

- `scripts/fleet/fleet_stack_base.py` — `missing_ancestor_reason` + walk
  (pure, injected callables)
- `scripts/fleet/fleet-state-scout` — resolve stash + merged-ref helper;
  `mergeCommitSha` + `headRefOid` projections; enrich integration; persistent
  compare-verdict memo; suppression `log()`
- `scripts/fleet/fleet-claim` — `--stackable-on` ancestry gate
- `scripts/fleet/tests/test_stack_base_guard.py`
- `scripts/fleet/tests/test_enrich_stackable_blocker_prs.py`
- `scripts/fleet/tests/test_fleet_task_class.py`
- `scripts/fleet/tests/test_fleet_claim_stackable_live_resolve.sh`

One task, one PR (the offer and accept must land together — that is the
module's contract). No epic.

### Acceptance criteria (positive-fire)

1. Unit: a base missing a transitively-merged ancestor (hole ≥1 level above
   the candidate's own blocker list) is rejected with a reason naming the
   ancestor; the same base containing that squash is offered.
2. Agreement: both surfaces call `missing_ancestor_reason`; the claim-side
   test refuses the same fixture base the scout declines to offer.
3. `resolve(slice) == "defer"` when the slice's only claimable entry rode the
   invalid base — and dispatches when the base is valid (the A/B fires both
   ways).
4. Enrich test proves a suppression **logs** (count > 0 in captured output) —
   no silent withholding.
5. Warm-memo enrich performs zero network-fetch calls (steady-state cost
   pinned at 0 per tick).
6. `tests/test_enrich_stackable_blocker_prs.py` fully green.

### Gotchas

- Compare-API verdict is **`ahead_by == 0`** (`behind` *or* `identical`) —
  do not test `status == "behind"` alone or read `behind_by`.
- `merge_commit_sha` is only meaningful when `merged_at` is set.
- `resolve_blocked_by` visits `tasks.open` only — never assume the
  `collapsed_blockers` stash exists on an `in_progress` record; classify raw
  refs on demand there.
- The ancestry inputs deliberately avoid PR **bodies** (issue-side graph +
  branch-match + oids only) so #2442's 304-body-stripping cannot corrupt this
  check.
- Keep downstream-repo identifiers out of code comments and commit text —
  reference #2447 itself for the incident (public-repo isolation).
- New `gh`/`git` call sites in `fleet-claim` are covered by the sourced
  `fleet-net.sh` timeout shadows — do not add raw unguarded calls.
- Cross-repo blocker refs in the walk: classify as undeterminable (fail
  closed + log), matching `resolve_blocked_by`'s same-repo-only window.

### Sibling / in-flight reconciliation

- **#2442** (open, unclaimed): same function, different stage (the
  Closes-#N *match*; this plan filters *after* the match). Merge-order
  agnostic; this plan's body-independence note above is the interaction
  guard.
- **#1531**: multi-blocker stacking stays deferred (v1 Q3) — this plan
  implements transitive ancestry for the single-blocker eligible set only.
  The 2026-07-15 transitive-closure comment there is subsumed for that set.
- **#1726 / #1998**: the defer A/B test pins the go-quiet gate those issues
  established; no dispatcher-side change (the addendum's resolver audit
  already ruled the dispatcher correct).
- No open PRs touch `scripts/fleet/` (checked both repos' open-PR lists this
  session).

