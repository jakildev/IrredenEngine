## Plan: fleet/planning — plans must verify a measurable mechanism premise before phases build on it (fix-014 recurring)

- **Issue:** #2401
- **Model:** sonnet — rule text, lint delta, and test cases are fully specified below; no design choice remains
- **Date:** 2026-07-14

### Scope

Make "the plan's lever rests on an unmeasured mechanism claim" a named, checkable planning defect. Three deltas: (1) a premise-verification (phase-0) requirement in PLANNING-PROTOCOL.md step 2, (2) mirror lines in the step-4 plan-review rigor list, (3) one conservative WARN in `fleet-plan-lint` with hermetic tests. A gated-lane follow-up issue carries the two-line inline mirror for the opus reviewer's role doc.

### Verified current state

Source-checked against `origin/master` @ b8311c12 (2026-07-14):

- `docs/agents/PLANNING-PROTOCOL.md` step 2 requires "Verified current state + confirmed repro" and exhaustive negative/gap-claim verification, but nothing about measurable mechanism premises (cost attribution, path dominance, a stage actually firing). Step 4's rigor list (verified state / single approach / sibling reconciliation / cross-system audit) has no unmeasured-mechanism line. The acceptance-criteria bullet has no positive-fire rule.
- `scripts/fleet/fleet-plan-lint` (read in full): hard-fails on no `## Plan` / deferred-approach phrases / >=2 core sections missing; warns on missing Affected files, Gotchas, current-state signal, Model tag. No mechanism-vs-measurement signal of any kind. Existing hermetic test at `scripts/fleet/tests/test_fleet_plan_lint.sh` uses a PATH-shim fake `gh` with fixtures derived from a `GOOD_PLAN` env var.
- The positive-fire rule exists today only on the render author surface (`engine/render/CLAUDE.md` enabled-path rule, #1989/#2338, landed via PR #2399); grep for `positive-fire|vacuous` over `docs/agents/` = 0 hits — the planning surfaces don't carry it.
- `.claude/commands/role-opus-reviewer.md` "Plan-review pass" step 2 judges plans "against PLANNING-PROTOCOL.md step-2 rigor" **by reference**, then enumerates four example checks inline. New rigor items placed in PLANNING-PROTOCOL.md therefore bind the reviewer without a gated edit; only the inline enumeration needs a two-line mirror, and role-`*.md` edits are blocked for autonomous workers (human-cued gated lane, as this issue's Area line states).
- The measurement sources the issue names all exist: per-system timer rows, `--auto-profile` tables, the disarm-probe methodology in `docs/design/gpu-stage-timing-cost-model.md` §3.

Sibling / in-flight reconciliation: #2402 (fix-011, worktree-scoped-edit enforcement, being planned concurrently by worker-4) is a disjoint surface — no file overlap. PR #2399 (merged 2026-07-14) carries the render-surface positive-fire complement; this task cross-references it, never duplicates it. No open PR touches PLANNING-PROTOCOL.md or fleet-plan-lint (checked #2393, #2403, #2405).

### Approach

Phase 0 does not apply to this task itself: the premise here is process-recurrence data, already measured — fix-014 flipped to recurring with 8 occurrences since PR #2038 (`~/.fleet/feedback/.fix-log.jsonl`).

1. **Plan file first.** Commit this plan as `.fleet/plans/issue-2401.md` (first commit of the implementation branch, per #1932).
2. **`docs/agents/PLANNING-PROTOCOL.md` step 2** — insert a new bullet directly after "**Verified current state + confirmed repro.**":
   - **"Mechanism premises are measured, not asserted (phase 0)."** When any phase's lever depends on a measurable mechanism claim — where a cost lives (body-side vs dispatch-bound), which code path dominates, that a stage/mode fires at all, that two values share one storage/shape — the plan must either **(i)** cite an existing measurement with its source (a per-system timer row, an `--auto-profile` table, a disarm probe per `docs/design/gpu-stage-timing-cost-model.md` §3, a DOMAIN-STATE log), or **(ii)** name a cheap probe as **phase 0 of the Approach**: what the implementer runs, the expected reading that confirms the premise, and the bail path if refuted (stop; comment the measurement on the issue; design-block or flag for re-plan — never build the dependent phases on a refuted premise). Cite the recurrences compactly: #2258 (assumed body-side, measured dispatch-bound), #2256/#2271/#2273 (parallel efforts on mutually-invalidated premises), #2278 (vacuous gate), #2321 (unverified singleton/same-shape premise).
   - Deliberately **not** a new required `###` section — a new core section would re-lint every existing queued plan into a bounce. It is a conditional content rule inside the existing structure.
3. **Same file, acceptance-criteria bullet** — add: acceptance tests named in a plan must be **positive-fire**: at least one named check observably fires with the feature ON (a count > 0, an asserted probe reading, a visible delta). "Gate passes at default / byte-identical output" alone proves the OFF path is a no-op, not the premise — mirror of the #1989/#2338 enabled-path rule (`engine/render/CLAUDE.md`), which PR #2399 established render-side.
4. **Same file, step 4 rigor list** — append two mirror lines to the reviewer's judgment: "does any phase assume an unmeasured mechanism? (cited measurement or phase-0 probe required)" and "are the named acceptance tests positive-fire?".
5. **`scripts/fleet/fleet-plan-lint`** — one new soft signal in the warn block (never hard-fail, per the script's false-bounce contract): if the lowercased plan contains any mechanism-lever keyword (`"bottleneck"`, `"dominated by"`, `"dispatch-bound"`, `"fill-bound"`, `"the cost is"`, `"hot path"`, `"perf lever"`, `"should be cheap"`) and none of the citation keywords (`"measur"`, `"timer"`, `"auto-profile"`, `"disarm"`, `"probe"`, `"profil"`, `"domain-state"`), print `warn #N: mechanism-lever language without a measurement citation or phase-0 probe`. Match style identical to the existing `low`-substring checks; update the header-comment warn list.
6. **`scripts/fleet/tests/test_fleet_plan_lint.sh`** — two new fixtures via the existing `GOOD_PLAN`-mutation pattern in the PATH-shim fake gh: (i) a plan whose Approach claims "the cost is dominated by the resolve loop" with no citation keyword → expect exit 0 **and** the new warn line present (the warn must be observed firing); (ii) the same plan plus "confirmed by a disarm probe" → expect the warn absent. Keep the shim hermetic (a mock miss must fail, never fall through to live `gh` — scripts/fleet/CLAUDE.md).
7. **Gated-lane follow-up.** File a no-label issue per TASK-FILING.md: "role-opus-reviewer: add unmeasured-mechanism + positive-fire lines to the plan-review judgment enumeration (#2401 follow-up, gated)". Link it in the PR body. Non-blocking — the by-reference binding in the reviewer doc already picks the rule up.

One task, one PR — no stack.

### Affected files

- `docs/agents/PLANNING-PROTOCOL.md` — step-2 phase-0 premise bullet, positive-fire acceptance rule, step-4 mirror lines
- `scripts/fleet/fleet-plan-lint` — one new WARN + header-comment warn list
- `scripts/fleet/tests/test_fleet_plan_lint.sh` — warn-fires + warn-absent cases
- `.fleet/plans/issue-2401.md` — this plan (first commit)
- *(follow-up issue only, not this PR: `.claude/commands/role-opus-reviewer.md` two-line mirror — gated)*

### Acceptance criteria

- `bash scripts/fleet/tests/test_fleet_plan_lint.sh` → 0 FAIL, including the two new cases; the warn-fires case demonstrates the new warn actually printing (positive-fire applied to its own backstop).
- Lint behavior unchanged for hard-fail: a mechanism-lever plan without citation still exits 0 (warn-only).
- PLANNING-PROTOCOL.md step 2 names both satisfaction routes (cited measurement | phase-0 probe with expected reading + bail path) and all four recurrence refs; step 4 carries both mirror lines.
- PR diff contains no gated self-config file; follow-up issue filed and linked.

### Gotchas

- **WARN only.** A hard-fail here false-bounces narrative plans and burns a full re-plan round — the lint's own header pins this contract.
- Do not put the role-doc mirror in the PR: the commit gate deterministically blocks role-`*.md` pushes from workers; escalating class does not help (route = follow-up issue, human-cued lane).
- Keep the keyword lists short and conservative; noisy warns erode the signal the Opus pass weighs. Substring-match on the existing `low` variable, same as current checks.
- Word the phase-0 doc rule so it cannot be read as approach-deferral: phase 0 verifies the premise of the **already-picked** approach; a refuted premise routes to design-block/re-plan, not to a mid-implementation choice between approaches. Keep the DEFER-phrase lint list in mind when phrasing examples.
- Plans and these docs are engine-public — engine terminology only.
