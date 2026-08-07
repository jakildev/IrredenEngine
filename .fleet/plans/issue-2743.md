## Plan: fleet-state-scout open-PR list capped at gh's default --limit 30

- **Issue:** #2743
- **Model:** opus — one-line surface, but it changes a shared fleet-infra data source every role reads, in a subsystem with explicit GitHub-quota discipline
- **Date:** 2026-07-31

### Verified current state

- `_fetch_prs_graphql` (`scripts/fleet/fleet-state-scout:263-266`) invokes `gh pr list --repo <r> --state open --json …` with no `--limit`. `gh pr list --help` documents `-L, --limit int  Maximum number of items to fetch (default 30)`.
- Measured 2026-07-31 13:30Z: 35 open engine PRs live, 30 in `state.json` (cache 25s old). Dropped set is the 5 lowest numbers — #2393, #2475, #2585, #2607, #2608 — confirming a newest-N-by-number window, not a staleness artifact.
- The change-detector in `fetch_prs` (`:322`) already passes `per_page=100` and its comment reasons explicitly about covering *"every open PR (REST defaults to 30)"*. The cap was closed on the detector and left open on the fetch it gates: the detector correctly observes all 35 change, then the GraphQL fetch retains 30.
- Downstream confirmation: `enrich_inflight_pr_tasks` (`:1841`) matches on `headRefName`; #2376 projected `inflight_pr: null` / `owner: free` while open PR #2607 (head `claude/2376-implicit-grid-rotation`, `Closes #2376`, `fleet:approved`) held the finished work. The predicate would have matched — the record was absent. This pane claimed #2376 off the queue on that projection.
- Other `repos.*.prs[]` readers inheriting the narrowing: `enrich_stackable_blocker_prs` (`:1721`), `_semantic_conflict_claimable`, the per-role slices built at `:2596`, and role-worker steps 1 / 1c.
- Existing test harness: `scripts/fleet/tests/` (106 entries), including `test_enrich_inflight_pr_tasks.py` — the natural home for the regression check.

### Scope

Give `_fetch_prs_graphql` an explicit, named limit well above the plausible open-PR ceiling, **plus a truncation guard** so the silent-narrowing class cannot recur unobserved. One function, both repos (`fetch_prs` is called per repo, so engine and game are fixed by the same change).

Rejected alternatives:
- *Leave it and widen the matching predicate instead* (the #2672 / #2507 direction) — no predicate can match a record that was never fetched. Those issues remain independently valid; this is upstream of both.
- *Migrate the open-PR list to paginated REST* — `fetch_prs`'s own comment documents why this read stays on GraphQL (it needs `reviews` + `mergeable`, which the REST list omits). Out of scope.
- *Raise to exactly 100* — one page, but it re-creates the same silent cliff at a higher number. The guard below is what actually makes the failure loud; the limit only sets where.

### Approach

1. `scripts/fleet/fleet-state-scout` — add a module constant next to the other fetch tunables:
   ```python
   # gh pr list defaults to --limit 30 and silently truncates past it, dropping
   # the OLDEST open PRs — exactly the long-lived stuck ones (#2743). This is the
   # fleet's authoritative in-flight signal; every role's projection reads it.
   OPEN_PR_FETCH_LIMIT = 200
   ```
2. Pass it in `_fetch_prs_graphql`: `"--limit", str(OPEN_PR_FETCH_LIMIT),`.
3. Truncation guard — immediately after the `json.loads` succeeds, before the per-PR normalization loop:
   ```python
   if len(prs) >= OPEN_PR_FETCH_LIMIT:
       log(f"gh pr list {repo}: returned {len(prs)} PRs at the "
           f"{OPEN_PR_FETCH_LIMIT} cap — open-PR list may be truncated; "
           f"raise OPEN_PR_FETCH_LIMIT")
   ```
   `>=` not `==` so it still fires if the constant is later lowered below the live set. This is the piece that converts a silent narrowing into an operator-visible one; do not drop it as "defensive noise".
4. Regression test in `scripts/fleet/tests/test_enrich_inflight_pr_tasks.py` (or a sibling `test_fetch_prs_limit.py` if the existing file's fixtures don't reach the fetch layer): assert `_fetch_prs_graphql` passes `--limit` in its argv, and assert the #2376 shape — a task whose issue number appears in an open PR's `headRefName` gets a non-null `inflight_pr` when that PR is the 31st-or-older record.

### Affected files

- `scripts/fleet/fleet-state-scout` — one constant, one argv pair, one guard (~10 lines)
- `scripts/fleet/tests/test_enrich_inflight_pr_tasks.py` (or a new sibling) — regression coverage

### Acceptance criteria

- **Positive-fire, and it must be run both ways:** with >30 open PRs live,
  `python3 -c "import json,os;d=json.load(open(os.path.expanduser('~/.fleet/state/state.json')));print(len(d['repos']['engine']['prs']))"`
  equals `gh pr list --repo jakildev/IrredenEngine --state open --limit 200 --json number --jq length` on a fresh scout tick. This assertion **FAILS on master today** (30 vs 35) — record both the pre-fix red and the post-fix green in the PR body; a post-fix-only green does not distinguish the fix from a shrunken PR set.
- #2376-shaped unit regression passes; it fails with the `--limit` argument reverted.
- The truncation guard fires when `OPEN_PR_FETCH_LIMIT` is temporarily set below the live open-PR count, and is silent at 200.
- `sample_github_rate_limit`'s latched utilization shows no material regression across ~3 consecutive ticks. Expected: unchanged request count — gh pages at `min(limit, 100)` per request and stops once the result set is exhausted, so 35 open PRs cost one request at either limit; only the payload grows.

### Gotchas

- **Widening the list widens every consumer at once.** Roles that previously never saw #2393 / #2475 / #2585 will start seeing them. Those three carry `fleet:design-unblocked` (x2) and `fleet:needs-human` — all currently parked for reasons documented on the PRs. Expect a dispatch-pressure bump on the first post-fix tick as long-buried PRs re-enter the projections; that is the fix working, not a regression. Do **not** "fix" it by re-narrowing.
- The projection/trigger hashes will flip once on the first tick carrying the newly-visible PRs. One-time churn, not a loop.
- Do **not** touch `fetch_closed_fleet_queued(repo, limit=100)` or `fetch_issues_by_label`'s `per_page=30` — both are deliberate, documented windows chosen to keep the queue-ingest set byte-identical. Only the open-PR list is the unintended cap.
- CI is Linux; this is Python within an existing harness, so no BSD/GNU portability surface — but run the touched test file, not just a macOS-local scout tick.

### Sibling / in-flight reconciliation

Checked all 35 open engine PRs' file lists (2026-07-31). Three touch `scripts/fleet/fleet-state-scout`: **#2709** (needs_gl_host inference from source paths), **#2657** and **#2656** (stacked-PR machinery retirement, phases 2–3). None touch `_fetch_prs_graphql` or `fetch_prs`; #2709's scout hunks are in the GL-host backstop path and it adds its own test file. No conflict expected. Single task, single PR — no stack needed.

---

## Plan-review refinement (opus-reviewer, pool-7, 2026-08-01) — BINDING

The plan was reviewed SOUND with one binding change to the acceptance section,
carried here so the implementer does not have to reconstruct it from the thread.

The **unit** test as originally specified does not do what the plan claims:
`enrich_inflight_pr_tasks` receives `prs[]` directly, so a fixture of 31+ PRs
passes at **any** fetch limit — that assertion cannot go red when `--limit` is
reverted. The only part that inverts is `assert "--limit" in argv`, which is a
change-detector on the fix's own artifact, not a behavioural control (the trap
#2723 was filed for).

Make the fetch-layer test **behavioural** instead — stub `run_capture` with a
fake that **honors** `--limit` the way `gh` does:

```python
_FIXTURE = [_pr(n) for n in range(1000, 1040)]   # 40 open PRs

def _fake_gh(argv):
    limit = int(argv[argv.index("--limit") + 1]) if "--limit" in argv else 30
    return json.dumps(_FIXTURE[:limit])          # gh's newest-first truncation

with patch.object(_mod, "run_capture", side_effect=_fake_gh):
    prs = _mod._fetch_prs_graphql("jakildev/IrredenEngine")
self.assertEqual(len(prs), 40)
```

Red on master (no `--limit` → fake returns 30), green with the fix, and it
discriminates on *behaviour* rather than on the presence of the flag. Precedent
for the module-load + `patch.object` shape is
`scripts/fleet/tests/test_scout_agent_approved_fetch.py:16-20` and `:45`.

Keep the `#2376`-shape enrich assertion — it is good documentation of the
downstream harm — but state it as **coverage**, not as the control that inverts.

Model stays **opus**, settling the `#2742` consolidation's open divergence.

---

## Implementer's note (2026-08-07) — the live control decayed before pickup

Everything above is the plan as reviewed, kept verbatim. One acceptance
criterion no longer reproduces and must not be read as a live claim:

The **live** positive control ("FAILS on master today — 30 vs 35") depended on
the engine having >30 open PRs. The merge queue drained between plan review and
implementation: at pickup there were **16** open engine PRs and **13** game,
both under gh's default window, so master and the fix return the same list and
the live assertion is **vacuous today in both directions**. It is not evidence
of anything, and a green post-fix run of it would not distinguish the fix from
the shrunken PR set — exactly what the criterion warned about, arriving from the
other side.

The control that does invert is the **hermetic** one the plan-review refinement
specified: `scripts/fleet/tests/test_fetch_prs_limit.py` stubs `run_capture`
with a fake honoring `--limit` as gh does, and goes red on the pre-fix tree.
Two further arms cover what a stub cannot: the flag is driven against the
**real** `gh` binary (proving it is accepted in that argv position — the #2781
hazard), and the request count is measured directly rather than via the shared
rate-limit bucket, which concurrent fleet daemons make unusable. Evidence is in
the PR body.

