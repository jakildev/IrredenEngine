---
name: simplify
description: >-
  Polish the dirty working tree before committing — catch Irreden-Engine
  smells that a reviewer would otherwise flag. Use whenever you're
  about to commit, after addressing review feedback, after amending a
  commit, or whenever the user says "simplify", "clean up", "polish",
  "self-review", or "review my changes". Also auto-invoked by
  commit-and-push before the commit message is drafted. The skill
  finds per-entity getComponent in tick functions, allocation in hot
  loops, ECS naming convention slips, opportunities to reuse existing
  helpers, dead code, debug logs left behind, tautological comments,
  and stale or drifting CLAUDE.md / role / skill docs — applying safe
  fixes inline and reporting anything that needs human judgment.
  Saves a review-fix-rereview round-trip.
---

# simplify

A pre-commit cleanup pass on the dirty working tree. Reads what
changed, applies the fixes that don't need judgment, and reports
the rest.

## Why this exists

The reviewer agent flags the same handful of issues over and over:
per-entity `getComponent` inside a system tick, naming-convention
slips, debug `std::cout` lines that didn't get removed, comments
that say `/// Returns x` on `int getX() { return x_; }`. Each
flagged issue costs a round-trip — reviewer comments, author
addresses, reviewer re-reviews. If the author's worktree catches
these before the PR opens, the reviewer's only finding is the
genuinely subtle stuff.

This skill is that filter. It's not a substitute for review — it's a
first pass that gets the cheap stuff out of the way so the reviewer
can focus on what matters.

## Flow

### 1. Read what changed

```bash
git diff --stat
git diff
```

If both are empty, also check unpushed commits:

```bash
git diff @{upstream}..HEAD
```

If everything is empty, report "nothing to simplify" and exit.

Group the touched files by module — `engine/render/`,
`engine/system/`, `engine/prefabs/irreden/`, `creations/`, etc. The
relevant `CLAUDE.md` files in those directories define module-specific
rules that override the defaults below; read them before touching
anything in their scope.

### 2. ECS smells (the biggest performance footgun)

Per-entity `getComponent<C_Foo>()` or `getComponentOptional<C_Foo>()`
inside a system's per-entity tick is a hash-map lookup, an archetype
scan, and another hash-map lookup — at scale it dominates the frame.
The fix is mechanical: add the component to the system's template
parameters so it iterates the dense column.

**Apply the fix when** the call is unconditional and the component is
small. Add the type to the system's `createSystem<...>` template
params (or the equivalent template signature) and replace the
`getComponent` call with the iteration variable.

**Report instead of fixing when** the call is conditional, when the
component might not exist on every entity in the archetype, or when
the system signature is shared with other tick functions you can't
see in the diff. Note `engine/system/CLAUDE.md` has the full
tick-function-signature story.

Also flag (don't auto-fix):
- `createEntity` / `removeComponent` mid-iteration without the
  deferred variant — race-prone, deferred is the right answer.
- Allocation in per-entity tick paths (`new`, `vector::push_back` on
  a hot vector, `string` concat, `map::operator[]` insertion).
- A new prefab system that isn't added to `SystemName` in
  `engine/system/include/irreden/ir_system_types.hpp` (the system
  name enum is the registration mechanism).
- `C_Position3D` read in a render-related system for visual placement
  instead of `C_PositionGlobal3D + C_PositionOffset3D` — rendered
  position is always Global + Offset. Flag; confirm the component is
  intentional.
- A component method that calls `IREntity::getComponent` /
  `setComponent` / `createEntity` / `setParent` on a *different*
  entity (tier-c violation per `engine/prefabs/CLAUDE.md`). Flag
  unless the method is on the documented exceptions list.
- `functionBeginTick` / `functionEndTick` declared with `Archetype&`
  or any component parameter — they must be `void()`.
- `endTick` body that reads `ids[0]` or indexes `ids` without first
  guarding `ids.size() == 0`.
- **Bare string C++ component references in Lua.** In `.lua` files, a
  `components = { "C_..." }` or `excludes = { "C_..." }` entry that
  spells a C++ component as a bare string (e.g. `"C_Position3D"`) instead
  of the `IRComponent.C_Name` handle. Fix: replace `"C_Name"` with
  `IRComponent.C_Name`. Only applicable when `bindLuaDrivenEcs()` is in
  use (any creation that calls `IRSystem.registerSystem`). Bare strings
  for Lua-defined components (no `C_` prefix) are still the only option —
  don't flag those.
- **Hand-written IRTime / SystemName string keys in new C++ binding code.**
  In C++ binding files (e.g. `lua_pipeline_bindings.hpp`), new `IRTime`
  or `SystemName` table entries written as `t["NAME"] = ...` without the
  `IR_BIND_TIME` / `IR_BIND_SYS` macro. The macros derive the key from
  the enum via stringization so the key stays in sync. Flag; suggest using
  `IR_BIND_TIME(NAME)` or `IR_BIND_SYS(NAME)` instead.

### 2b. Math primitives + system-state smells (mechanically detectable)

Two convention slips that are pure regex catches. Both are documented in
[`.claude/rules/cpp-math.md`](../../rules/cpp-math.md) and
[`.claude/rules/cpp-systems.md`](../../rules/cpp-systems.md) — those rules
auto-load whenever an agent opens a C++ file, but agents still slip.
This pass catches what slipped through.

**Check 1: `glm::` and `std::` math calls outside the allowlist.**

Run a grep against all in-scope C++ files (catches both new and existing
violations). The Grep tool is allowlisted; use it directly:

```
Grep tool with:
  pattern: '\b(glm::|std::(sin|cos|tan|sqrt|abs|min|max|clamp|floor|ceil|round|pow|atan2|asin|acos))\b'
  glob:    '**/*.{hpp,cpp,h,cc}'
  output_mode: 'files_with_matches'
```

For each hit, manually exclude paths in the allowlist (do not flag):

- `engine/math/**` — IRMath itself wraps these names internally.
- `engine/render/include/irreden/render/backend/**` — backend interop
  may pass raw glm types into graphics APIs.
- `*.glsl` / `*.metal` files (the grep glob excludes these, but
  double-check).

For everything else, flag with the IRMath equivalent. Common substitutions:

| Found | Suggest |
|-------|---------|
| `glm::vec3` / `glm::ivec3` / `glm::mat4` | `IRMath::vec3` / `IRMath::ivec3` / `IRMath::mat4` |
| `glm::min` / `glm::max` / `glm::clamp` | `IRMath::min` / `IRMath::max` / `IRMath::clamp` |
| `glm::length` / `glm::normalize` / `glm::dot` | `IRMath::length` / `IRMath::normalize` / `IRMath::dot` |
| `glm::sin` / `glm::cos` / `glm::sqrt` | `IRMath::sin` / `IRMath::cos` / `IRMath::sqrt` |
| `glm::pi<float>()` / `glm::half_pi<float>()` / `glm::two_pi<float>()` | `IRMath::kPi` / `IRMath::kHalfPi` / `IRMath::kTwoPi` |
| `std::min` / `std::max` / `std::clamp` | `IRMath::min` / `IRMath::max` / `IRMath::clamp` |
| `std::sin` / `std::cos` / `std::sqrt` / `std::abs` | `IRMath::sin` / `IRMath::cos` / `IRMath::sqrt` / `IRMath::abs` |

If the IRMath wrapper does not exist yet, **don't auto-substitute** —
flag with: "IRMath::<name> does not exist; add the wrapper to
`engine/math/` first, then call it." (The `IRMath::kPi` / `kHalfPi` /
`kTwoPi` constants in particular may not be merged yet — verify before
suggesting.)

**Check 2: function-local `static` in system tick files.**

Use Grep to scan system files for `static` declarations that are NOT
`static constexpr` or `static const`, then cross-reference the hits
against `git diff` added lines to confirm the match is newly introduced.

```
Grep tool with:
  pattern: '\bstatic\b(?!\s+constexpr)(?!\s+const\b)'
  glob:    '{engine/prefabs/irreden/**/system_*.{hpp,cpp},engine/system/**/*.{hpp,cpp},creations/**/system_*.{hpp,cpp}}'
  output_mode: 'content'
  -n: true
```

Filter the Grep results to lines that also appear as `+` lines in
`git diff --unified=0` for the same file — those are the newly added
violations. Lines that exist on both sides (pre-existing code) belong
to the live-deviation list and should only be noted, not re-flagged.

For each hit, suggest the canonical `SystemParams` migration pattern:

> Replace `static <T> name;` with a `SystemParams` field. Capture the
> params pointer once at `create()` time and pass into the lambdas by
> value. See [`.claude/rules/cpp-systems.md`](../../rules/cpp-systems.md)
> "Canonical SystemParams pattern" or
> [`engine/system/CLAUDE.md`](../../../engine/system/CLAUDE.md) for the
> canonical example.

Live deviations already on the list (don't re-flag, but note in the
report if touched):

- `engine/prefabs/irreden/render/systems/system_entity_canvas_to_framebuffer.hpp:41-43`
- `engine/prefabs/irreden/audio/systems/system_rhythmic_launch.hpp:29`
- `engine/prefabs/irreden/update/systems/system_gravity.hpp:17`
- `engine/prefabs/irreden/update/systems/system_animation_color.hpp:25-26`

These are tracked in `.fleet/status/system-static-deviations.md` (or
will be once it's introduced). Don't add new violations; do migrate
when touching one of the deviation files for other reasons.

**Check 3: `std::cout` / `printf` instead of the engine logger.**

Already covered by section 6 (Reuse opportunities) — left here only as
a cross-reference. The engine has `IRE_LOG_*` and `IR_LOG_*` macros;
raw stdout/printf in non-debug code is a cleanup target.

### 2c. Serialized-struct version-bump check

When a struct annotated `// IRAsset: serialized` gains, loses, or renames a
field, the matching `kSaveVersion` constant must be bumped in the same diff
and a migration entry must exist (or be flagged as a `// TODO: migration`
comment) in the format's reader. This is Save Format Extensibility Rule #3
(`engine/asset/CLAUDE.md`).

Run this check whenever any `.hpp` or `.cpp` file under `engine/asset/`,
`engine/prefabs/irreden/voxel/`, or `engine/world/` is in the diff.

**Detection:**

1. Scan the diff's `+` lines for struct fields (member variable declarations)
   inside a struct body that is preceded — anywhere in the same file — by the
   comment `// IRAsset: serialized`. The struct name is on the line with
   `struct <Name>`.

2. For each struct type whose field layout changed (added/removed/renamed
   field on a `+` or `-` line), check whether the diff also contains a
   corresponding `kSaveVersion` change on a `+` line in the same file (or a
   sibling sidecar file for the format):

   ```
   static constexpr uint16_t kSaveVersion = N;
   ```

3. If the field layout changed but no `kSaveVersion` bump appears in the
   diff, emit a finding — **do not auto-fix**, this needs human judgment:

   ```
   reported 1 finding for review:
     - <path>:<line> — <StructName> is annotated // IRAsset: serialized and
       its field layout changed, but kSaveVersion was not bumped.
       Add `static constexpr uint16_t kSaveVersion = N+1;` and a migration
       entry in the format's reader for saves written at the old version.
   ```

**False-positive guard — do NOT flag:**
- Changes to method bodies, constructors, or `static` helper functions within
  the struct (these do not affect binary layout).
- A struct whose `// IRAsset: serialized` annotation was itself added in the
  same diff — version 1 never needs a migration.
- Changes to the `kSaveVersion` line itself.
- Changes that only touch comments or whitespace inside the struct.

### 3. Naming convention slips

These are the rules from [`docs/agents/CLAUDE-BASELINE.md`](../../../docs/agents/CLAUDE-BASELINE.md):

| Context           | Convention            |
|-------------------|-----------------------|
| Private members   | `m_` prefix           |
| Public members    | trailing `_`          |
| Components        | `C_` prefix           |
| Compute shaders   | `c_` prefix           |
| Vertex shaders    | `v_` prefix           |
| Fragment shaders  | `f_` prefix           |
| Geometry shaders  | `g_` prefix           |

Backwards usage (`m_` on public, trailing `_` on private) is the
single most common slip — fix it inline. Same for missing `C_` on a
new component class, missing shader prefixes, anonymous namespaces in
headers (use a nested `detail` namespace instead), or feature-named
helper namespaces (`MinimapDetail` instead of plain `detail`).

Abbreviations in new identifiers (`vcIso` instead of `viewCenterIso`)
are worth flagging as a nit unless context makes them unambiguous.

### 4. Ownership and lifetime

- `shared_ptr` where `unique_ptr` would do — fix when the lifetime is
  obviously a tree, report otherwise.
- Raw owning pointers — raw pointer = non-owning, always.
- Storing references or pointers to ECS component storage across
  ticks — archetype changes invalidate addresses. The fix is to
  cache the entity ID and re-fetch.
- Lambdas that capture `this` or World-manager references and outlive
  the World (e.g. lua callbacks registered before World teardown) —
  flag for human review.

### 5. Render pipeline

For files in `engine/render/` or shaders, check that the CPU
frame-data struct in `engine/render/include/irreden/render/` is in
sync with its GLSL `layout(std140)` counterpart — an out-of-sync pair
silently corrupts uniform blocks. Cross-reference the two if either
side changed. When checking: `vec3` members pad to 16 bytes, array
elements stride to 16 bytes, members crossing a 16-byte boundary need
`alignas(16)`. Also verify that every `binding = N` in the shader
matches the C++ `kBufferIndex_*` constant — a bind-point mismatch is
silent.

Also check:
- Canvas allocation before the canvas entity exists (race in init).
- Hand-rolled compute dispatch sizes — should use
  `voxelDispatchGridForCount()` rather than computing `(n+63)/64`
  manually.
- 3D-world-coord values being mixed with iso-2D-coord values without
  going through `IRMath::pos3DtoPos2DIso` or a named helper. The two
  spaces are not interchangeable.

If the diff touches `system_*ao*`, `system_*shadow*`, `system_*flood*`,
`system_*fog*`, `system_build_light_occlusion_grid*`, or any
`c_compute_*shadow*.glsl` / `.metal`, also flag:
- Grid-build code that includes `cull_viewport_state.hpp` or calls
  `visibleIsoViewport` — the light-occlusion grid must cover the full
  voxel pool, not the render-culled subset.
- Flood-fill seed gather filtered by `visibleIsoViewport` without
  expanding by `C_LightSource::radius_` — off-screen sources must
  still seed on-screen tiles.

### 6. Reuse opportunities (the highest-leverage check)

For every new function or block of logic, search for similar code
that already exists. The signal is strong enough to justify a
`Grep` round even if you think the code is novel:

- Same name → likely a duplicate
- Same call sequence (3+ identical lines, possibly with different
  variable names) → extract a shared helper
- Same math sequence in shaders → check `engine/math/` (CPU) or
  shader includes like `ir_iso_common.glsl` (GPU). If the helper
  exists, use it; if it doesn't but the sequence appears 3+ times,
  propose extracting one.
- Raw `std::cout` / `printf` for diagnostic output → use the
  `IRE_LOG_*` (engine) or `IR_LOG_*` (game) macros from
  `engine/profile/include/irreden/ir_profile.hpp`. They route to the
  right sink and compile to no-ops in release.

Prefer existing helpers over inline duplication, even if the
duplication is shorter.

### 7. Dead code, debug logs, extraneous comments

Remove:
- Unused functions, unused includes, unreachable branches.
- Commented-out code blocks (even ones from this session).
- Debug-level logging added during development that isn't part of the
  task spec — `std::cout << "DEBUG: ..."`, `printf("here\n")`,
  `IRE_LOG_DEBUG("step 3")` left over from troubleshooting. If the
  logging has rare-path or error-context value, downgrade to the
  right severity (`IRE_LOG_WARN` / `IRE_LOG_ERROR`) instead of
  removing.
- Tautological comments where the code says exactly what the comment
  says — `/// Returns the value` on `int getValue() { return value_; }`.
  Same for `// Increment counter` on `++counter_;`.
- Change-narration comments that describe what the diff modified
  rather than what the code does — `// Refactored from std::vector
  to std::array`, `// Now uses the deferred variant`, `// Updated
  for the new API`, `// Removed old approach (was X, now Y)`.
  These narrate the diff (which git already shows in the commit
  history) and age into noise once the change is the new normal.
  Delete; let the commit message carry the change story.
- Stale `// TODO`/`// FIXME` markers on code you actually finished
  this session.
- "Old code" markers next to deleted lines.

Keep:
- Comments that explain non-obvious **why** — rationale, gotchas,
  cross-references to docs/papers/prior-art decisions.
- Doc comments on public surface where the function name alone
  doesn't capture the contract (preconditions, side effects, ranges).

### 8. Style

The engine's style preferences are simple and worth applying inline:

- Early return over nested logic — refactor when nesting is 2+ levels
  and the condition is a guard rather than a real branch.
- No `try`/`catch` for control flow at internal boundaries; the
  engine doesn't use exceptions internally.
- Don't add abstractions for hypothetical future requirements.
- Don't validate scenarios that can't happen (defensive checks
  against impossible states); only validate at system boundaries.
- Magic numbers that carry domain meaning — `if (count > 64)` where
  64 is a GPU dispatch group size, `sleep(900)` where 900 is the
  usage-limit cooldown, `if (depth > 4)` where 4 is the max
  recursion. Extract to a named `constexpr` (or `const` in code that
  can't be `constexpr`) at the appropriate scope (function-local,
  file-local, or module-level). Throwaway numbers in tests, init
  lists, axis vectors (`vec3(1.0f, 0.0f, 0.0f)`), or one-off math
  are fine — only flag numbers where a name would clarify intent.

### 9. Doc-side checks (always run)

Doc upkeep is part of the reviewer-facing bar, in both directions:

- **9a — Doc → code drift.** When the diff includes markdown,
  check the doc still describes reality (existing API examples,
  cited file paths, internal consistency).
- **9b — Code → doc drift.** When the diff includes non-doc files,
  check whether the nearest `CLAUDE.md` (or relevant role / skill
  doc) should be updated to reflect a new or removed pattern.

Run whichever sub-checks apply. Pure formatter-only diffs can skip
both.

#### 9a. Doc → code drift (when the diff includes markdown)

Markdown sources to check: `.md` files anywhere, `docs/**`, role
docs under `.claude/commands/`, skill docs under `.claude/skills/`,
top-level `CLAUDE.md` and module-level `CLAUDE.md` files.

- **Stale cross-references.** A file path, label name, role name,
  task ID, PR number, or skill name cited in the prose now refers
  to something that no longer exists or means something different.
  Common triggers: a renamed file, a removed label, a role that
  got merged into another, a TASKS.md task ID that already shipped.
  Grep the cited identifiers against the current tree and fix what
  drifted.
- **Examples that drifted from the current API.** A doc shows a
  code snippet using `IRRender::makeCanvas()` but the API is now
  `IRRender::createCanvas()`. Same for shell snippets that use
  removed scripts or outdated flags.
- **Change-narration prose** (markdown analog of the code rule):
  paragraphs that describe what *was changed* in the current PR
  rather than what the doc covers — "Updated this section to
  reflect the new flow", "Removed the old explanation of X". The
  commit message is the place for change history; doc bodies
  describe the current state.
- **Redundant prose.** A paragraph that re-says the previous
  paragraph in different words. Pick the clearer one, drop the
  other. Same for two bullet items that say the same thing with
  different framing.
- **Section drift.** A section header promises one thing but the
  body covers something else (heading "Common patterns" but body
  is a single example, or heading "Examples" with no examples).
  Either rename the heading or refocus the body.
- **Contradictions within a doc.** Step 3 says X, step 7 (added
  later in a different change) says NOT X. Reconcile or report.

For role docs and skill docs specifically, also report (don't
auto-fix — these need human judgment on scope):

- Cross-doc duplication that's grown unmanageable. The earlier
  fleet audit flagged 3-7× duplication of "Common patterns" /
  "single-command Bash" blocks across role docs. Don't refactor
  in simplify (out of scope per "doesn't refactor across modules"
  below), but flag when you see it accumulating.
- Stale instructions that contradict newer ones in the same
  doc — same smell as the main "Contradictions within a doc"
  bullet, but in role/skill docs scope judgment belongs with the
  human. Report; reconciling is outside simplify's scope here.

#### 9b. Code → doc drift (when the diff includes non-doc files)

For each non-doc file in the diff, walk up the directory tree to
locate the nearest `CLAUDE.md`. De-dupe so each `CLAUDE.md` is
considered once. For each one, ask whether the current change
introduces something the doc would reasonably want to mention, or
invalidates something it currently asserts. The intent is to keep
each module's `CLAUDE.md` representative of the current state — not
to grow them with every change.

Flag (don't auto-edit — the "doc-worthy?" call belongs to the
author):

- **New pattern, file, or convention.** The diff adds a system,
  component, prefab, shader, helper namespace, debug toggle, build
  preset, label, role, skill, or any other piece of vocabulary the
  doc establishes. If the doc enumerates the category (e.g.
  `engine/render/CLAUDE.md` describes the pipeline stages, or a
  module `CLAUDE.md` lists "common patterns"), the new entry
  belongs in the list — or the list needs to stop claiming to be
  exhaustive.
- **Removed or renamed thing the doc cites.** The diff deletes or
  renames a symbol, file, helper, label, or skill that the doc
  body references by name. Grep the doc for the old name; if it
  appears, the doc lies now.
- **Documented counts or lists drifted.** The doc says "we have N
  X" or enumerates by name; the diff changed the count or the
  membership. Numbered claims rot the fastest.
- **A new convention the doc should warn about.** The diff adds a
  rule or constraint that future contributors will trip over
  without docs (e.g., "this struct must stay 16-byte aligned",
  "this enum is the registration mechanism", "this header is
  generated"). If a reviewer would reasonably ask "where is this
  documented?", surface the gap.
- **A convention the doc warned about that no longer applies.**
  The diff removes the constraint; the warning in the doc is now
  noise.

Skip 9b when:

- The diff only changes function bodies — no new symbol, no
  removed symbol, no new file, no new build target.
- The touched directory has no relevant `CLAUDE.md` upstream
  (test fixture, generated artifact, third-party vendor tree).
- The change is purely a typo, formatting, or comment edit.

Report format — one line per `CLAUDE.md` that may need attention,
with the specific gap and a one-line suggestion. Don't speculate
about wording; let the author decide whether and how to update.
Example:

```
  reported 1 doc-drift finding:
    - engine/render/CLAUDE.md — pipeline-stages list doesn't
      mention the new SSAO compute stage added in
      engine/prefabs/irreden/render/systems/system_ssao.hpp.
      Worth a one-line addition under "Pipeline stages"?
```

A clean 9b pass is a finding of "no doc drift" and produces no
output — silence is success.

### 10. Format and verify

After applying fixes, run the formatter and rebuild (code diffs
only; doc-only diffs can skip the build):

```bash
fleet-build --target format-changed
fleet-build --target <touched-target>
```

`format-changed` scopes clang-format to files changed on the
current branch (committed vs upstream + working tree). The bare
`format` target is whole-tree and will pull every drift in the
repo into your PR — use it only on intentional cleanup PRs, never
mid-iteration. If `format-changed` rewrote anything, those changes
are part of the polish — keep them. If the build broke, **revert
your simplify changes** (or fix the break before continuing) —
never push a simplify pass that broke the build.

### 11. Report

Print a compact summary so the author knows what changed and what
needs their attention:

```
simplify: <N> file(s), <M> hunk(s)
  applied <X> auto-fix(es):
    - <path:line> — <one-line description>
  reported <Y> finding(s) for review:
    - <path:line> — <issue> — <suggested fix>
```

Empty sections — drop them rather than writing "None".

If everything was either fixed in place or reverted, report a clean
working tree and let `commit-and-push` proceed.

## What this skill does NOT do

- **Doesn't run tests.** The author runs tests separately via
  `fleet-run` or `ctest`.
- **Doesn't refactor across modules.** Out of scope for a pre-commit
  pass. If a fix would touch unrelated files, report instead of
  applying.
- **Doesn't redesign.** If the code is structurally wrong, surface it
  and let the author decide. Don't silently rewrite a system.
- **Doesn't push.** Read-only on history; only edits the working tree.
- **Doesn't bundle unrelated cleanup.** Drift in files the current PR
  doesn't touch — report it (or file an issue), don't sneak it into
  the dirty diff.

## Example

User says "simplify before I commit". The diff touches a new render
system and a creation demo.

```
simplify: 4 files, 11 hunks
  applied 3 auto-fixes:
    - engine/prefabs/irreden/render/systems/IRSGlowPulse.hpp:34
      moved getComponent<C_Color> out of tick, added C_Color to
      system template
    - engine/render/src/RenderManager.cpp:128 — removed
      `std::cout << "made canvas " << id` debug log
    - engine/render/include/irreden/render/IRCanvas.hpp:18 —
      removed tautological `/// Returns the canvas ID` doc comment
  reported 1 finding for review:
    - creations/demos/IRDemoFoo/src/main.cpp:55 — `shared_ptr<Foo>`
      where `unique_ptr` would do, but the demo passes the pointer
      to a lua callback — may be intentional sharing. Confirm
      before changing.
  build: clean
```

The author commits via `commit-and-push` knowing the diff is
already polished. If the reported finding matters, the author
addresses it; otherwise, ship.
