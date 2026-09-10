## Plan: persist — executed inventory-population gate + C_Locked save decision (rev 2)

- **Issue:** #2834
- **Model:** opus
- **Date:** 2026-08-09

### Revision note

Rev 1 (2026-08-09T04:11Z) was bounced by plan review (04:27Z) on three items:
the phase-4 wiring list named three sites of five, phase 3 left the
absent-inventory behavior unspecified, and phase 5's positive controls were
PR-body-only. This revision folds in all three. Phases 0–3's diagnosis, census,
and mechanism choice are unchanged from rev 1 and were independently
re-verified by the review ("Checked — don't re-derive these"). Re-measured
before revising: `origin/master` is still `5a1fdc9ec` — the exact commit rev 1
and the review both measured — so every census number and line citation below
carries without decay. The lint stub `add_custom_target(lint)` at
`ir_quality_tools.cmake:281` (clang-tidy absent) runs no checks and is
confirmed NOT a sixth wiring site.

### Verified current state (origin/master @ `5a1fdc9ec`)

- **Repro holds.** `kExpectedEngineComponentCount` is `167`
  (`save_component_inventory.hpp:555`) vs a 169-entry `AllEngineComponents`;
  `SaveTrait.InventoryIsComplete` is red on master. Full-suite control: 1474
  tests, exactly this one failing.
- **Census (comment-stripped; measured by rev 1, reproduced exactly by the
  review):** 170 live `struct C_*` in `engine/` headers; 169 decision lines
  (167 `IRComponents::` + 2 `IRSystem::`); 169 tuple names; decision set ==
  tuple set; missing = `{C_Locked}` in both directions; dead-decision and
  dead-tuple sets empty. `.cpp`-only declarations are exactly the two Class-D
  anchors (`C_AutoScreenshotAnchor`, `C_GuiTestAnchor`), excluded by the
  inventory's stated scope.
- **False-positive corpus:** an un-stripped census returns 174 — the four
  extras are commented-out declarations in `component_tags_all.hpp`
  (`C_MainCanvas`, `C_GuiCanvas`, `C_BackgroundCanvas`, `C_IsGamepad`).
- `kExpectedEngineComponentCount` has one code consumer:
  `test/world/save_trait_test.cpp:105`.
- **Sibling / in-flight reconciliation (updated — rev 1's "no open PR touches
  the surface" no longer holds for the revised scope):** three approved open
  PRs edit `scripts/fleet/tests/test_header_checks_standalone.sh` — #2944
  (header-global matcher, #2916), #2959 (Metal registry comment strip, #2899),
  #2984 (preprocessor-conditional registry entries, #2983) — the same file
  phase 5 now commits arms to. Separately, #2930 (#2929) generalizes the
  workflow path-list sync ratchet so `test_workflow_paths_sync.sh` covers
  `header-checks.yml`'s duplicated `paths:` blocks. None of the four touches
  the inventory header, the save-trait test, `ir_quality_tools.cmake`,
  `run_header_checks_standalone.cmake`, or the workflow's path lists — the
  overlap is textual and confined to the harness file. Consequences are baked
  into phases 4–5: rebase before editing the harness, treat the arm tally as
  whatever master then carries (the 53-arm baseline moves as those PRs land),
  and keep the two `paths:` blocks byte-identical.
- **#2718** (quality.yml disabled at repo level → ctest never runs in CI) is
  the separately filed CI half; this plan is deliberately independent of it.

### Scope

Three deliverables in one PR: (1) `C_Locked` gets an explicit save decision +
tuple entry; (2) the hand-maintained count constant and its test are deleted;
(3) an **executed, merge-gating population check** compares the live
`struct C_*` set in engine headers against the inventory, so a component added
with no inventory entry fails CI — now with all five wiring sites and
committed fixture arms.

### Approach

**Mechanism (unchanged, review-endorsed):** a pure-CMake script-mode check in
the header-checks family — new `cmake/run_save_inventory_population_check.cmake`,
modeled on `run_metal_kernel_registry_check.cmake` (#2798) and
`run_metal_scratch_consumer_check.cmake` (#2878). It gates merges via the one
CI path that gates merges (`header-checks.yml`; `quality.yml` is disabled,
#2718); it reads the tree, not the tuple, so a never-listed type is visible to
it; codegen stays rejected — the inventory is an audited human decision table,
and generating it would erase the audit.

**Phase 0 — census probe (premise gate), unchanged.** Strip comments from
every `engine/**/*.{hpp,h}` (excluding `engine/render/third_party/`), collect
`struct C_\w+` names, diff against the inventory's decision lines and tuple
block. Expected reading: missing set = `{C_Locked}`, dead sets empty. Bail
path: any other offender → stop, comment the measurement on the issue, swap
back to `fleet:needs-plan` — an unplanned save-policy question is a planning
matter, not the implementer's to improvise.

**Phase 1 — inventory fix, unchanged**
(`save_component_inventory.hpp`):
- `#include <irreden/common/components/component_locked.hpp>` in the
  alphabetical include block (simplify check-16 enforces the order).
- `IR_SAVE_OPT_IN(IRComponents::C_Locked, 1)` adjacent to `C_Persistent`
  (`:362`) — the component's own doc comment ("Mirrors `C_Persistent`") fixes
  the policy; an empty tag serializes exactly like the existing opted-in tags.
- Tuple entry adjacent to `C_Persistent` (`:546`).
- Amend the header's top contract comment to name the executed population
  check as the third gate, keeping spec and executor adjacent (#2727 lesson).

**Phase 2 — delete the constant and its test, unchanged**
(`test/world/save_trait_test.cpp`): remove `kExpectedEngineComponentCount`
(`:555`) and `TEST(SaveTrait, InventoryIsComplete)` (`:103-106`). Its residual
coverage — a decision line whose tuple entry was dropped — transfers to the
new check's tuple-membership half and is regression-locked by fixture arm (c)
in phase 5. Add a `SaveTrait<C_Locked>` spot-check to the existing trait
expectations (`kSave == true`, version 1) so criterion 2 fires positively.
Engine-API-removal sweep: `fleet-rules-sweep --pattern
'kExpectedEngineComponentCount'` must return only the lines being deleted.

**Phase 3 — the checker, one addition: the absent-anchor ruling**
(`cmake/run_save_inventory_population_check.cmake`, new):
- Census and parse spec unchanged from rev 1: `file(GLOB_RECURSE)` over
  `engine/**/*.hpp` + `*.h` minus `engine/render/third_party/`; the check
  derives its own file list (no style-scoped collector to inherit, so the
  `INCLUDE_RENDER_BACKENDS` contract does not apply — render-backend headers
  are in by construction). Strip block comments with the CMake-safe
  greedy-bounded form `/\*([^*]|\*+[^*/])*\*+/` (measured working by the
  review under `cmake -P`), then line comments `//[^\n]*`. Extract live names
  `struct[ \t\r\n]+C_[A-Za-z0-9_]+`; dedupe. Parse
  `save_component_inventory.hpp` (also comment-stripped): decision names from
  `IR_SAVE_OPT_(IN|OUT)\(` + `(IRComponents|IRSystem)::C_\w+`, tuple names
  from the `AllEngineComponents = std::tuple<...>;` block. Template
  instantiations (`C_GeometricShape<...>`, `C_SystemEvent<...>`) match on the
  `C_\w+` head only.
- **Ruling (the design call rev 1 left open): a missing or unparseable
  `save_component_inventory.hpp` is FATAL** — exit 1, anchor guard, matching
  the family's existing shape (`run_metal_scratch_consumer_check.cmake:42-43`
  FATALs on a missing `metal_runtime.hpp`, and again on an unreadable slot
  constant). A silent pass on a checker that read nothing is precisely the
  false-clean class this issue exists to close. Same guard on the census: zero
  `struct C_*` names collected → FATAL "glob mis-scoped", mirroring the
  kernel-count guard (`:80-86`). The fixture consequence is budgeted in phase
  5: the base fixture grows a miniature inventory + component header so every
  arm, including "clean fixture exits 0", keeps passing.
- Two distinct failure messages, each naming the type, its declaring header,
  and the remedy ("add IR_SAVE_OPT_IN/OPT_OUT + AllEngineComponents entry in
  save_component_inventory.hpp"): **missing decision** and **missing tuple
  entry**.
- **No baseline/ratchet list.** Phase 1 empties the offender set in the same
  PR; a population check starts at zero.
- Scope notes in the file header: headers-only census (Class-D `.cpp` anchors
  excluded by construction); `engine/` root only; forward declarations count
  as live names (today zero engine headers forward-declare a `C_*` type, so no
  allowlist; if one appears the check names it and the author acts on it).

**Phase 4 — wiring, all FIVE sites** (rev 1 named the first three; skipping
site 4 reds every harness arm in CI, skipping site 5 leaves the checker's own
edits ungated):
1. `cmake/ir_quality_tools.cmake` — `header-checks` target (`:244-256`):
   fourth `-P` command, same `-DPROJECT_ROOT` shape as the two Metal checks.
2. `cmake/ir_quality_tools.cmake` — `lint` target, clang-tidy branch
   (`:260-278`): same addition. (The `:281` stub target runs no checks — not
   a site.)
3. `cmake/run_header_checks_standalone.cmake` — `include()` alongside the two
   Metal checks (`:68-69`).
4. `scripts/fleet/tests/test_header_checks_standalone.sh` — `make_fixture`'s
   explicit `cp` list (`:117-121`): add the new script. CMake `include()` on a
   missing file is a hard error, so wiring (3) without (4) fails every arm —
   "clean fixture exits 0" included — and that harness is
   `header-checks.yml`'s first job step, so it lands as red CI on this PR
   itself.
5. `.github/workflows/header-checks.yml` — the header comment list (`:3-9`,
   which states this obligation verbatim) and **both** hand-duplicated
   `paths:` blocks (`:34-36`, `:47-49` — Actions has no YAML anchors), so a PR
   touching only the new check script still triggers the gate. Keep the two
   blocks byte-identical; once #2930 lands, `test_workflow_paths_sync.sh`
   checks that mechanically.

**Phase 5 — controls: committed fixture arms, not PR-body pastes** (re-formed
per the bounce; every sibling check in the family has committed arms, and the
harness's own header states why a green run alone proves nothing):
- **Base-fixture growth in `make_fixture`:** a miniature
  `engine/world/include/irreden/world/save_component_inventory.hpp` (two
  decision lines + a matching two-entry `AllEngineComponents` tuple block) and
  an `engine/include/irreden/components_fixture.hpp` declaring the two
  matching `struct C_*` types **plus the false-positive corpus**: one
  `// struct C_LineCommented {};` and one `/* struct C_BlockCommented {}; */`.
  Every green run of any arm then re-proves comment-stripping and both parse
  halves.
- **Committed arms** (exit code AND named-offender output asserted via the
  existing `lib_assert.sh` pattern):
  - (a) clean fixture → exit 0 — extends the existing clean arm; proves the
    anchor guard does not misfire and the miniature inventory parses.
  - (b) a fixture header gains `struct C_FixtureOrphan {};` with no inventory
    edit → exit 1, output names `C_FixtureOrphan`, its declaring header, and
    "missing decision".
  - (c) same type given a decision line but no tuple entry → exit 1, "missing
    tuple entry" — this arm is the committed regression lock replacing the
    deleted `InventoryIsComplete` test's residual coverage.
  - (d) inventory header removed from the fixture → exit 1, anchor guard —
    the ruling in phase 3, pinned.
- **PR-body demonstrations** (not committable, still required): the historical
  positive — on the pre-phase-1 tree the check FAILS naming `C_Locked` — and
  the injected-probe walk on the real tree (`struct C_DummyPopulationProbe {};`
  in an engine header → FAILS missing-decision; decision line added → FAILS
  missing-tuple; tuple entry added → passes), which also proves the real
  inventory parses, not only the miniature.

### Affected files

- `engine/world/include/irreden/world/save_component_inventory.hpp` — include
  + decision + tuple entry for `C_Locked`; delete the constant; contract
  comment names the executed gate
- `test/world/save_trait_test.cpp` — delete `InventoryIsComplete`; add the
  `SaveTrait<C_Locked>` spot-check
- `cmake/run_save_inventory_population_check.cmake` — **new**, the population
  check
- `cmake/ir_quality_tools.cmake` — wire into `header-checks` + `lint`
- `cmake/run_header_checks_standalone.cmake` — wire into the CI shim
- `scripts/fleet/tests/test_header_checks_standalone.sh` — fixture `cp` list;
  miniature inventory + component fixture header in `make_fixture`; arms
  (b)–(d)
- `.github/workflows/header-checks.yml` — comment list + both `paths:` blocks

### Acceptance criteria

1. `./build/IrredenEngineTest --gtest_filter='SaveTrait.*'` green
   **standalone**, and the full binary goes clean — this flips the tree-wide
   suite control from "1474 with exactly one known red (#2834)" to all-green;
   the PR body must say so, since multiple in-flight plans cite the old
   control number.
2. `SaveTrait<C_Locked>::kSave == true` at version 1, asserted by a named test
   line (fixture = the existing `IrredenEngineTest` target).
3. `bash scripts/fleet/tests/test_header_checks_standalone.sh` on the PR
   branch → **all arms pass, including (a)–(d)**, tally pasted in the PR body.
   The criterion is "all pass", not a pinned count — the pre-PR tally (53 on
   `5a1fdc9ec`) moves as #2944/#2959/#2984 land.
4. `cmake -DPROJECT_ROOT=<repo-root> -P cmake/run_header_checks_standalone.cmake`
   and `cmake --build <build-dir> --target header-checks` both pass on the
   fixed tree.
5. The two PR-body demonstrations from phase 5 (historical positive on the
   pre-fix tree; injected-probe walk on the real tree), output pasted.
6. `fleet-rules-sweep --pattern 'kExpectedEngineComponentCount'`
   post-deletion → zero hits outside at-rest `.fleet/plans/` records.

### Gotchas

- **Comment stripping is load-bearing** — the four commented-out tags in
  `component_tags_all.hpp` false-fire any naive matcher (measured, not
  hypothetical).
- CMake regex portability: no `\d`, no non-greedy, `[ \t\r\n]` for whitespace.
- The inventory's include list is alphabetized (simplify check-16) — place
  `component_locked.hpp` in order.
- Old snapshots predate any `C_Locked` record and load unchanged — version 1,
  no `SaveMigration`.
- Do not edit the at-rest `.fleet/plans/*.md` files that cite the old
  suite-control numbers; they are historical records.
- **Rebase before editing `test_header_checks_standalone.sh`** —
  #2944/#2959/#2984 all touch it and are approved; their land order is theirs,
  and the conflict surface is textual only.
- The reverse direction — a decision or tuple line naming a type that no
  longer exists — is compile-gated (both the macro and the tuple reference the
  real type), so the text check deliberately does not duplicate it.
- The harness warns that arms injecting a new Metal *kernel* must register it
  in `write_pipeline_cpp`'s registry list — not applicable to arms (a)–(d),
  which touch only headers and the miniature inventory; noted so a Metal-check
  FATAL during arm authoring is not "fixed" in the wrong fixture half.

One task, one PR — the check without the fix is red on master; the fix without
the check re-opens the hole. Suggested model: **[opus]** (bounded, but the
matcher's false-clean/false-fire pitfalls and the five-site wiring gate every
future component PR).

*Rev 2 planned at fable class per dispatcher assignment (re-plan after the
04:27Z bounce). Label timeline checked: `human:review-plan` was set with rev 1
at 04:11Z and has not been human-cleared, so it stays in place for the human's
approach sign-off on the gate mechanism + shipped-test deletion — nothing is
re-added. Swapping `fleet:needs-plan` → `fleet:plan-review`.*

