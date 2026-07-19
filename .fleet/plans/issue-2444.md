## Plan: review/test policy — per-new-public-surface coverage + test/CLAUDE.md

- **Issue:** #2444
- **Model:** sonnet — docs/checklist work, fully bounded: the exact checklist
  replacement text is supplied below and the doc's content spec is enumerated.
- **Date:** 2026-07-15

### Scope

Two deliverables, split by the self-config gate:

1. `test/CLAUDE.md` (new) — suite layout + per-surface authoring expectation.
   **Worker-executable**; ships as the PR.
2. The `review-pr` SKILL.md §Tests/build reword. `.claude/skills/**/SKILL.md`
   is gated self-config — the auto-mode gate physically blocks every worker
   class from pushing it (fleet-labels-reference § `fleet:gated`; all prior
   checklist edits landed via human-cued batches: #2399, #2268, #1733).
   **Human-applied**: the worker posts the exact edit (verbatim from
   § Gated half below) as an issue comment and parks this issue
   `fleet:needs-human`.

### Verified current state (master @ b622bb54)

- `.claude/skills/review-pr/SKILL.md:145` still reads "New feature with no
  new test at all (flag as needs-fix unless the user explicitly said 'no
  tests')" — the per-PR wording the issue targets.
- `test/CLAUDE.md` is absent.
- The #2425 worked example **still holds post-merge**:
  `test/system/system_cadence_test.cpp` has 13 tests and zero
  `kCadence`/`kCadenceOffset` references, so the `System<N>` spec-member
  detection path (`engine/system/include/irreden/ir_system.hpp:325-336`) is
  uncovered; the four Lua bindings `IRSystem.setSystemCadence` /
  `getSystemCadence` / `setSystemCadenceOffset` / `getSystemCadenceOffset`
  (`engine/script/include/irreden/script/lua_pipeline_bindings.hpp:356-382`)
  have no coverage anywhere under `test/script/`.
- **Count correction:** the issue body says 19 precedent files; master has
  **17** `test/script/lua_*_test.cpp`. Use 17.
- Suite mechanics (for the doc): single `add_executable(IrredenEngineTest …)`
  in `test/CMakeLists.txt` with an explicit per-area source list (a new test
  file does not build until registered there); `gtest_discover_tests`
  registers per-test CTest entries; Lua fixtures live at `test/script/*.lua`
  and are wired through `irreden_lua_codegen(IrredenEngineTest …)` blocks.
- Sibling/in-flight check: the only open engine PR (#2393) is render-only —
  no overlap with `test/` or the skill file.
- No phase-0 probe needed: every premise above is a static source fact,
  verified against master at planning time (no measurable-mechanism claim).

### Approach

1. Branch off master; commit this plan as `.fleet/plans/issue-2444.md`
   (first commit, per #1932).
2. Author `test/CLAUDE.md` covering, in order:
   - **Target**: all tests compile into the single `IrredenEngineTest`
     executable; `gtest_discover_tests` handles CTest registration.
   - **Layout**: per-area subdirectories mirroring engine modules (asset,
     audio, common, ecs, job, math, render, script, system, time, tools,
     utility, world); new engine functionality lands with tests in the
     matching subdirectory.
   - **Registration**: add the new `.cpp` to the explicit source list in
     `test/CMakeLists.txt` (grouped by area) — unregistered files silently
     don't build.
   - **Lua fixture pattern**: `test/script/*.lua` fixtures +
     `lua_*_test.cpp` exercising the sol2 seam (17 precedent files);
     codegen fixtures wired via `irreden_lua_codegen`.
   - **Filtered runs**: build with `fleet-build --target IrredenEngineTest`,
     run a subset with `--gtest_filter=<pattern>` through `fleet-run`.
     Verify the documented command by running it once; paste the output
     line in the PR body.
   - **Authoring expectation**: coverage is per new public surface — each
     new Lua binding, each registration path when a facility has two
     (free-function params AND `System<N>` spec-member detection), each new
     public API — with the #2425 cadence gap as the worked example.
   Keep it tight (aim under ~100 lines) per CLAUDE-BASELINE's rules for
   what belongs in a `CLAUDE.md`.
3. Open the PR via `commit-and-push`. PR body says **"Part of #2444"** —
   NOT `Closes` — the gated half stays open behind it.
4. After the PR is open: post the § Gated half block below verbatim as a
   comment on #2444, then park the issue for the human:
   `gh issue edit 2444 --remove-label fleet:queued --add-label fleet:needs-human`
   (keep `human:approved`), and release the claim. The human pastes the
   edit, lands it through their own lane, and closes the issue.

### Gated half — ready-to-apply §Tests/build edit (human applies)

In `.claude/skills/review-pr/SKILL.md`, replace this bullet (lines 145-146):

```
- New feature with no new test at all (flag as needs-fix unless the user
  explicitly said "no tests").
```

with:

```
- New public surface with no covering test — coverage is per **new public
  surface**, not per PR. "A new test exists" does not satisfy this: a PR
  that tests one surface while shipping others uncovered gets needs-fix on
  the uncovered surfaces. Per surface the diff adds, expect:
  - a new Lua binding → a `test/script/lua_*_test.cpp` exercising the sol2
    seam (17 existing files set the precedent);
  - a facility with two registration paths (e.g. free-function params AND
    `System<N>` spec-member detection) → a test through **each** path;
  - other new public surface (`ir_*.hpp` API, component/system, serialized
    format) → a test in the matching `test/<area>/` subdirectory.
  Worked example (#2425, per-system cadence): 13 SystemManager tests
  shipped, but the `IRSystem.*Cadence*` Lua bindings and the
  `kCadence`/`kCadenceOffset` spec-member detection path shipped with zero
  coverage — under this wording each uncovered surface is mechanically
  flaggable even though "a new test" existed. Suite layout + authoring
  expectations: `test/CLAUDE.md`.
  The "no tests" waiver stays human-explicit (the user literally said "no
  tests"); never infer it.
```

### Affected files

- `test/CLAUDE.md` — new; suite doc per Approach step 2.
- `.fleet/plans/issue-2444.md` — new; this plan, first commit of the PR.
- `.claude/skills/review-pr/SKILL.md` — §Tests/build bullet swap;
  **human-applied, never part of the worker PR** (gated self-config).

### Acceptance criteria

- [ ] `test/CLAUDE.md` merged covering target / layout / CMake registration /
      Lua fixture pattern / filtered-run command / per-surface authoring
      expectation, with the #2425 gap cited as the worked example.
- [ ] The documented filtered-run command was actually executed once and its
      output shown in the PR body (positive-fire: the named check runs and
      reports tests, not just "docs look right").
- [ ] The § Gated half block posted verbatim as a comment on #2444; issue
      parked `fleet:needs-human` (with `human:approved` kept) after the PR
      opened; claim released.
- [ ] PR body uses "Part of #2444", not "Closes" — the issue closes only
      when the human lands the SKILL.md edit (acceptance box 1 of the issue
      body is theirs).
- [ ] Under the new wording, both #2425 gaps (Lua cadence bindings;
      spec-member path) are mechanically flaggable — the worked example in
      the checklist text names them explicitly.

### Gotchas

- **Do not attempt to edit or commit `.claude/skills/review-pr/SKILL.md`.**
  The self-modification gate blocks the write for every worker class —
  attempting it burns the iteration (role-worker step 8b). The comment +
  `fleet:needs-human` park is the sanctioned handoff.
- Park the issue only **after** the PR is open, or ingest/scope-shipped
  churn can re-queue a half-done state.
- Keep `human:approved` when parking — it is the human's durable signal.
- Use the verified count (17), not the issue body's 19.
- The plan and PR are engine-public: engine terminology only.
- New test files need explicit registration in `test/CMakeLists.txt` —
  document that in the CLAUDE.md; it is the most common authoring miss.
