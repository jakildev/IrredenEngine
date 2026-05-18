# Audit — CLAUDE.md files across engine + creations

Research findings for task T-223 (issue #800-family). Covers all 31 `CLAUDE.md`
files in the engine repo: root, `engine/**`, `creations/**`. Each file is
audited against six categories — baseline duplications, dead pointers,
rule contradictions, slop, missing patterns, layout dumps — using
`docs/agents/CLAUDE-BASELINE.md` as the cross-cutting reference for what
"already inherited" looks like.

The follow-up cleanup tasks at the bottom enumerate one-PR-each issues that
the queue-manager can ingest. Each is scoped to a single subtree so the
diffs stay reviewable.

---

## Pre-verified facts used throughout

- `C_PositionOffset3D` — DELETED in PR #746 (T-192). Any reference is
  genuinely stale.
- `C_Position3D`, `C_PositionGlobal3D`, `C_Rotation` — still defined today
  (component files exist), but actively being phased out per T-199
  (status `~`, in flight). Replacement: `C_WorldTransform` +
  `C_LocalTransform`. References that exist are accurate-but-aging;
  ideally CLAUDE.md notes the transition.
- Stale-ref hits in the issue's notes section overstate the situation —
  three of the four "stale" component names are still live and being
  migrated, not removed. The one truly-dead name is `C_PositionOffset3D`,
  which isn't actually referenced in any CLAUDE.md today (already cleaned
  up).

---

## Cross-cutting patterns observed

Several pathologies repeat across many files. Calling them out once so the
per-file sections don't re-justify them:

1. **Name catalogs dominate the per-module files.** Almost every
   `CLAUDE.md` under `engine/` and `engine/prefabs/irreden/` opens with
   "Key components" / "Key systems" sections that mirror
   `ls engine/<module>/components/` and `ls engine/<module>/systems/`.
   These are exactly the "Catalogs of type, class, or struct names.
   Agents can Grep for them" that baseline §"What belongs in CLAUDE.md
   files" explicitly forbids. The valuable content is mixed into the
   catalog (per-component gotchas, lifecycle quirks) and would be lost
   if naively deleted — the cleanup is per-entry triage, not a sed
   pass.
2. **ASCII directory trees recur** in small files (`engine/common/`,
   `engine/utility/`, `engine/profile/`, `engine/CLAUDE.md`,
   `engine/prefabs/CLAUDE.md`, `creations/CLAUDE.md`, root). All
   violate baseline §"What belongs in CLAUDE.md" on "File/directory
   tree listings or layout blocks. Agents can Glob/Grep." Several
   are demonstrably stale (e.g. `engine/profile/` tree omits
   `profile_report.hpp` which exists; `creations/demos/`
   "Current demos" list is missing 5 of the on-disk demos).
3. **SQT transition isn't acknowledged consistently.** T-199 is
   actively migrating consumers from `C_Position3D`/`C_PositionGlobal3D`/
   `C_Rotation` to `C_WorldTransform` + `C_LocalTransform`.
   `engine/prefabs/irreden/common/CLAUDE.md` overstates ("retired in
   T-199 migration"); `engine/prefabs/CLAUDE.md` undersells (no
   mention); `engine/prefabs/irreden/render/CLAUDE.md` uses the legacy
   names in code examples without a transition note. A single
   consistent "in flight under T-199" note across the affected files
   would close the drift.
4. **Component-method rules are duplicated between `engine/prefabs/CLAUDE.md`
   and `.claude/rules/cpp-ecs.md`** verbatim (several paragraphs are
   identical word-for-word, including the C_PeriodicIdle example and
   the C_VoxelSetNew/C_ShapeDescriptor exceptions). The baseline
   explicitly names `engine/prefabs/CLAUDE.md` as the canonical home
   for the categorization, so the rule file should reference it, not
   duplicate it. Same canonical-home problem for the
   "Three valid TICK function signatures" block (system/CLAUDE.md
   vs `.claude/rules/cpp-systems.md`) and the "no function-local
   static" anti-pattern.
5. **"Section header for an empty / one-sentence body"** pattern recurs
   (`engine/prefabs/irreden/audio/CLAUDE.md`'s "Commands\n\nNone.";
   `engine/prefabs/irreden/common/CLAUDE.md`'s "Systems" header for a
   one-line "None" body; `engine/prefabs/irreden/video/CLAUDE.md`'s
   three section headers each saying "this is stubs"). The header is
   ceremonial; the content belongs in the intro paragraph.

---

## Per-file findings

### CLAUDE.md (root)

**Total lines:** ~91

**Baseline duplications:**
- L57–60 ("never commit to master", "never `--force`", "never
  `--no-verify`) restates content covered in `docs/agents/FLEET.md`
  referenced just above. Borderline — these aren't literally in
  CLAUDE-BASELINE.md, so this is closer to "TL;DR of the linked
  doc" than a duplication. Likely fine to keep as a safety-TL;DR.

**Dead pointers:**
- (none) — all links to `docs/agents/AGENTS-ARCHITECTURE.md`,
  `CLAUDE-BASELINE.md`, `BUILD.md`, `FLEET.md`,
  `docs/design/claude-md-sharing.md` resolve.

**Rule contradictions:**
- (none)

**Slop:**
- L33 (last row of the "Where to find things" table) — "Cross-repo
  info isolation rule → CLAUDE-BASELINE.md" is already implied by
  the first row ("Coding conventions … → CLAUDE-BASELINE.md").
  Minor.

**Missing patterns:**
- (none — root is intentionally a thin shim.)

**Layout dumps:**
- L71–82: ASCII tree of `engine/` and `creations/` subdirs. Mirrors
  `ls`; the per-module CLAUDE.md files are linked from each subdir's
  own document. Violates baseline §"What belongs in CLAUDE.md files"
  ("Do NOT include: File/directory tree listings"). Delete the tree;
  keep the two sentences below.

---

### engine/CLAUDE.md

**Total lines:** ~109

**Baseline duplications:**
- (none direct.)

**Dead pointers:**
- L54: cites `engine/system/include/irreden/ir_system_types.hpp` —
  actual path is `engine/system/include/irreden/system/ir_system_types.hpp`
  (extra `system/` segment). Verified via Glob.
- L84–88: references `C_PositionGlobal3D`, `GLOBAL_POSITION_3D`,
  `APPLY_POSITION_OFFSET` as the "legacy" path. All still exist as of
  today; the file does flag the transition correctly. Aging, not dead.

**Rule contradictions:**
- (none)

**Slop:**
- L1–3 ("Core engine static libraries. Everything here is shared by
  every creation.") — borderline tautological intro. The "Layer map"
  below carries the actual content.

**Missing patterns:**
- (none concrete)

**Layout dumps:**
- L20–44: `## Layer map (bottom-up)` is an ASCII tree of every engine
  module with a one-line description per entry. **The largest layout
  dump in the file.** The next sentence ("Read the CLAUDE.md inside
  each subdirectory") admits the layout dump is redundant — every
  subdir already has its own CLAUDE.md. Direct baseline violation.

---

### engine/system/CLAUDE.md

**Total lines:** ~261

**Baseline duplications:**
- L56–82 ("Three valid TICK function signatures") — duplicated
  verbatim in `.claude/rules/cpp-systems.md`. Pick a canonical home.
  Since this is the closest-to-code doc, the rule file should
  reference here, not duplicate.
- L210–227 ("Don't use function-local `static` for system state") —
  the entire anti-pattern explanation (four bullets +
  perf-argument rationale) is duplicated verbatim in
  `.claude/rules/cpp-systems.md`. Same canonical-home problem.

**Dead pointers:**
- L228–231 cites `.fleet/status/system-static-deviations.md` —
  **(unverified)** in this audit pass.

**Rule contradictions:**
- (none)

**Slop:**
- L196–200: post-explanation snippet showing both
  `getSystemParams<MyParams>` and `getSystemParams<System<MY_NAME>>`
  is very close in spirit to the example above; trim-candidate.

**Missing patterns:**
- (none concrete)

**Layout dumps:**
- L10–14 ("Public API" section listing `IRSystem::` function names
  verbatim) — function-name catalog mirrors `grep IRSystem::`.
  Delete; the sections below carry the actual contract.

---

### engine/entity/CLAUDE.md

**Total lines:** ~253

**Baseline duplications:**
- (none)

**Dead pointers:**
- L217–230 cites `C_PositionGlobal3D`, `C_LocalTransform`,
  `C_WorldTransform`, `POSITION_OFFSET_3D`, `TRANSFORM_TRANSLATION`,
  `TRANSFORM_SCALE` — all verified. T-199 transition is noted
  appropriately ("legacy … and the canonical … channel coexist").

**Rule contradictions:**
- (none)

**Slop:**
- L1–3: borderline definitional ("archetype-based ECS: groups
  entities sharing the same set of component types into dense
  arrays") — restates the meaning of the term rather than a
  project-specific gotcha.

**Missing patterns:**
- (none concrete)

**Layout dumps:**
- L13–30 (`## Key types` — flat catalog of `EntityId`, `ComponentId`,
  `Archetype`, `ArchetypeNode`, `ArchetypeGraph`, `Relation` each
  with one-paragraph descriptions). Type-name catalog. Some entries
  carry real gotchas (the `IR_ENTITY_ID_BITS` mask note) — relocate
  those to a gotcha section before deleting the catalog.
- L7–12 ("Entry point" lists `IREntity::` free functions). Same
  baseline violation.

---

### engine/world/CLAUDE.md

**Total lines:** ~78

**Baseline duplications:**
- (none)

**Dead pointers:**
- L33: cites `initEngineSystems()`, `initIRInputSystems()`,
  `initIRUpdateSystems()`, `initIRRenderSystems()`. The world header
  also declares `initIROutputSystems()` and `initEngineCommands()`
  — the doc omits them. Mildly stale (incomplete).
- L23–25 manager construction order matches actual member
  declaration order in `world.hpp`. ✓

**Rule contradictions:**
- (none)

**Slop:**
- L75 ("If video recording is not starting, check these flags
  first.") — borderline; kept terse and actionable.

**Missing patterns:**
- `enableFrameTiming` is exposed via `world.hpp:33` but isn't
  mentioned. Low priority unless other agents need to touch it.

**Layout dumps:**
- (none of substance — the "Entry point" sentence is two lines.)

---

### engine/render/CLAUDE.md

**Total lines:** ~535

**Baseline duplications:**
- L137–143 (shader naming prefixes `c_`/`v_`/`f_`/`g_`) restates
  CLAUDE-BASELINE.md's Naming table directly. The same prefixes
  appear in `.claude/rules/cpp-ecs.md`. Delete the local prefix
  list; keep the example file names.

**Dead pointers:**
- L240: cites `engine/render/tests/render-baselines/` — **directory
  does not exist** (verified via Glob). The `render-debug-loop`
  skill references it inconsistently too. PR authors following the
  CLAUDE.md instruction ("refresh the affected directory under
  engine/render/tests/render-baselines/") will fail. **High-priority
  fix.**
- L344: cites task `T-09Y` ("scheduled for removal in T-09Y") —
  looks like a placeholder that never got filled in. **(unverified —
  flag as placeholder.)**
- L98 cites `IRRender::mouseWorldPos3DAtIsoDepth` — **(unverified)**.

**Rule contradictions:**
- (none)

**Slop:**
- L13–30 ("Key exposed surface") — bulleted catalog of `IRRender::`
  function names. Layout dump. Mirrors
  `grep "IRRender::" engine/render/include/irreden/ir_render.hpp`.
- L119–131 ("Key components") — `C_*` catalog with one-line
  descriptions. Same.
- L133–146 ("Shaders") — prefix list duplicates baseline.
- L41–56 (C_GizmoHandle field documentation) — mixes
  per-field inventory (`referenceParams_`, `referenceLocalPos_`,
  `isAnchor_`, `hover_`, `baseColor_`, `anchorEntity_`) with semantic
  notes. Fields belong in the header.

**Missing patterns:**
- SQT transition note for the L42, L48, L209, L213–218 references to
  `C_Position3D`. Code is accurate today but will rot under T-199.

**Layout dumps:**
- L13–30 + L119–131 + parts of L46–113 (per-stage inventory).
  L254–268 (ASCII pipeline ordering block) is load-bearing —
  documents the *ordering* constraint. Keep that.

---

### engine/math/CLAUDE.md

**Total lines:** ~109

**Baseline duplications:**
- L8–12 (`## GLM alias rule`) — restates the baseline §Style
  bullet "All math primitives flow through `IRMath`" and
  `.claude/rules/cpp-math.md`. Trim to one sentence + cite.
- L14–36 (`## Isometric projection — the equations`) — duplicated
  **verbatim** in `.claude/rules/cpp-math.md`. Pick a canonical home.
  Since the equations are math-implementation details, the math
  CLAUDE.md is the right home — the rule file should reference here.

**Dead pointers:**
- L80–88 cites `IRComponents::Joint::rotation_`,
  `IRAsset::RigJoint::rotation_`, `IRPrefab::Rig::worldTransformForBindPoint`,
  `IREntity.bindPoint(entity, name)` — **(unverified)**.

**Rule contradictions:**
- (none)

**Slop:**
- L37–95: `## Layout helpers`, `## Color`, `## Physics`,
  `## Quaternions`, `## Random` — each is a function-signature
  catalog mirroring `grep "namespace IRMath" engine/math/include/`.
  **The middle 60 lines is essentially `ls`** of the math headers.
  Compress to one paragraph per section with the actionable
  gotcha — e.g. "`color.hpp`: HSV conversion, palette ops,
  lerpColor. Gotcha: `applyHSVOffset` works in-place on packed
  u8 RGBA; don't mix with float HSV in the same expression."

**Missing patterns:**
- (none concrete)

**Layout dumps:**
- L37–95 (see Slop).

---

### engine/script/CLAUDE.md

**Total lines:** ~1016

**Baseline duplications:**
- (none — file stays in Lua-specific lane.)

**Dead pointers:**
- L430, L568 reference `IRComponent.C_Position3D` and
  `IRComponent.C_Velocity3D` in code examples. Both exist today but
  C_Position3D is being phased out per T-199.
- L1010–1015 "Prefab registry is process-global" — accurate.

**Rule contradictions:**
- (none)

**Slop:**
- L984 (`## C++ ↔ Lua math type helpers` table with one row
  `vec3FromLua`) — table for one entry is overkill.
- L411–418 "Future-hook note" about `kEvalSystemNames[]` array that
  "no consumer reads yet" — forward-looking commentary on dead code.
  Belongs in a follow-up issue, not a CLAUDE.md.

**Missing patterns:**
- (none concrete — file is dense with module-specific gotchas as
  appropriate.)

**Layout dumps:**
- L924–948: long enum-value catalog (`IRInput.Key.{A..Z}`,
  `GamepadButton.*`, `GamepadAxis.*`). Direct baseline violation —
  goes stale on the next enum addition. Replace with pointer to
  `lua_command_bindings.hpp` plus one pattern example.

---

### engine/asset/CLAUDE.md

**Total lines:** ~408

**Baseline duplications:**
- (none — module-specific format details throughout.)

**Dead pointers:**
- L389: cites `.claude/skills/simplify/SKILL.md section 2c` and
  `review-pr/SKILL.md step 4 "Serialization"`. Section/step
  references will drift; more durable as symbol-level. **(unverified.)**
- L406–408 voxel-image stub `kVoxelImage`/`kSpriteImage` — verified
  in `ir_asset.hpp:17`.

**Rule contradictions:**
- (none)

**Slop:**
- L103–107 ("Typical usage") — generic background, not a gotcha.
- L17–20 + L309–346: the seven rules are inlined here and also
  live in `docs/design/entity-editor-epic.md`. The own-it-here
  pattern is intentional; the lead-in disclaimer is wordy.
- L396–408 ("Gotchas") — 3 items, one borderline tautological
  ("paths are joined with joinPath… double-check when debugging").

**Missing patterns:**
- (none concrete)

**Layout dumps:**
- The chunk-layout blocks (L51–58, L71–90, L229–239) are wire-format
  specifications, not directory dumps. Keep.

---

### engine/audio/CLAUDE.md

**Total lines:** ~74

**Baseline duplications:**
- (none)

**Dead pointers:**
- L57 `C_AudioFile — placeholder, minimal` — verified only mentioned
  in this CLAUDE.md, never implemented. Either delete or rephrase as
  "(planned: audio-file loading not yet implemented)".
- L56 `C_MidiDevice`, `C_MidiChannel`, `C_MidiDelay` — all verified.

**Rule contradictions:**
- (none)

**Slop:**
- L50–58 ("Key components") + L59 ("Plus `*_lua.hpp` variants…") —
  name catalog that duplicates what `engine/prefabs/irreden/audio/CLAUDE.md`
  already covers. Delete; defer to the prefab-side doc.

**Missing patterns:**
- (none concrete)

**Layout dumps:**
- L50–59 (see Slop).

---

### engine/video/CLAUDE.md

**Total lines:** ~112

**Baseline duplications:**
- (none)

**Dead pointers:**
- L87–89 lists `C_FramebufferCapture`, `C_FramebufferOutputPosition`,
  `C_OutputResolution`. Verified via Grep: **these names appear ONLY
  in this file.** No corresponding header files or usages.
  Genuinely dead — never implemented or removed without updating
  this doc.
- L83–85 (`command_take_screenshot`, `command_take_screenshot_canvas`,
  `command_toggle_recording`) — files exist. ✓

**Rule contradictions:**
- (none)

**Slop:**
- L86–90 ("Commands and components" section) — half-dead catalog.

**Missing patterns:**
- (none concrete)

**Layout dumps:**
- L82–90 (see above).

---

### engine/input/CLAUDE.md

**Total lines:** ~92

**Baseline duplications:**
- (none)

**Dead pointers:**
- L62–65 references `C_Hitbox2D` as "a screen-space axis-aligned
  rectangle on an entity." Verified via Grep: **`C_Hitbox2D` appears
  ONLY in this CLAUDE.md.** Actual components are `C_HitboxRect` and
  `C_HitboxCircle` (under `update/components/`). Name was likely
  renamed and never reflected here. **High-priority fix** — the
  surrounding hover-callback section may also be stale.
- L29 `C_GLFWGamepadState` — exists. ✓

**Rule contradictions:**
- (none direct, but see above.)

**Slop:**
- L72–91 ("Gotchas") — "Lua callback lifetime" duplicates the same
  gotcha in `engine/script/CLAUDE.md` and `engine/command/CLAUDE.md`.
  Consolidate to one canonical home + pointer.

**Missing patterns:**
- (none beyond fixing the `C_Hitbox2D` references)

**Layout dumps:**
- (none)

---

### engine/command/CLAUDE.md

**Total lines:** ~117

**Baseline duplications:**
- (none)

**Dead pointers:**
- L84–87 + L99–104 — verified.

**Rule contradictions:**
- (none)

**Slop:**
- L51–77 ("Lua-defined commands (T-193)") substantially duplicates
  `engine/script/CLAUDE.md` "Commands and input" (L860–963). Both
  files describe `IRCommand.{bindPrefab, createCommand, fire,
  fireByName}`. This file should defer to `engine/script/CLAUDE.md`
  for the Lua surface and only document the C++-side entry points.
- L106–107: the "command list in the debug overlay is informational"
  sub-clause is implicit in L114–116. Minor.

**Missing patterns:**
- (none concrete)

**Layout dumps:**
- (none)

---

### engine/window/CLAUDE.md

**Total lines:** ~55

**Baseline duplications:**
- (none)

**Dead pointers:**
- (none — stable surfaces only.)

**Rule contradictions:**
- (none)

**Slop:**
- (none — file is tight and on-mission.)

**Missing patterns:**
- Borderline: nothing about `g_irglfwWindow` being set/cleared by
  World (parallels `engine/CLAUDE.md`'s "Manager globals" section).
  Could note that the manager-globals rule applies here too.

**Layout dumps:**
- (none)

Cleanest large-ish file in the engine layer.

---

### engine/common/CLAUDE.md

**Total lines:** ~58

**Baseline duplications:**
- (none)

**Dead pointers:**
- L19 `IRConstants::kFPS = 60` — verified.
- **L20 `IRConstants::kChunkSize = 32` is inaccurate.** Actual type at
  `ir_constants.hpp:19` is `constexpr uvec3 kChunkSize = uvec3{32, 32, 32}`.
  The doc presents it as scalar `= 32`.
- L21–22 `IRConstants::kTrixelDistanceMaxDistance` — verified.

**Rule contradictions:**
- (none)

**Slop:**
- L38–46 ("Internal layout" section with explicit directory tree:
  `engine/common/ └── include/irreden/ ├── ir_constants.hpp └── …`).
  Textbook example of what baseline §"What belongs in CLAUDE.md"
  forbids. The "No `src/` — everything is header-only" line is the
  only useful info; the 9-line ASCII tree mirrors `ls`.

**Missing patterns:**
- (none concrete — GLSL-mirror gotcha at L56–58 is the right kind
  of content.)

**Layout dumps:**
- L38–46 (see Slop).

---

### engine/profile/CLAUDE.md

**Total lines:** ~81

**Baseline duplications:**
- (none)

**Dead pointers:**
- L62 "internal layout" tree omits `profile_report.hpp` (exists at
  `engine/profile/include/irreden/profile/profile_report.hpp` and
  is implemented in `src/profile_report.cpp`). Layout-dump
  already proven stale.

**Rule contradictions:**
- (none)

**Slop:**
- L1–5 intro borderline tautological.
- L48 ARGB hex trivia ("0xff0000ff = pure blue, 0xffff0000 = pure
  red") — color-channel order is the real gotcha but isn't called
  out; the example pairs add nothing.

**Missing patterns:**
- No mention of the `IR_PROFILER_COLOR_*` named constants used
  elsewhere (e.g. `IR_PROFILER_COLOR_RENDER`). Hard-coded ARGB in
  callers is the anti-pattern compared to the named macros.

**Layout dumps:**
- L54–64 ("Internal layout" — already stale, see above). Delete.

---

### engine/time/CLAUDE.md

**Total lines:** ~75

**Baseline duplications:**
- (none — fixed-step-loop specifics throughout.)

**Dead pointers:**
- (none — `TimeManager`, `EventProfiler`, `beginEvent<T>`,
  `deltaTime`, `shouldUpdate`, `skipUpdate` all verified.)

**Rule contradictions:**
- (none)

**Slop:**
- L40–56 fixed-step loop code example — pattern is illustrative,
  allowed by baseline, but worth occasionally verifying against
  current `World::gameLoop()`. Borderline.

**Missing patterns:**
- No pointer to where `kFPS` lives (`ir_time_types.hpp` or similar)
  — a reader hitting "the UPDATE accumulator has buffered at least
  one frame period (1/kFPS)" gets no source-of-truth pointer.
- The 10 ms slow-tick threshold is stated as fact at L36 with no
  "where to change it" pointer.

**Layout dumps:**
- (none — refreshingly clean.)

---

### engine/utility/CLAUDE.md

**Total lines:** ~51

**Baseline duplications:**
- (none)

**Dead pointers:**
- L9 cites `engine/utility/ir_utility.hpp` — actual path is
  `engine/utility/include/irreden/ir_utility.hpp`. Minor.

**Rule contradictions:**
- (none)

**Slop:**
- L13–24 ("What's here") is a function-by-function inventory of 4
  free functions. Direct baseline violation. Replace with one
  sentence: "Path/file helpers consumed by `asset/`, `video/`, and
  creation startup — Grep `IRUtility::` for the current set."
- L48–51 ("Don't grow this into a kitchen sink") — actionable, but
  the redirection examples are off ("`math/` for random,
  `profile/` for time scaffolding" — random doesn't semantically
  belong in `math/`; `profile/` doesn't own time scaffolding).

**Missing patterns:**
- (none)

**Layout dumps:**
- L27–33 (ASCII tree of `engine/utility/`). Delete entirely.

---

### engine/prefabs/CLAUDE.md

**Total lines:** ~173

**Baseline duplications:**
- L19–22 "Math primitives go through IRMath…" restates the baseline
  §Style IRMath bullet. The file already auto-loads
  `.claude/rules/cpp-math.md`; trim the 4-line capsule to one
  sentence.
- L32–36 "No per-entity `getComponent` inside a system tick"
  restates the baseline "ECS — the single biggest footgun" capsule.
  Same redundancy.
- L93–147 ("Component method rules" §(a)/(b)/(c) + Documented
  exceptions) is **near-verbatim** to `.claude/rules/cpp-ecs.md`
  "Component method tiers" + "Documented exceptions to (c)" —
  several paragraphs are identical word-for-word (the C_PeriodicIdle
  example, the C_VoxelSetNew/C_ShapeDescriptor exception).
  CLAUDE-BASELINE.md L74–79 explicitly names *this file* as the
  canonical home, so the rule file should reference here, not
  duplicate.
- L150–173 ("Anti-patterns") — items 1, 2, 6, 7 restate
  `.claude/rules/cpp-ecs.md` rules. Same canonical-home problem.

**Dead pointers:**
- L68–70 cites `engine/prefabs/irreden/update/nav_query.hpp` as
  example of a free-functions-not-system. **Verified file does
  exist** (referenced by `engine/prefabs/CLAUDE.md` exception list);
  the original "stale" call was wrong. ✓ — keep the example.
- L49 ("asset/ — asset-related prefabs (currently small)") — accurate
  but the hedge is itself a sign the layout block needs maintenance.
- L50–51 demo/, wip/ — both exist. ✓

**Rule contradictions:**
- L94–97 ("(a) Pure data. ... Most components in `common/`,
  `input/`, and tag-style components fall here.") doesn't acknowledge
  the SQT transition — `common/` currently houses both legacy
  `C_Position3D` and new `C_WorldTransform`/`C_LocalTransform`.

**Slop:**
- L60–66 ("File pattern" table) — actual pattern documentation, keep.
- L155–163 (anti-patterns 4 and 5: "Storing references to other
  entities' component storage" / "Cross-domain includes inside a
  prefab header") are prefab-specific gotchas — keep.

**Missing patterns:**
- No `wip/` and `demo/` policy documentation (what graduates out of
  `wip/`? what removes from `demo/`?).
- No SQT transition note.

**Layout dumps:**
- L40–52 ("Layout" section — directory inventory of
  `engine/prefabs/irreden/`). Direct baseline violation. Delete.

---

### creations/CLAUDE.md

**Total lines:** ~111

**Baseline duplications:**
- (none — file correctly delegates via "Inherits from engine
  baseline" at L8–10.)

**Dead pointers:**
- L18 cites `creations/editors/voxel_editor/` as "checked into git;
  engine editor tool." **Directory does not exist in this worktree.**
  The `.gitignore` block L29–34 also references
  `!creations/editors/voxel_editor/**` — pointing at non-existent
  path. Either the editor was removed without doc cleanup, or it
  hasn't landed yet (T-211 in flight may produce it).
- L74–75 `creations/template/` (parenthetical hedge "if present;
  template is gitignored") — acceptable.

**Rule contradictions:**
- (none)

**Slop:**
- L23–34: `.gitignore` block pasted verbatim. Drifts the moment
  `.gitignore` is edited. Replace with a one-liner pointer.
- L77–95 ("CMake boilerplate") — lengthy walkthrough that ends with
  "Use `creations/demos/shape_debug/CMakeLists.txt` as the canonical
  reference." Makes the whole section redundant; trim to one bullet
  + the MinGW-DLL gotcha at L86–88 (the only piece worth keeping).

**Missing patterns:**
- (none — the gotchas section is well-targeted.)

**Layout dumps:**
- L14–21 (`creations/` subdir tree). Half the listed entries are
  gitignored or absent. Replace with prose.

---

### creations/demos/CLAUDE.md

**Total lines:** ~97

**Baseline duplications:**
- (none — properly delegates via "Inherits from engine baseline".)

**Dead pointers:**
- L16–48 ("Current demos") covers `default`, `shape_debug`,
  `midi_keyboard`, `metal_clear_test`, `lighting`, `modifier_demo`,
  `sprite_demo`, `lua_pipeline_demo`, `z_yaw_rotation`,
  `lua_perf_grid`. **Actual `creations/demos/` directory contains
  additionally `gpu_particles`, `perf_grid`, `stateless_particles`,
  `ui_dockspace`, `ui_widgets`** — 5 demos on disk but not listed.
  Classic layout-dump failure mode.

**Rule contradictions:**
- (none)

**Slop:**
- L51–67 ("Adding a new demo") 5-step recipe re-explains
  `CMakeLists.txt` boilerplate already covered by
  `creations/CLAUDE.md` and `shape_debug/CMakeLists.txt`. Trim.

**Missing patterns:**
- The demo list goes stale (file is itself proof). Either drop the
  inventory or add a "verify with `ls creations/demos/`"
  disclaimer.

**Layout dumps:**
- L16–48 ("Current demos") — see Dead pointers.

---

### engine/prefabs/irreden/asset/CLAUDE.md

**Total lines:** ~28

**Baseline duplications:**
- (none)

**Dead pointers:**
- L13 cites "(plus a `.vxs.json` sidecar; Rule #6)" — no in-document
  anchor and no `.claude/rules/` registry has "Rule #6". Likely a
  left-over reference. **(unverified — Grep found no canonical
  home.)**
- L18–19 `IRAsset::loadShapeGroup` and
  `<irreden/asset/voxel_set_format.hpp>` — verified.

**Rule contradictions:**
- (none)

**Slop:**
- L22–27 "Typical usage" snippet — `#include` + one function call
  the listing already explained.

**Missing patterns:**
- The round-trip contract (parallel-spans on save, `ShapeRecord`
  reconstruction on load) isn't documented. For an adapter that
  bridges component layer to asset layer, that's the actionable
  gotcha.

**Layout dumps:**
- (none)

---

### engine/prefabs/irreden/audio/CLAUDE.md

**Total lines:** ~67

**Baseline duplications:**
- (none)

**Dead pointers:**
- L48 cites `entity_midi_sequence_animated.hpp` as an "Entity builder"
  — file exists but is a documented stub (file header says "STATUS:
  stub. PrefabTypes::kMidiSequenceAnimated is declared but no
  creation instantiates it yet").
- L65–67 "There is no audio-device-manager system yet" —
  `engine/prefabs/irreden/audio/systems/system_audio_device_manager.hpp`
  **does exist** as a non-compiling WIP (placeholder `system_name`
  / `components…` syntax). Directionally true; misleading for a
  Grep-first reader.

**Rule contradictions:**
- (none)

**Slop:**
- L9–22 ("Key components" 7-entry catalog) and L26–37 ("Key
  systems" 4-entry catalog) — both name-catalog shape. The
  `C_MidiNote::onDestroy()` NOTE_OFF teardown is the genuine
  gotcha worth promoting.
- L39–40 ("Commands\n\nNone. MIDI control is entirely
  entity-driven.") — empty section to assert emptiness. Delete.
- L41–48 ("Entity builders" 3-entry catalog with one stub).

**Missing patterns:**
- The **ephemeral-entity + `C_Lifetime{1}` pattern** is the
  generalizable gotcha across the audio domain — mentioned in
  passing twice (L31, L57). Promote to a named pattern: "MIDI
  messages travel as one-frame ephemeral entities," with the
  system-ordering caveat (L56–58).
- No mention of the `system_audio_device_manager.hpp` WIP stub.

**Layout dumps:**
- L9–22 + L26–37 (see Slop).

---

### engine/prefabs/irreden/common/CLAUDE.md

**Total lines:** ~309

**Baseline duplications:**
- (none — stays in per-module lane: SQT propagation, modifier
  framework. The "Component method rules" duplication that
  `engine/prefabs/CLAUDE.md` carries is absent here.)

**Dead pointers:**
- L15 `position_modifier_fields.hpp` — exists. ✓
- L46 update/systems/system_propagate_transform.hpp — exists. ✓
- L74, L113, L153: uses `SYSTEM_PROPAGATE_TRANSFORM` as the
  comment-banner name; actual `SystemName` enum value is
  `PROPAGATE_TRANSFORM` (no `SYSTEM_` prefix) per
  `ir_system_types.hpp:56`. Mildly drift-prone — readers searching
  the enum will miss. The sibling `update/CLAUDE.md` uses the
  correct bare form.

**Rule contradictions:**
- L9–18: phrases `C_Position3D`, `C_PositionGlobal3D`, `C_Rotation`
  as **"retired in T-199 migration" / "superseded by
  C_LocalTransform"** — overstates the current state. Components
  are still active today; T-199 is in flight (status `~`). Soften
  to "in flight under T-199."

**Slop:**
- L31–34 "## Systems" heading with a one-sentence "None" body.
- L302–308 "**No systems means no ownership**" gotcha duplicates the
  same point.

**Missing patterns:**
- (none — the SQT composition formula, topological-ordering
  invariant, auto-attach conflict, modifier compose order are
  exactly the right content.)

**Layout dumps:**
- L7–28 ("Key components" 22-line bullet catalog). Some entries
  carry real lifecycle gotchas (auto-add, modifier routing) — keep
  those; prune the tag-only entries (`C_Name`, `C_Player`,
  `C_Selected`).

---

### engine/prefabs/irreden/demo/CLAUDE.md

**Total lines:** ~27

**Baseline duplications:**
- (none)

**Dead pointers:**
- L9 `components/component_example.hpp` and L12
  `entities/entity_example.hpp` — both exist. ✓

**Rule contradictions:**
- (none)

**Slop:**
- L7–14 ("Contents") — 2-item file inventory paraphrasing what
  `Read`-ing the file would tell you. Borderline.

**Missing patterns:**
- (none — "don't copy from here" rule is documented.)

**Layout dumps:**
- L7–14 (defensible at 2 entries).

---

### engine/prefabs/irreden/input/CLAUDE.md

**Total lines:** ~99

**Baseline duplications:**
- (none)

**Dead pointers:**
- L34–36 vs L85: file mixes `beginTick` (L36) and `beforeTick` (L85)
  for the same system's camera-pos/zoom cache. Verify which one
  `HITBOX_MOUSE_TEST` actually overrides; one is wrong.
- All `C_*` and enum values (L8–26, L30–51) — verified.

**Rule contradictions:**
- (none)

**Slop:**
- L75–80 ("Entity builders" 2-entry catalog).
- L69–71 "## Commands" header with one bullet — ceremonial.

**Missing patterns:**
- (none — GUI-vs-world coordinate gotcha is well-explained.)

**Layout dumps:**
- L8–26 ("Key components" — 8 bullets). Several entries are pure
  catalog with no actionable rule (`C_KeyboardKey — GLFW key
  code`, `C_MousePosition — cached cursor`). Prune.
- L75–80 (see Slop).

---

### engine/prefabs/irreden/render/CLAUDE.md

**Total lines:** ~355

**Baseline duplications:**
- L351–354 ("**`SystemName` enum registration.** Every render
  system name … must exist in
  `engine/system/include/irreden/ir_system_types.hpp`") — project-wide
  rule, also covered in `engine/prefabs/CLAUDE.md` and
  `engine/CLAUDE.md`. Delete; parent docs cover it.

**Dead pointers:**
- L42, L48, L209, L213–218: code uses `C_Position3D` /
  `IREntity::getComponent<C_Position3D>(gizmo).pos_ = anchor`.
  Aging-but-accurate per T-199; flag the transition.
- L83–90 `SPRITE_TO_SCREEN`, `SPRITE_ANIMATION_ADVANCE` — verified.
- L93–103 `VOXEL_PICKING`, `IRPrefab::Picking::castVoxelRay` —
  verified.
- L98 `IRRender::mouseWorldPos3DAtIsoDepth` — **(unverified.)**

**Rule contradictions:**
- L70 ("## Key systems (all RENDER pipeline)") — header lies. L83
  `SPRITE_TO_SCREEN` is post-FRAMEBUFFER_TO_SCREEN composite; L93
  `VOXEL_PICKING` is INPUT-driven, not RENDER.

**Slop:**
- L8–68 ("Key components") — 60-line catalog mixing genuine
  semantic content with pure one-liners (`C_Camera — tag`,
  `C_CameraPosition2DIso — iso-space position`, `C_ZoomLevel — float
  zoom`, `C_CameraYaw — continuous Z-yaw`).
- L41–56 (`C_GizmoHandle` per-field documentation) — fields belong
  in the header, not the CLAUDE.md.

**Missing patterns:**
- SQT transition note (see Dead pointers).

**Layout dumps:**
- L8–68 + L70–103. L254–268 (pipeline-wiring ordering block) is
  load-bearing — keep.

---

### engine/prefabs/irreden/update/CLAUDE.md

**Total lines:** ~77

**Baseline duplications:**
- (none)

**Dead pointers:**
- All `C_*` components, all system enum values — verified. ✓

**Rule contradictions:**
- (none)

**Slop:**
- L7–17 ("Key components") — short catalog. The `C_Velocity3D —
  vec3 in blocks per second, not pixels` is the gotcha worth keeping;
  `C_Lifetime — integer countdown; destroyed when ≤ 0` is borderline.
- L48–51 ("Commands" single-item under its own header) — ceremonial.

**Missing patterns:**
- (none — pipeline-ordering gotcha is named with rationale.)

**Layout dumps:**
- L7–17 + L19–45. The system entries carry real content (PROPAGATE_TRANSFORM
  SQT formula, PERIODIC_IDLE_POSITION_OFFSET slot-key shape). Several
  entries (`VELOCITY_3D — applies velocity * deltaTime(UPDATE) to
  position`) are pure name-restatement; prune those.

One of the cleaner files. Light pruning would tighten it.

---

### engine/prefabs/irreden/video/CLAUDE.md

**Total lines:** ~42

**Baseline duplications:**
- (none)

**Dead pointers:**
- L9 `C_FramebufferCapture` — exists in
  `engine/prefabs/irreden/video/` (verified, only referenced by this
  CLAUDE.md and `engine/video/CLAUDE.md`).
- L33 `IR_VIDEO_HAS_FFMPEG` — verified. ✓

**Rule contradictions:**
- L28–29 "Adding `C_FramebufferCapture` to an entity *does nothing*
  right now." Doesn't contradict but flags this as a "write a demo"
  candidate per baseline §"Engine API removal rule" (no removal;
  write a demo).

**Slop:**
- L8–13 + L27–29 — same point repeated (components are stubs).
  Remove one.
- L15–19 ("Commands" — vague three-sentence summary that doesn't
  name the commands). Either name them or drop.
- L21–25 ("Systems" — "A video encoder system exists but is largely
  commented out" — vague; doesn't name the file).

**Missing patterns:**
- The "no per-entity recording state machine; recording is global"
  is the actual gotcha — sharpen.

**Layout dumps:**
- (none — too short.)

File feels half-empty; collapse three sections into one "Status: mostly
placeholder" paragraph + Gotchas.

---

### engine/prefabs/irreden/voxel/CLAUDE.md

**Total lines:** ~195

**Baseline duplications:**
- (none)

**Dead pointers:**
- L8–40 all `C_*` — verified. ✓
- L29–31 `C_JointHierarchy — DEPRECATED; superseded by C_Skeleton +
  C_Joint` matches the header's deprecation notice. ✓
- L44–45 `UPDATE_VOXEL_SET_CHILDREN`, `VOXEL_SQUASH_STRETCH` —
  verified.
- L82, L94 `getActiveCanvasEntityOrNull()` — **(unverified.)**
- L116–119 `kBufferIndex_JointTransforms` — verified.
- L142–151 `IRPrefab::Skeleton::severJoint(rigRoot, joint);` — file
  explicitly flags "The severance API is declared in this design,
  not yet implemented." Exemplary aspirational marking. Keep.

**Rule contradictions:**
- (none)

**Slop:**
- L46–49 "A pool hierarchy/sort system exists but is commented out"
  + "A WIP scene/skeleton hierarchy traversal system is present
  but incomplete." Neither names the file.
- L167–173 "Optional follow-up: C_JointName" — design-doc material,
  not module gotcha. Move to `docs/design/` or TASKS.md.

**Missing patterns:**
- (none — headless/staged-mode contract, bone-index stability,
  pool-per-canvas are the right kind of gotcha to document here.)

**Layout dumps:**
- L8–40 "Key components" — 32-line catalog. Several entries restate
  the header. Keep the non-obvious bits (one-pool-per-canvas,
  std430 layout-matching, deprecation note); prune the rest.

Substantively strong on the entity-based-joints design block
(L111–173) — exactly the cross-cutting design rationale baseline
§"What belongs in CLAUDE.md" endorses.

---

### engine/prefabs/irreden/wip/CLAUDE.md

**Total lines:** ~26

**Baseline duplications:**
- (none)

**Dead pointers:**
- L9 `components/component_alarm.hpp` / `C_Alarm` — exists. ✓
- L23 `lint` and `format-check` CMake targets — both exist in
  `cmake/ir_quality_tools.cmake`. ✓

**Rule contradictions:**
- (none)

**Slop:**
- L8–11 ("Contents" — 1-file inventory). The explanation that the
  component is a placeholder is the actionable bit; the inventory
  shape is borderline. Defensible at 1 entry.

**Missing patterns:**
- (none — graduation procedure L18–22 and "must still compile" rule
  are exactly the gotchas that belong here.)

**Layout dumps:**
- L8–11 (defensible).

Cleanest file in the audit.

---

## Follow-up cleanup tasks (one PR each)

Each issue below is sized for a single PR and is intentionally scoped to
one subtree so the diffs stay reviewable and conflicts stay rare. Filed as
GitHub issues (no labels — queue-manager will triage).

1. **`engine/render/CLAUDE.md` — fix dead pointer, trim catalogs.** Remove
   the `engine/render/tests/render-baselines/` reference (directory does
   not exist; PR authors following the instruction will fail), resolve or
   delete the `T-09Y` placeholder, trim the `Key exposed surface`
   function-name catalog and the `Key components` per-component catalog.
   Keep the pipeline-wiring ordering block (load-bearing).

2. **`engine/math/CLAUDE.md` — collapse function-signature catalogs.**
   The middle 60 lines (`Layout helpers`, `Color`, `Physics`,
   `Quaternions`, `Random`) are essentially `ls` of the math headers.
   Compress each section to one paragraph with the actionable gotcha.
   Delete the duplicated isometric-projection equations (canonical home
   here; rule file should reference, not duplicate).

3. **`engine/prefabs/CLAUDE.md` — de-dup vs `.claude/rules/cpp-ecs.md`.**
   The "Component method rules" section and the
   "Three valid TICK function signatures" / "no function-local static"
   anti-patterns are duplicated verbatim with the corresponding rule
   files. CLAUDE-BASELINE.md explicitly names
   `engine/prefabs/CLAUDE.md` as the canonical home for the categorization,
   so the rule files should reference here. Delete the duplicates from
   the rule files OR delete the CLAUDE.md restatements — pick one
   canonical home. Also delete the layout-block inventory of
   `engine/prefabs/irreden/` subdirs.

4. **`engine/prefabs/irreden/` — prune name catalogs across all subtree
   CLAUDE.md.** `common/`, `render/`, `audio/`, `input/`, `update/`,
   `voxel/`, `video/` all open with "Key components" / "Key systems"
   sections in catalog shape. Per-entry triage: keep entries that
   document a non-obvious gotcha (auto-add lifecycle, modifier routing,
   onDestroy side effects, pool-per-canvas invariant); prune entries
   that pure-restate the name. One PR per file is too granular —
   batch the subtree in one PR.

5. **`engine/audio/CLAUDE.md` + `engine/video/CLAUDE.md` — remove
   genuinely dead pointers.** `C_AudioFile`, `C_FramebufferCapture` (at
   the `engine/video/` reference, not `engine/prefabs/irreden/video/`),
   `C_FramebufferOutputPosition`, `C_OutputResolution` appear ONLY in
   the respective CLAUDE.md files — never implemented or removed
   without doc cleanup. Either delete the references or rephrase as
   "(planned: …)" stubs. Also remove the audio-CLAUDE.md `Key components`
   catalog that duplicates `engine/prefabs/irreden/audio/CLAUDE.md`.

6. **`engine/input/CLAUDE.md` — fix `C_Hitbox2D` reference.** L62–65
   references `C_Hitbox2D` which appears ONLY in this file. The actual
   components are `C_HitboxRect` and `C_HitboxCircle` (under
   `engine/prefabs/irreden/update/components/`). The surrounding
   `onHovered`/`onUnhovered`/`onClicked` callback section may need to be
   audited together — verify the callback names and update the
   component name to match.

7. **Engine small modules — delete inline directory trees.**
   `engine/CLAUDE.md` (L20–44 layer map), `engine/common/CLAUDE.md`
   (L38–46 internal layout), `engine/utility/CLAUDE.md` (L27–33 tree),
   `engine/profile/CLAUDE.md` (L54–64 internal layout — already proven
   stale by missing `profile_report.hpp`), root `CLAUDE.md` (L71–82
   project-layout tree). All violate baseline §"What belongs in
   CLAUDE.md files" on file/directory listings. One PR for all five.

8. **`engine/system/CLAUDE.md` ↔ `.claude/rules/cpp-systems.md` de-dup.**
   The "Three valid TICK function signatures" block (L56–82) and the
   "Don't use function-local static for system state" anti-pattern
   (L210–227) are duplicated verbatim. Pick a canonical home — recommend
   `engine/system/CLAUDE.md` since it's the closest-to-code doc.
   Delete the duplicates from `.claude/rules/cpp-systems.md` and replace
   with one-line references. Also delete the L10–14 `IRSystem::`
   function-name catalog.

9. **`creations/CLAUDE.md` + `creations/demos/CLAUDE.md` — fix drift.**
   `creations/CLAUDE.md` L18 + L29–34 reference
   `creations/editors/voxel_editor/` which doesn't exist; the inlined
   `.gitignore` block at L23–34 will drift from the real `.gitignore`;
   the "Current demos" list in `creations/demos/CLAUDE.md` is missing
   `gpu_particles`, `perf_grid`, `stateless_particles`, `ui_dockspace`,
   `ui_widgets` (5 of 15 demos). Either remove the inventory entirely or
   add a "verify with `ls`" disclaimer. Delete the redundant CMake
   boilerplate walkthrough; keep the MinGW-DLL gotcha.

10. **SQT transition notes across `prefabs/` family.** Add a one-line
    "T-199 in flight: `C_Position3D` / `C_PositionGlobal3D` / `C_Rotation`
    are being phased out in favor of `C_WorldTransform` +
    `C_LocalTransform`. New code should prefer the SQT pair; legacy
    references will be migrated by T-199 consumer sweep." Apply to
    `engine/prefabs/CLAUDE.md` (under §"Component method rules"
    `(a) Pure data` example), `engine/prefabs/irreden/render/CLAUDE.md`
    (under gizmo/sprite code examples), and `engine/prefabs/irreden/common/CLAUDE.md`
    (softening the "retired in T-199 migration" phrasing to "in flight").
    `engine/script/CLAUDE.md` Lua examples are out of scope —
    they'll be touched when T-199 lands the Lua-side migration.

---

## Filed follow-up issues

| # | Issue | Scope |
|---|---|---|
| 1 | [#833](https://github.com/jakildev/IrredenEngine/issues/833) | `engine/render/CLAUDE.md` — render-baselines dead pointer, catalogs |
| 2 | [#834](https://github.com/jakildev/IrredenEngine/issues/834) | `engine/math/CLAUDE.md` — function-signature catalogs |
| 3 | [#835](https://github.com/jakildev/IrredenEngine/issues/835) | `engine/prefabs/CLAUDE.md` ↔ `.claude/rules/cpp-ecs.md` de-dup |
| 4 | [#836](https://github.com/jakildev/IrredenEngine/issues/836) | `engine/prefabs/irreden/` subtree — name-catalog pruning |
| 5 | [#837](https://github.com/jakildev/IrredenEngine/issues/837) | `engine/audio/CLAUDE.md` + `engine/video/CLAUDE.md` — dead pointers |
| 6 | [#838](https://github.com/jakildev/IrredenEngine/issues/838) | `engine/input/CLAUDE.md` — `C_Hitbox2D` rename drift |
| 7 | [#840](https://github.com/jakildev/IrredenEngine/issues/840) | Small modules — delete inline directory trees |
| 8 | [#841](https://github.com/jakildev/IrredenEngine/issues/841) | `engine/system/CLAUDE.md` ↔ `.claude/rules/cpp-systems.md` de-dup |
| 9 | [#842](https://github.com/jakildev/IrredenEngine/issues/842) | `creations/` + `creations/demos/` drift |
| 10 | [#843](https://github.com/jakildev/IrredenEngine/issues/843) | SQT transition notes across `prefabs/` family |
