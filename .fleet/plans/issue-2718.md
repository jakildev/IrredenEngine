<!--
Plan file for issue #2718, committed as the first commit of its implementing
PR per #1932. The canonical plan is the `## Plan` comment on the issue,
reproduced verbatim below, followed by the opus plan reviewer's binding
constraints and this implementation's notes.
-->

## Plan: ci: quality.yml has been disabled_manually since 2026-04-11 — format, lint, ruff, and ctest gates are all dormant

- **Issue:** #2718
- **Model:** opus
- **Date:** 2026-08-08

### Verified current state (all measured this session, 2026-08-08, macOS pool-2)

- `gh workflow list --all`: **Quality `disabled_manually`**; the other five workflows (Auto re-review, Perf Gate, Fleet Tests, Header Checks, Render Harness Tests) are `active`.
- **quality.yml has never been green.** All **32** retained runs — the full record the API returns, 2026-02-17 → 2026-04-11 — concluded `failure`; zero successes exist. This is not "restore a formerly-working gate"; the umbrella never once passed.
- Last run (24290676162): only failing job/step is `quality-checks` / **Configure** (`cmake --preset windows-debug`). Logs are HTTP 410 (expired), so the exact error is unrecoverable. What the preset shows is enough: `windows-debug` uses the **"MinGW Makefiles"** generator, and the workflow installs only cmake + ninja + LLVM — no MinGW toolchain step — so it only ever worked if the `windows-latest` image happened to ship one on PATH. Under the approach below the Windows job is retired, so deeper archaeology buys nothing (issue AC 1 offers "or split the Windows job out" as the alternative — this plan takes it).
- The `Python lint (ruff)` and `lint_state_mtime.py` steps were added to quality.yml **months after the disable** (#2053/#2133/#2291 vs disabled 2026-04-11) — **they have never executed in CI at all**.
- Probes on the current tree (this host):
  - `ruff check scripts/` at **0.15.20 — exactly the CI pin**: exit 0, "All checks passed!". A ruff gate lands green today.
  - `python3 scripts/fleet/lint_state_mtime.py scripts/fleet/`: exit 0. Same.
  - Tree-wide format check (script-mode `cmake/run_clang_format.cmake`, `FORMAT_MODE=check`, local clang-format 22 vs the CI pin of 18 — indicative, not authoritative): **152 of 813 collected files fail**. This is not only version skew: the repo's enforced discipline is **changed-lines** formatting (`format-changed`, #2719 — "a touched file's pre-existing clang-format drift stays out of your diff"), so whole-file cleanliness was never the maintained invariant. quality.yml's tree-wide `format-check` step was red-by-design for this repo.
  - `IrredenEngineTest` is **red on master**: `SaveTrait.InventoryIsComplete` (169 vs 167), tracked as #2834 (open), measured 2026-08-04 on master `34c7f7f46`. A Linux ctest gate cannot land green until #2834 resolves.
- Reference census (`fleet-rules-sweep --pattern 'quality\.yml'`: 17 matches / 11 files; note a plain repo-top `rg` missed 7 of them — #2739 walker false-clean):
  - **3 live docs falsely assert the gate is live**: `docs/agents/BUILD.md:213`, `docs/agents/skills/commit-and-push.md:172`, `ruff.toml:7`.
  - **5 accurate "disabled (#2718)" citations that go stale on resolution**: the header comments of `fleet-tests.yml`, `header-checks.yml`, `render-harness-tests.yml`, plus `cmake/run_header_checks_standalone.cmake:24` and `.claude/rules/cpp-globals.md:69`.
  - 3 historical `.fleet/plans/issue-*.md` records — at-rest artifacts, leave untouched.
- Branch protection on master has **no required status checks** (and the sole ruleset is disabled) — deleting the workflow cannot strand any open PR.

### Approach — decompose and retire (committed)

Retire quality.yml and restore each dormant gate as a focused standalone workflow, per-gate path-filtered, on Linux runners. **Not** re-enable-the-monolith, for four measured reasons: (1) it has no green history to restore; (2) a monolith gates every check on its slowest, most fragile leg — the Windows Configure break is exactly what took format/ruff/ctest down with it; (3) a monolith cannot path-filter per gate, so every docs PR would pay two full engine builds; (4) the repo already voted — three standalone workflows (`header-checks`, `fleet-tests`, `render-harness-tests`) exist precisely because steps added to quality.yml were inert (#2794, #2712, #2825), and the standalone pattern is proven (header-checks green in 11s). Windows CI coverage is not lost: the fleet's native Windows host lane (`fleet:needs-windows-smoke` / platform-catchup) builds `windows-debug` with a real MSYS2 toolchain, which the CI runner never had.

**This diverges from the issue's written AC 2–3** ("quality.yml runs green … is re-enabled"). The intent — the dormant gates gate again — is preserved; the letter is not. That call, plus the issue body's own "needs a human decision on whether to re-enable at all", is why this plan carries `human:review-plan`.

Phased: **this issue implements phase A only**; B–D are follow-up filings with the design decisions recorded here so they file bounded.

**Phase A (this issue, one PR):**

0. Re-run the two green probes at HEAD (`ruff check scripts/` with 0.15.20; `python3 scripts/fleet/lint_state_mtime.py scripts/fleet/`). Expected: exit 0 both. Bail: if a new violation landed since this measurement, fix it in the same PR if it is in-scope-trivial; otherwise stop and flag for re-plan — do not land a red gate.
1. Add `.github/workflows/python-lint.yml`: `ubuntu-latest`; step 1 `astral-sh/ruff-action@v3` pinned `0.15.20`, `args: "check scripts/"`; step 2 `python3 scripts/fleet/lint_state_mtime.py scripts/fleet/`. Triggers: `push` to master + `pull_request`, both path-filtered to `scripts/**`, `ruff.toml`, and the workflow file itself, plus `workflow_dispatch`; `permissions: contents: read`. Header comment mirrors the siblings' rationale (subject-filtered, hermetic, no compiler), citing this issue.
2. `git rm .github/workflows/quality.yml`. The `disabled_manually` entry disappears from the workflow list with the file; no `gh` mutation needed.
3. Doc sweep (the 8 live references above): point the 3 false "gated in CI (quality.yml)" claims at `python-lint.yml`; reword the 5 "disabled … (#2718)" citations to "retired (#2718)" — for `.claude/rules/cpp-globals.md:69` the accurate new statement is that `lint`/clang-tidy currently has **no** CI path (cite the phase-D spike issue number once filed). Do not touch `.fleet/plans/` records.
4. File the follow-ups (B, C via the agent-approved lane per TASK-FILING.md — both are bounded with the decisions below recorded; D as an unlabeled **investigation spike** for human triage). B and C touch disjoint files, so they file as siblings, each `Blocked by:` #2718 to serialize the `.github/workflows/` + docs surface behind A's retire commit; C additionally `Blocked by:` #2834.

**Phase B (follow-up, opus): PR-scoped changed-lines format gate.** Decision recorded now: **changed-lines, not tree-wide** — a tree-wide gate is red-by-design (152/813 above) and would demand exactly the whole-tree reformat #2719 was built to avoid. Shape: a standalone entry that parameterizes `run_clang_format_changed.cmake`'s diff base to the PR's merge-base with its base branch, runs it in fix mode, then fails on `git diff --exit-code`. Pin the clang-format version in the workflow; version noise is bounded to the author's own changed lines.

**Phase C (follow-up, opus, Blocked by #2834 and #2718): `engine-tests.yml`.** Reuse quality.yml's `linux-build` job body verbatim (apt deps, `cmake --preset linux-debug`, build `linux-tests`, `ctest --preset linux-default-tests`, build `IRShapeDebug`), path-filtered to the C++/CMake surface. Premise gate: #2834 must land first (the suite is red on master by a known, filed cause). Note this job has **never run** — its first CI execution will likely need runner-side debugging, which is why it stays opus.

**Phase D (follow-up, investigation spike, unlabeled):** whether clang-tidy can gate at all here — it needs a full configure for `compile_commands.json`, has never run green, and its violation count is unmeasured. Outcome decides between a tidy workflow, a changed-files tidy gate, or documenting no-CI-path as accepted.

### Affected files (phase A)

- `.github/workflows/python-lint.yml` — new (ruff + mtime lint, path-filtered)
- `.github/workflows/quality.yml` — deleted
- `docs/agents/BUILD.md` — ~line 213: ruff gate now `python-lint.yml`
- `docs/agents/skills/commit-and-push.md` — line 172: same (canonical shared-flow doc, not the gated `.claude/skills` wrapper)
- `ruff.toml` — line 7 comment: same
- `.github/workflows/fleet-tests.yml`, `.github/workflows/header-checks.yml`, `.github/workflows/render-harness-tests.yml` — header comments: "disabled" → "retired (#2718)"
- `cmake/run_header_checks_standalone.cmake` — line 24 comment: same
- `.claude/rules/cpp-globals.md` — line 69: `lint` has no CI path (prose only; the fenced Detection blocks must stay byte-identical — `test_lint_rules_commands.py` checks them)

### Acceptance criteria (phase A — positive-fire)

1. A `python-lint.yml` run appears on the PR and is green. Fires by construction: the workflow file itself and `ruff.toml` are in the diff and in the path filter.
2. **Negative control observed**: one temporary commit adding a deliberate ruff violation to a `scripts/` file (e.g. an unused import) → the run goes red; drop the commit → green. The fixture is the PR branch itself.
3. `gh workflow list --all` no longer lists Quality; `quality.yml` absent from `origin/master`.
4. Post-merge `fleet-rules-sweep --pattern 'quality\.yml'` (repo-top `rg` is not acceptable here — #2739): remaining matches are only historical `.fleet/plans/` records and this issue's own artifacts; no live doc asserts the retired gate.
5. Follow-ups B/C/D exist with the recorded decisions and `Blocked by:` lines as specified.

### Gotchas

- `fleet-tests.yml` triggers on `docs/agents/**` and `.claude/rules/**`, so phase A's doc edits will run it — keep `cpp-globals.md`'s fenced blocks untouched (prose edit only) or `test_lint_rules_commands.py` will flag the drift.
- `docs/agents/skills/commit-and-push.md` is a **cross-repo shared file**; the parity suite (`test_cross_repo_shared_file_parity.sh`) is already 1-red on master for an unrelated file. Run it before and after the edit (arm-to-arm control) and mirror the edit to the game-side twin if the mechanism requires it — do not "fix" the pre-existing red.
- Keep the ruff pin at `0.15.20` (the version measured green here); bumping the pin is out of scope.
- Do not add the fleet bash suites, header checks, or render-harness suites to the new workflow — they already have standalone homes; python-lint.yml owns exactly the two orphaned Python gates.
- When commenting on why the siblings went standalone, don't delete the history — they cite #2794/#2712/#2825; keep those numbers.

## Plan review — **sound**, clearing `fleet:plan-review` with binding constraints

`fleet-plan-lint 2718`: PASS (0 warnings). Independently verified against
`origin/master` this session (macOS pool-5) — every load-bearing measurement in
the plan reproduces:

- `gh workflow list --all`: Quality `disabled_manually`; five siblings active. ✓
- `gh run list --workflow quality.yml --limit 100 --json conclusion` →
  `[{"failure": 32}]`. **Never green**, exactly as claimed. ✓
- `.github/workflows/quality.yml:41-43` — `astral-sh/ruff-action@v3`, pinned
  `0.15.20`; `:51` — `python scripts/fleet/lint_state_mtime.py scripts/fleet/`;
  `:25` — clang-format `version: "18"`. ✓
- `fleet-rules-sweep --pattern 'quality\.yml'` → **17 matches / 11 files**
  (3977 files swept). The three live false assertions (`docs/agents/BUILD.md:213`,
  `docs/agents/skills/commit-and-push.md:172`, `ruff.toml:7`), the five accurate
  "disabled (#2718)" citations, and the three `.fleet/plans/` records land
  exactly as censused. ✓
- #2834 **OPEN**. ✓  The sole ruleset ("Master Restruction") is `disabled`, so
  no required status check can strand a PR. ✓

The divergence from the issue's written AC 2–3 is licensed by AC 1's own
"or split the Windows job out", and the plan surfaces it rather than burying it.
That call stays with the human: **`human:review-plan` untouched**, so the issue
remains out of the queue until they rule on retire-vs-re-enable. What I am
clearing is the agent-rigor gate.

---

### Binding constraints

The one real gap is that the plan has **no in-flight reconciliation section**,
and two open `fleet:approved` PRs sit on phase A's exact surface. These are not
optional refinements — #1 makes the workflow as specified fail a test suite.

**1. #2930 makes `python-lint.yml` red on arrival.** It lands
`scripts/fleet/tests/test_workflow_paths_sync.sh`, whose covered set is
*derived* from a `.github/workflows/*.yml` glob rather than a registry, and
whose **T4** (`missing_triggers`) asserts every covered workflow appears in
**both** of `fleet-tests.yml`'s duplicated `paths:` blocks. Its own comment
states the intent: *"a fifth workflow that declares both blocks fails here the
moment it lands instead of quietly costing itself its trigger."* Phase A's
`python-lint.yml` — `push` + `pull_request`, both path-filtered — is precisely
that fifth workflow.

⇒ Phase A must also add `- '.github/workflows/python-lint.yml'` to **both**
`paths:` blocks in `.github/workflows/fleet-tests.yml`, and add that file to
"Affected files". Deleting `quality.yml` shrinks the same derived set — expected,
not drift.

**2. Add a suite acceptance criterion.** Phase A currently has none, so
constraint 1 would first surface in CI instead of locally. Add:
`bash scripts/fleet/tests/run_all.sh` at or above the master baseline (known-red:
`test_cross_repo_shared_file_parity.sh`, irreden#368 — do not "fix" it). Given
that phase A already edits `.claude/rules/**` and `docs/agents/**`, both inside
`fleet-tests.yml`'s filter, this suite runs on the PR regardless.

**3. #2936 moves the ruff probe's ground truth.** It adds `fleet-gh-poll`,
`fleet-rules-sweep`, and `fleet-session-track` to `ruff.toml`'s `extend-include`,
and lands `lint_python_registry.py` whose **subject under test is `ruff.toml`** —
the file phase A edits. The plan's green `ruff check scripts/` was measured
*before* those three files entered ruff's population, so step 0's re-probe must
run **after #2936 merges**, not merely "at HEAD"; if phase A opens first, rebase
onto it before trusting the probe result. (#2936 also widens `fleet-tests.yml`'s
first filter from `scripts/fleet/**` to `scripts/**`, which is another reason
constraint 2's suite run is mandatory rather than nice-to-have.)

---

Everything else stands as written, including the two gotchas I spot-checked:
`.claude/rules/cpp-globals.md` is inside `fleet-tests.yml`'s `.claude/rules/**`
filter, so the prose-only edit will run `test_lint_rules_commands.py` — keep the
fenced Detection blocks byte-identical; and `docs/agents/skills/commit-and-push.md`
is a cross-repo shared file, so run the parity suite arm-to-arm around the edit.

The phase B decision (changed-lines, not tree-wide) is the right call and is
measured — 152/813 tree-wide failures against a repo whose enforced invariant is
`format-changed` (#2719) means a tree-wide gate would be red by construction.
Phase C's `Blocked by:` #2834 premise gate is correctly identified.

— opus-reviewer (pool-5, macOS)


---

## Plan review — **sound**, clearing `fleet:plan-review` with binding constraints

`fleet-plan-lint 2718`: PASS (0 warnings). Independently verified against
`origin/master` this session (macOS pool-5) — every load-bearing measurement in
the plan reproduces:

- `gh workflow list --all`: Quality `disabled_manually`; five siblings active. ✓
- `gh run list --workflow quality.yml --limit 100 --json conclusion` →
  `[{"failure": 32}]`. **Never green**, exactly as claimed. ✓
- `.github/workflows/quality.yml:41-43` — `astral-sh/ruff-action@v3`, pinned
  `0.15.20`; `:51` — `python scripts/fleet/lint_state_mtime.py scripts/fleet/`;
  `:25` — clang-format `version: "18"`. ✓
- `fleet-rules-sweep --pattern 'quality\.yml'` → **17 matches / 11 files**
  (3977 files swept). The three live false assertions (`docs/agents/BUILD.md:213`,
  `docs/agents/skills/commit-and-push.md:172`, `ruff.toml:7`), the five accurate
  "disabled (#2718)" citations, and the three `.fleet/plans/` records land
  exactly as censused. ✓
- #2834 **OPEN**. ✓  The sole ruleset ("Master Restruction") is `disabled`, so
  no required status check can strand a PR. ✓

The divergence from the issue's written AC 2–3 is licensed by AC 1's own
"or split the Windows job out", and the plan surfaces it rather than burying it.
That call stays with the human: **`human:review-plan` untouched**, so the issue
remains out of the queue until they rule on retire-vs-re-enable. What I am
clearing is the agent-rigor gate.

---

### Binding constraints

The one real gap is that the plan has **no in-flight reconciliation section**,
and two open `fleet:approved` PRs sit on phase A's exact surface. These are not
optional refinements — #1 makes the workflow as specified fail a test suite.

**1. #2930 makes `python-lint.yml` red on arrival.** It lands
`scripts/fleet/tests/test_workflow_paths_sync.sh`, whose covered set is
*derived* from a `.github/workflows/*.yml` glob rather than a registry, and
whose **T4** (`missing_triggers`) asserts every covered workflow appears in
**both** of `fleet-tests.yml`'s duplicated `paths:` blocks. Its own comment
states the intent: *"a fifth workflow that declares both blocks fails here the
moment it lands instead of quietly costing itself its trigger."* Phase A's
`python-lint.yml` — `push` + `pull_request`, both path-filtered — is precisely
that fifth workflow.

⇒ Phase A must also add `- '.github/workflows/python-lint.yml'` to **both**
`paths:` blocks in `.github/workflows/fleet-tests.yml`, and add that file to
"Affected files". Deleting `quality.yml` shrinks the same derived set — expected,
not drift.

**2. Add a suite acceptance criterion.** Phase A currently has none, so
constraint 1 would first surface in CI instead of locally. Add:
`bash scripts/fleet/tests/run_all.sh` at or above the master baseline (known-red:
`test_cross_repo_shared_file_parity.sh`, irreden#368 — do not "fix" it). Given
that phase A already edits `.claude/rules/**` and `docs/agents/**`, both inside
`fleet-tests.yml`'s filter, this suite runs on the PR regardless.

**3. #2936 moves the ruff probe's ground truth.** It adds `fleet-gh-poll`,
`fleet-rules-sweep`, and `fleet-session-track` to `ruff.toml`'s `extend-include`,
and lands `lint_python_registry.py` whose **subject under test is `ruff.toml`** —
the file phase A edits. The plan's green `ruff check scripts/` was measured
*before* those three files entered ruff's population, so step 0's re-probe must
run **after #2936 merges**, not merely "at HEAD"; if phase A opens first, rebase
onto it before trusting the probe result. (#2936 also widens `fleet-tests.yml`'s
first filter from `scripts/fleet/**` to `scripts/**`, which is another reason
constraint 2's suite run is mandatory rather than nice-to-have.)

---

Everything else stands as written, including the two gotchas I spot-checked:
`.claude/rules/cpp-globals.md` is inside `fleet-tests.yml`'s `.claude/rules/**`
filter, so the prose-only edit will run `test_lint_rules_commands.py` — keep the
fenced Detection blocks byte-identical; and `docs/agents/skills/commit-and-push.md`
is a cross-repo shared file, so run the parity suite arm-to-arm around the edit.

The phase B decision (changed-lines, not tree-wide) is the right call and is
measured — 152/813 tree-wide failures against a repo whose enforced invariant is
`format-changed` (#2719) means a tree-wide gate would be red by construction.
Phase C's `Blocked by:` #2834 premise gate is correctly identified.

— opus-reviewer (pool-5, macOS)


---

## Implementation notes (phase A, pool-1 / macOS, 2026-09-10)

Recorded per PLANNING-PROTOCOL.md — the Decisions and Acceptance criteria
above held; these are the departures and the evidence.

### Plan step 0 — probes re-run at HEAD, after the reviewer's constraint 3

Both re-run on the branch point (`origin/master`), i.e. **after** #2936 merged
and widened `ruff.toml`'s `extend-include` — which is what constraint 3 asked
for:

- `ruff --version` → `0.15.20`, exactly the pin the retired workflow carried.
- `ruff check scripts/` → exit 0, "All checks passed!"
- `python3 scripts/fleet/lint_state_mtime.py scripts/fleet/` → exit 0.

No new violation had landed, so the bail path was not taken.

### Reviewer constraint 1 — satisfied, and negative-controlled

`python-lint.yml` declares both `push:` and `pull_request:` `paths:` blocks, so
it enters `test_workflow_paths_sync.sh`'s derived covered set on arrival and
T4 requires it in **both** of `fleet-tests.yml`'s duplicated `paths:` lists.
Both entries added.

This was not taken on trust. Dropping the `pull_request` entry alone (a
line-targeted mutation, push block left intact — the exact half-drift the
ratchet exists for) turns the suite red with the specific message:

```
FAIL: fleet-tests.yml paths: covers every covered workflow, in both blocks
      actual:   pull_request .github/workflows/python-lint.yml
FAIL: fleet-tests.yml: push/pull_request paths: lists in sync
```

and restoring it returns the suite to green. The wiring is therefore
*exercised*, not merely present.

### Reviewer constraint 2 — suite run, arm to arm

`bash scripts/fleet/tests/run_all.sh`, same host, before and after the change:

| arm | result |
|---|---|
| branch point (`origin/master`) | 152 suites — **149 passed, 3 failed** |
| with this change | 152 suites — **149 passed, 3 failed** |

Per-suite results are byte-identical between the arms (`diff` of the
PASS/FAIL/SKIP lines is empty). The three red suites are red on pristine
master and are not this PR's: `test_dispatch_wrap_session.sh`,
`test_fleet_claim_decline.sh`, `test_fleet_health.py`.

**The plan review's stated known-red is stale.** It named
`test_cross_repo_shared_file_parity.sh` (irreden#368); that suite **passes**
today and a different three are red. The baseline above is the one measured
this session — use it, not the reviewer's.

### Reviewer constraint 3 — #2936 and #2930 both merged before the branch point

Confirmed merged, so the probe ground truth is current and no rebase-onto was
needed.

### Departures from the plan as written

1. **The census is 18 matches / 12 files, not 17 / 11.** One reference landed
   after the plan was written: `scripts/fleet/tests/test_workflow_paths_sync.sh`
   names `quality.yml` as an example of a workflow with no `paths:` filter at
   all. Retiring the workflow makes that example stale, so it is rewritten to
   name only `auto-rereview.yml`. Same class as the five "disabled (#2718)"
   citations the plan already enumerated.

2. **`.github/workflows/fleet-tests.yml` and
   `scripts/fleet/tests/test_fleet_tests_workflow_paths.sh` are in "Affected
   files".** The first is reviewer constraint 1. The second is that suite's
   `OUT_OF_TREE_SUBJECTS` mirror registry, whose comment enumerates
   `test_workflow_paths_sync.sh`'s subjects as "the three .github/workflows/
   entries"; leaving it at three while the covered set is four makes the
   readable list disagree with the executed one. T4 is the check; the list is
   the documentation of it, and they now agree.

3. **One adjacent stale note dropped**, in a comment block this PR already
   edits: `render-harness-tests.yml`'s header said "Generalizing the sync
   ratchet … is tracked as #2929 … Drop this note when #2929 lands." #2929
   landed (PR #2930, merged) — `test_workflow_paths_sync.sh` exists and covers
   that workflow. The note is replaced with the accurate statement.

4. **`.fleet/plans/` records left untouched**, as the plan specifies. They are
   at-rest artifacts of already-shipped work; four of them cite `quality.yml`
   and remain accurate as history.

5. **`scripts/fleet/CLAUDE.md` is in "Affected files" too.** Its authoring
   rules enumerate the workflows `test_workflow_paths_sync.sh` covers
   ("`header-checks.yml`, `perf-gate.yml`, `render-harness-tests.yml`, and
   `fleet-tests.yml` today") and describe the derived-growth case as "the
   fifth such workflow". `python-lint.yml` makes both stale on arrival — the
   "documented counts or lists drifted" case. Found by the code→doc pass, not
   by the `quality.yml` census, which is keyed on the retired name and cannot
   see a list that never mentioned it.

6. **No game-side mirror was needed.** The plan's gotcha flags
   `docs/agents/skills/commit-and-push.md` as cross-repo shared. Checked: the
   byte-identity contract in `test_cross_repo_shared_file_parity.sh` covers
   exactly two files (`.github/workflows/auto-rereview.yml`,
   `scripts/fleet/classify-auto-rereview.sh`) and not this one; the game repo
   carries only the thin `.claude/skills/commit-and-push/SKILL.md` wrapper, no
   copy of the shared flow, and has **zero** `quality.yml` references
   (`git grep -F quality.yml origin/master` → no hits). Nothing to mirror.

### Local negative controls on the new workflow's two gates

CI-side controls are on the PR (acceptance criterion 2); these are the
author-side arms proving each step's checker actually fires:

| gate | violation planted | result | restored |
|---|---|---|---|
| `ruff check scripts/` @ 0.15.20 | unused `import os` in `scripts/fleet/lint_state_mtime.py` | non-zero, `F401` reported | exit 0 |
| `lint_state_mtime.py` | a `os.stat("state.json").st_mtime` read in a new `scripts/fleet/` file | non-zero, "1 unsuppressed state.json st_mtime read(s) found" | exit 0 |

### Phase A acceptance criteria — status at PR-open

| # | criterion | status |
|---|---|---|
| 1 | `python-lint.yml` run appears on the PR and is green | pending — observed on the PR |
| 2 | negative control observed in CI (deliberate ruff violation reds the run, dropping it greens it) | pending — observed on the PR |
| 3 | `gh workflow list --all` no longer lists Quality; `quality.yml` absent from `origin/master` | pending merge; the file is deleted in this diff |
| 4 | post-merge `fleet-rules-sweep --pattern 'quality\.yml'` finds only `.fleet/plans/` records | pending merge; pre-merge sweep of the branch verifies it |
| 5 | follow-ups B/C/D exist with the recorded decisions and `Blocked by:` lines | **done** — #3187 (B), #3188 (C), #3189 (D) |

### Follow-ups filed

- **#3187** — phase B, changed-lines clang-format gate. `Blocked by:` #2718.
  Agent-approved lane, `## Plan` posted at file time + `fleet:plan-review`
  (`fleet-plan-lint`: PASS, 0 warnings). Carries the locked
  changed-lines-not-tree-wide decision and its 152/813 measurement, plus an
  **isolation control** the plan did not name: a gate that is file-scoped
  rather than line-scoped passes the ordinary negative control and still
  breaks #2719's property, so the criteria require a run proving a PR that
  touches a clean line in a drifted file stays green.
- **#3188** — phase C, `engine-tests.yml`. `Blocked by:` #2718 **and #2834**
  (the premise gate; #2834 confirmed still OPEN on 2026-09-10). Same lane and
  plan shape (`fleet-plan-lint`: PASS). Its phase 0 re-measures the suite
  rather than trusting #2834's five-week-old numbers.
- **#3189** — phase D, clang-tidy CI spike. Filed **unlabeled** for human
  triage, as the plan specifies: one of its three admissible outcomes is
  "document no-CI-path as accepted", which is a policy call. Cited from
  `.claude/rules/cpp-globals.md` and `cmake/run_header_checks_standalone.cmake`
  so the two docs point at the open question rather than at a retired
  workflow.
