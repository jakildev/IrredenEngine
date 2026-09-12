## Plan: ci/perf — make the perf gate actually gate (revision 2)

- **Issue:** #2817
- **Model:** opus
- **Date:** 2026-08-09

Supersedes the 2026-08-01 plan (kept above as audit trail). Written against `origin/master` @ `5a1fdc9ec`. Each finding from the 2026-08-01 plan-review bounce is addressed with a measurement, not a promise, and the approach is re-committed so that **no phase requires a repo-admin action**.

### Verified current state (all measured this session — fable, macOS, pool-1)

- `.github/workflows/perf-gate.yml` and `scripts/perf/` are unchanged since the bounced plan (`git log origin/master --since=2026-08-01 -- <both>` is empty). Re-verified at today's lines: flat-layout guard `perf-gate.yml:108`, standing-falsehood comment `:110`, seed-new markdown `check_regression.py:98-111`, `resolve_baseline` `compare_perf_runs.py:228-244`, host-mismatch informational return `check_regression.py:130-143`, all four exit-2 paths (`:84`, `:87`, `:93`, `:115`).
- **Push path still 100% failure:** 52 `push` runs in the current 200-run window, 0 success (latest 2026-08-08T19:26Z). 39 reached "Update committed baseline (master push)" and all 39 died at `git push` (GH006). The other 13 are classified, not ignored: 12 July-era `Build IRPerfGrid + ir_ref_bench` failures (build breaks since fixed — every run since 07-23 reaches the push step) and 1 transient apt-install failure (run 30959160922). They never computed a slug, so they don't bias the distribution below.
- **[review finding 1 — measured] The hosted runner pool is heterogeneous: 4 CPU SKUs across 39 runs.** The slug appears in the executed baseline-commit line of every push run that reached the baseline step; harvested from all 39 logs:

  | slug | n | share |
  |---|---|---|
  | `linux-x86_64-epyc-7763-64-unknown` | 19 | 49% |
  | `linux-x86_64-epyc-9v74-80-unknown` | 12 | 31% |
  | `linux-x86_64-xeon-platinum-8573c-unknown` | 7 | 18% |
  | `linux-x86_64-xeon-6973p-c-unknown` | 1 | 3% |

  A single committed baseline therefore gates at best ~half of PR runs, and every miss is silent (host mismatch → informational, `check_regression.py:137-143`). The slug-tolerance decision is made in this plan, not deferred.
- **[new] Branch protection measured via API** (`gh api repos/jakildev/IrredenEngine/branches/master/protection`): `required_pull_request_reviews` enabled with **no `bypass_pull_request_allowances`** — the bot bypass the prior plan proposed as primary was never granted, and master pushes kept failing through 2026-08-08. `master` is the **only** protected branch, and the repo's single ruleset ("Master Restruction") is `disabled`. Load-bearing consequence: the workflow's existing `GITHUB_TOKEN` (`permissions: contents: write`) **can push any non-master ref**.
- Label timeline: the human removed `human:review-plan` on 2026-08-04 (standalone edit, while the issue sat in `fleet:needs-plan`) and did not grant the bypass in the days since. This plan reads that as "commit to an approach that needs no admin action" and does not re-set the label (rationale at the end).
- `git version 2.54.0` on ubuntu-latest (read from run 31274443034's log) — `git worktree add --orphan` (needs ≥ 2.42) is available.
- **Cross-system audit — consumers of `docs/perf/baseline_latest`** (tree-wide literal grep): the workflow (`:105`, `:176`, `:195`); `check_regression.py` / `compare_perf_runs.py`, which take the baseline root as an **argument** (no hard-coded checkout path — local flows pass their own dir, and a dev machine's slug never matches a CI SKU anyway); `docs/perf/README.md:20,30,221-222` (updated by this plan); `ir_hardware_probe.py` docstring (no change); `.fleet/plans/issue-226.md` (historical record, no edit). No other consumer exists, so moving CI baselines off the master tree breaks nothing.

### Scope

One task, one PR, no human-gated phase. Fix both halves of the gate — the reader (flat-layout guard + fail-open exit mapping) and the writer (baseline persistence that branch protection cannot block) — then seed per-SKU baselines and demonstrate the gate failing something, in-PR.

### Approach

**Committed slug decision (finding 1):** keep per-SKU baselines exactly as the writer already lays them out — one `docs/perf/baseline_latest/<slug>/` per observed SKU — and make coverage *observable and seedable* instead of relaxing the slug. Cross-SKU comparison stays informational by design (#1074 decision tree; a 10% threshold is meaningless across EPYC↔Xeon silicon). Rejected: relaxing the slug to os-arch (invalid comparison), nearest-baseline fallback (same problem), pinning a runner SKU (not offered on hosted standard runners). Coverage math from the measured distribution: seeding the top 3 SKUs covers ~97% of runs.

**Committed persistence decision:** baselines live on a dedicated, unprotected **`perf-baseline` branch** — not on master, not in `actions/cache`.
- vs. master commit + bot bypass (prior plan's primary): requires an admin grant that measurably has not happened, and is a policy exception to "Never commit to `master` directly" that the issue itself flags.
- vs. `actions/cache` (prior plan's fallback; review finding 5): caches evict after 7 idle days — a quiet week silently reverts to today's broken state — and the restore semantics were asserted, never measured. A git ref has no eviction, keeps the diffable/auditable history `docs/perf/README.md` wants, and its push mechanism is measured above (only master is protected).
- vs. bot PR per baseline: adds load at the merge-queue constraint.

**Phase 0 — live probe of the one remaining unmeasured mechanism (implementer, first).** Probe both halves cheaply: (a) push an empty orphan commit to `refs/heads/perf-baseline` from the implementation session (worker git creds; ref measured unprotected); (b) once the workflow edit exists on the impl branch, invoke `gh workflow run perf-gate.yml --ref <impl-branch>`. Expected: both accepted. Bail paths — push refused: stop, comment the refusal verbatim (it falsifies the protection measurement), flag for re-plan. Dispatch refused on a non-default ref (workflow_dispatch registration nuance): seeding moves post-merge (dispatch from master once the trigger lands there); pre-merge verification then rests on the hermetic tests and the scratch-PR proof runs immediately post-merge instead — state this in the PR body, do not silently drop it.

**Phase 1 — reader + loudness.**
1. Extract the "Compare head against baseline (PR)" step body into `scripts/perf/ci_compare_step.sh` (env/args: `BASELINE_ROOT`, `HEAD_DIR`, `PR_NUMBER`); the YAML step becomes a thin invocation. This is what makes finding 2's fix testable rather than YAML-trapped.
2. In that script: delete the `[[ ! -f "$BASELINE/manifest.json" ]]` guard (python owns resolution; the reviewer verified the seed-new body at `check_regression.py:98-111` is already comment-shaped — the prior plan's "adjust the wording" hedge is dropped per the review). Keep the `|| STATUS=$?` capture. **New mapping: `STATUS -ge 2` → dump `/tmp/perf_gate_stderr.txt` to stderr and `exit $STATUS`** (step goes red, no PR comment on that path). `STATUS ∈ {0,1}` → comment + `status=` output exactly as today.
3. Close the empty-head-dir hole at its source: "Locate head run directory" fails the step with a message when both `ls` globs miss — it never emits an empty `dir=`. (Finding 2's live example: empty dir → `Path("").resolve()` = cwd → "no cells in head" → exit 2 → silently green today.)
4. Add `'.github/workflows/perf-gate.yml'` and `'scripts/perf/**'` to **both** `paths:` lists.
5. Echo the head slug on the PR path (in `ci_compare_step.sh`, read the same way the push path reads it) — the permanent observability finding 1 asked for; SKU coverage becomes measurable from any PR run's log.
6. Commit the control as `scripts/perf/tests/test_baseline_layouts.py`, extending the pinned script above from the dead bash `-f` test to the real post-fix path: it drives `resolve_baseline` + `check_regression.py` over layout A (empty root) → seed-new exit 0 with the seeding body; layout B (per-slug) → resolves to the slug dir, comparison reached; layout C (legacy flat) → still resolves to the root (**unchanged positive control**); plus an exit-propagation arm running `ci_compare_step.sh` with PATH-shimmed `check_regression.py` and `gh` stubs, asserting exit 2 propagates and no comment is attempted. **Wired to execute** as a cheap early step of the perf-gate job itself (before the build) — an unexecuted spec drifts (#2727), and Phase 1.4 guarantees any PR touching this surface runs it.

**Phase 2 — writer (`perf-baseline` branch).**
7. Add a `workflow_dispatch:` trigger; extend the baseline-update step's condition to `push || workflow_dispatch`.
8. Rewrite the step's tail: instead of committing on master, materialize the branch in a linked worktree — fetch `origin/perf-baseline` and `git worktree add` at its tip (first time: `git worktree add --orphan`; git 2.54 measured), copy the per-slug files + `host.json` sidecar in, commit appending to the fetched tip (keep the `($SLUG) DATE@SHA` message form), `git push origin HEAD:perf-baseline`. **No force push.** Non-fast-forward (two runs racing) → refetch, re-apply, retry once; a second failure stays red (rare, loud, correct).
9. PR path: before invoking the compare script, `git fetch origin perf-baseline` and materialize only `docs/perf/baseline_latest/` from `FETCH_HEAD` into a temp dir via `git archive FETCH_HEAD docs/perf/baseline_latest | tar -x -C "$TMP"` (archive, not `checkout --` — no index mutation); if the fetch fails (branch not yet born), `mkdir -p` the empty root so `resolve_baseline` returns `None` → seed-new. Pass the temp root as `BASELINE_ROOT`.
10. `docs/perf/README.md`: update `:20`, `:30`, `:221-222` — baselines on the `perf-baseline` branch, per-SKU dirs, dispatch seeding, coverage-by-SKU expectation, and the carried-over "first comparisons may be noisy; `--regress-pct` tuning is follow-up" caveat.

**Phase 3 — seed + prove (implementer ops, in-PR).**
11. Seed: `gh workflow run perf-gate.yml --ref <impl-branch>` repeatedly — engine code on the impl branch is identical to master (the PR touches only workflow/scripts/docs), so the numbers are valid master baselines; state that in the PR body. Each dispatch lands on a random SKU and commits that SKU's baseline. Stop when the top-3 measured SKUs (~97% share) are covered, or after 8 dispatches, whichever comes first; report achieved coverage.
12. Prove the red path end-to-end: scratch branch off the impl branch adding `std::this_thread::sleep_for(std::chrono::milliseconds(2))` to a per-frame stage IRPerfGrid exercises — named site: the tick in `engine/prefabs/irreden/render/systems/system_entity_canvas_to_framebuffer.hpp` (inside the `engine/prefabs/irreden/render/**` path filter). Open as a **draft** PR, let perf-gate run; if the echoed slug isn't covered, re-run until it is (expected ≤ 2 attempts at 80% top-2 coverage). Assert the "Fail check on regression (PR)" step **ran and failed** — the red must come from the regression step, not an infra error; check which step fired. Then close the draft PR and delete the scratch branch.

### Affected files

- `.github/workflows/perf-gate.yml` — `workflow_dispatch` trigger, both `paths:` lists, head-dir guard, thin compare-step invocation, layout-test step, rewritten baseline-update step (branch push).
- `scripts/perf/ci_compare_step.sh` — new; extracted PR-compare logic including the exit-2 mapping and slug echo.
- `scripts/perf/tests/test_baseline_layouts.py` — new; three-layout control + exit-propagation arm.
- `docs/perf/README.md` — branch-based layout, seeding, coverage docs.
- `scripts/perf/check_regression.py` — **no change** (per the review's verification).

### Acceptance criteria (phase-tagged; every gating criterion implementer-executable)

- **[P1, hermetic]** `python3 scripts/perf/tests/test_baseline_layouts.py` passes, and the impl PR's own perf-gate run shows the test step executed. Cases: A → seed-new; B → per-slug resolved, comparison reached; **C → legacy flat still resolves (positive control unchanged)**; D → stubbed exit-2 propagates out of `ci_compare_step.sh` with no comment attempted.
- **[P1, hermetic]** The literal string `no committed baseline yet — skipping comparison` appears nowhere in `.github/workflows/perf-gate.yml` or `scripts/perf/` — fires at base (present at `perf-gate.yml:110` today), inverts on the fix. (Replaces the prior plan's unsatisfiable AC about already-posted comments on PR #2648.)
- **[P2, live]** ≥ 1 `perf-gate.yml` run with event `push` or `workflow_dispatch` and conclusion `success` (baseline: 52/52 failures in the current window, 0 successes all-time), AND `git ls-tree origin/perf-baseline -r --name-only` shows `docs/perf/baseline_latest/<slug>/manifest.json` + `host.json` for ≥ 1 slug.
- **[P3, live]** On the impl PR after seeding, a perf-gate run posts the comparison table (not seed-new) and its echoed slug matches a seeded dir — the gate demonstrably compared.
- **[P3, live]** On the scratch draft PR: `gh run view <id> --json jobs --jq '.jobs[].steps[] | select(.name | test("Fail check")) | .conclusion'` → `failure`, i.e. the step **ran** (baseline today: `skipped`, run 30685563241) and the check is red. Demo torn down (draft closed, scratch branch deleted) before the impl PR leaves review.
- **[post-merge observable, non-gating]** The next organic master push touching a perf path produces a green `push` run — the same code path the dispatch already proved, so it is a watch item in the PR body, not a merge blocker. The impl PR carries `Closes #2817`.

### Gotchas

- (carried) The two defects mask each other — a green PR run proves nothing until that run's SKU has a baseline. Use the slug echo and the layout tests as the discriminating signals, never run color.
- `workflow_dispatch` on a non-default ref has a registration nuance — phase 0's bail ladder covers it; do not let it silently degrade into "merged unverified".
- Dispatch-seeding is only valid from refs whose **engine code equals master's** — rebase the impl branch before seeding if master moved.
- Never `--force` push `perf-baseline`; history is the audit trail. Retry-once on non-FF races.
- Do not add `continue-on-error` anywhere in this workflow — it converts loud red into today's silent variety.
- `check_regression.py`'s seed-new text ("The next master push from this host will seed one") becomes *true* under this plan — leave it. The cosmetic double header ("## Perf gate" + "# Perf gate — seeding…") is out of scope.
- First real comparisons may be noisy (single quick-matrix baselines); threshold tuning is explicit follow-up, not this issue.
- `[skip ci]` in the baseline commit message stops being load-bearing (nothing triggers on `perf-baseline`) — keep or drop, but don't rely on it.

### Review-findings ledger (2026-08-01 bounce → this revision)

1. **Slug stability:** measured (4 SKUs / 39 runs, table above); decision committed — per-SKU baselines + dispatch seeding + permanent slug echo.
2. **Exit-2 fail-open:** fixed (Phase 1.2–1.3), made testable by the script extraction, and executed (AC case D).
3. **Phase-tagged ACs / human-gated phase:** no phase needs a human; ACs are tagged; `Closes #2817` is correct.
4. **AC 5 unsatisfiable:** restated hermetically against the artifact.
5. **Fallback restore unmeasured:** the cache fallback is abandoned; the branch mechanism's one live premise gets a phase-0 probe with explicit bail paths.

`human:review-plan` is deliberately **not** re-set: the timeline shows the human removed it on 2026-08-04 while this issue sat in `fleet:needs-plan`, and the revised approach leaves no admin decision open (the bypass question is moot — nothing pushes to master). If the human prefers the master-committed-baseline design after all, `human:revise-plan` is the lever.

— worker (fable, macOS, pool-1)
