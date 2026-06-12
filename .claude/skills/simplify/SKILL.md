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
  triple-nested loops over voxel volumes, renderer leaks from
  creation code, CPU-side SDF grid evaluation, linear-search in
  save/load hot paths, and stale or drifting CLAUDE.md / role / skill
  docs. Dispatches a parallel reuse-detection subagent fan-out so the
  reuse pass runs concurrently with the main checks. Applies safe
  fixes inline and reports anything that needs human judgment. Saves
  a review-fix-rereview round-trip.
---

# simplify

## Flow

### 1. Read what changed

```bash
git diff --stat
git diff
git ls-files --others --exclude-standard    # new, untracked files (the common miss)
```

If `git diff` is empty, also check unpushed commits:

```bash
git diff @{upstream}..HEAD
```

If all of the above are empty — no working-tree edits, no untracked
files, no unpushed commits — report "nothing to simplify" and exit.
Untracked files count as changes: a new-file-only diff still has a
working tree to polish, and the reuse fan-out in 1b exists precisely to
scan it. Never exit on an empty `git diff` alone when `git ls-files
--others --exclude-standard` lists new files.

Group the touched files by module — `engine/render/`,
`engine/system/`, `engine/prefabs/irreden/`, `creations/`, etc. The
relevant `CLAUDE.md` files in those directories define module-specific
rules that override the defaults below; read them before touching
anything in their scope.

### 1b. Dispatch reuse-detection subagents (async)

Before walking sections 2–5 inline, fan out the reuse-detection pass
to subagents that run in parallel. This is the highest-leverage check
the skill performs — recent editor PRs (#933, #976, #991, #993) all
shipped with smells that an explicit reuse pass would have caught:
triple-nested voxel loops, renderer leaks from creation code, CPU-side
SDF grid evaluation, and O(N²) metadata linear scans in save/load.

In a single message, dispatch **all five** subagents below via the
`Agent` tool. They run concurrently with the inline checks in
sections 2–5; their findings feed section 6.

| Subagent | Tier | What it finds |
|---|---|---|
| `simplify-grep-function-names` | Haiku | New function names that duplicate existing ones in the tree. |
| `simplify-grep-utility-candidates` | Haiku | New functions that look like utilities and should live in `engine/math`, `ir_container_utils.hpp`, the renderer, etc. |
| `simplify-scan-loop-patterns` | Haiku | Triple-nested voxel/grid loops, per-entity loops that allocate, repeated `getComponent` in inner loops, linear-search in save/load paths. |
| `simplify-scan-render-leak` | Sonnet | Non-render code calling renderer primitives directly (`subImage2D`, vertex composition, GL/Metal calls); CPU-side SDF grid evaluation; math that belongs in a shader. |
| `simplify-scan-call-sequence-dup` | Sonnet | New function bodies with ≥70% call-sequence overlap to existing functions (catches structural duplicates that pure name-match misses). |

Each subagent returns a tight findings list with `high` / `medium` /
`deferred` confidence. They're read-only — the parent (this skill)
decides what to auto-apply.

**Briefing each subagent:** hand each one the *explicit changed-path
list*, not bare `git diff --name-only` — that form omits brand-new
untracked files and only sees unstaged edits, so new files never reach
the subagent and it silently returns zero findings on the exact diff it
exists to scan. Build the list from both modified tracked files and new
untracked files, then union the two:

```bash
git diff --name-only HEAD                    # modified (staged + unstaged) vs HEAD
git ls-files --others --exclude-standard     # new, untracked, non-ignored files
```

Pass that unioned path list to every subagent, plus a short note that
you're the parent simplify skill. The subagent definitions in
`.claude/agents/` carry the rule set; you don't need to re-explain it.
The agent briefings `Read` each cited path directly (so a new file with
no committed state is still scanned), but they can only do that for
paths you actually hand them — so the list above MUST include the
untracked files. Example briefing:

```
Subagent: simplify-scan-loop-patterns
Prompt: "Diff scope (working-tree paths, may include new untracked
files): <paste path list>. Read each cited path directly, then scan for
the loop-pattern smells documented in your agent definition. Return the
findings list only — no preamble. Cap at 20 findings."
```

If a subagent times out or errors, skip its results and continue —
the inline checks below are still authoritative for sections 2–5;
the subagent fan-out is additive coverage, not a gating step.

While the subagents run, proceed with sections 2–5 inline. Collect
their findings when each returns and consume them in section 6.

### 2. ECS smells

Check the diff against every item in [`.claude/rules/cpp-ecs-smells.md`](../../rules/cpp-ecs-smells.md) — that file is the canonical diagnostic checklist.

**Auto-fix** when a `getComponent` call is unconditional and the component is small: add the type to `createSystem<...>` template params and replace the call with the iteration variable.

**Flag for human judgment** when: the call is conditional, the component might not exist on every archetype entity, the system signature spans files outside the diff, or the smell is a tier-c component method, a structural-change mid-iteration, a missing `SystemName` entry, or a Lua binding issue.

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

For everything else, flag with the IRMath equivalent. See [`.claude/rules/cpp-math.md`](../../rules/cpp-math.md) for the full substitution table.

If the IRMath wrapper does not exist yet, **don't auto-substitute** —
flag with: "IRMath::<name> does not exist; add the wrapper to
`engine/math/` first, then call it."

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

See `engine/asset/CLAUDE.md` §"Automated version-bump detection" for the full
detection policy, false-positive guards, and the detection extension for
unannotated serialized structs. Trigger scope: any `.hpp` or `.cpp` under
`engine/asset/`, `engine/prefabs/irreden/voxel/`, or `engine/world/`.

### 3. Naming convention slips

Follow the naming table in [`docs/agents/CLAUDE-BASELINE.md`](../../../docs/agents/CLAUDE-BASELINE.md) §Naming.
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

### 6. Reuse opportunities — consume subagent results

By the time sections 2–5 finish (typically 30–60 s of inline work),
the five reuse-detection subagents dispatched in section 1b should have
returned; if a subagent hasn't completed when section 6 starts, skip
its results — they are not blocking. Collect the findings and act on
them by confidence tier:

**High confidence — auto-apply mechanical rewrites.**

- `simplify-grep-function-names` returned `high`: exact name match in
  the same module subtree with a compatible signature. Replace the
  new definition with a call to the existing function; remove the
  redundant body.
- `simplify-grep-utility-candidates` returned `high`: the function
  body uses only math/std/container types and would slot directly
  into the cited canonical home with no engine-specific dependencies.
  Move the definition; update the call sites.
- `simplify-scan-call-sequence-dup` returned `high` (≥90 % overlap on
  a function <30 lines): rewrite the new function as a call to the
  existing one.

For every auto-applied rewrite, re-run the build check in section 10
— mechanical rewrites that touch headers or change call sites can
still break compilation.

**Medium confidence — report to the author for review.**

- Cross-module name matches, 70–89 % structural overlap on small
  functions, ≥90 % overlap on a larger function (>30 lines), or
  utility candidates that pull one engine-specific dependency along.
- Loop-pattern hits (`simplify-scan-loop-patterns`): triple-nested
  voxel/grid loops in `creations/` or editor code, quadruple-nested
  pixel-pack loops, repeated `getComponent` in inner loops,
  allocation in per-entity loops, linear-search in save/load paths,
  CPU-side SDF grid evaluation. Each has a canonical fix the subagent
  cites; surface to the author and let them apply.
- Renderer-leak hits (`simplify-scan-render-leak`): direct backend
  texture writes from non-render code (`subImage2D`,
  `glTextureSubImage2D`, `MTLTexture` calls, etc.), hand-rolled
  pixel-pack code outside `engine/render/`, direct framebuffer or
  canvas allocation outside the renderer.

Deduplication: if `simplify-scan-loop-patterns` and
`simplify-scan-render-leak` both flag the same SDF-grid loop (loop-
patterns pattern 6, render-leak pattern 3), report it once under the
renderer-leak bucket (it's an architectural smell, not just a perf one).

The author decides whether to address now or in a follow-up.

**Deferred — surface as "pick a home" or "worth a glance".**

- Utility candidates that don't match any existing canonical home —
  the author picks IRMath, ir_container_utils, a new module, or
  leaves in place.
- Call-sequence overlap 50–69 % — included so the author can confirm
  it's not the same function written twice.

Beyond the subagent findings, the older inline rules still apply for
patterns the subagents don't cover:

- Same math sequence in shaders → check `engine/math/` (CPU) or
  shader includes like `ir_iso_common.glsl` (GPU). If the helper
  exists, use it; if it doesn't but the sequence appears 3+ times,
  propose extracting one.
- Raw `std::cout` / `printf` for diagnostic output → use the
  `IRE_LOG_*` (engine) or `IR_LOG_*` (game) macros from
  `engine/profile/include/irreden/ir_profile.hpp`. They route to the
  right sink and compile to no-ops in release.

**If the subagent fan-out failed entirely** (all five timed out or
errored): fall back to the prior-art prose pass — for every new
function or block of logic, grep the engine + creations tree for the
name and the first two distinctive call targets, and surface what
you find. The subagents are a speedup, not a correctness gate; the
reuse pass still happens, just inline and slower.

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
  got merged into another, a GitHub issue number that already shipped.
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
- **Point-don't-dump violations.** Per
  [`docs/agents/CLAUDE-BASELINE.md`](../../../docs/agents/CLAUDE-BASELINE.md)
  §"What belongs in agent-facing docs", agent-facing docs (`CLAUDE.md`,
  `SKILL.md`, role files) restate canonical content far too often. Flag
  any of the following for replacement with a one-line pointer to the
  canonical home (see the Canonical-home map in `CLAUDE-BASELINE.md`):
  - File/directory tree listings, layout blocks, "Key components" /
    "Key systems" sections, type/class/function name catalogs,
    function-signature catalogs — agents can `Glob` / `Grep`.
  - Restated baseline rules — ECS footgun, naming table, IRMath
    substitution, Bash rules, cross-repo isolation, Hard rules,
    build commands, fleet workflow, feedback-handling, reviewer
    protocols.
  - In `SKILL.md`: `## When to invoke` / `## Why this exists` bodies
    paraphrasing the YAML `description:`.
  - In `SKILL.md`: `## Anti-patterns` entries that restate flow-step
    requirements (keep entries that capture non-obvious things to
    avoid).
  - Decorative emoji bullets (`❌`, `✅`) — codebase convention is bare
    list bullets.
- **Broken cross-refs.** Every `[text](path)` / `[text](path#anchor)`
  link in the diff resolves to an existing file / heading. Section
  references cited as `§Foo` match an actual `## Foo` heading. Named
  symbols (type, function, label, task ID) still exist in the tree.
  Use the Grep tool to verify, then fix or report.

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
needs their attention. The reuse findings (from the section 1b
subagent dispatch) are reported as a nested block so they're easy to
scan separately from the main inline-check findings:

```
simplify: <N> file(s), <M> hunk(s)
  applied <X> auto-fix(es):
    - <path:line> — <one-line description>
  reuse findings (from subagent dispatch):
    applied <A> high-confidence rewrite(s):
      - <path:line> — <description> — replaced with <existing>
    reported <B> medium-confidence finding(s):
      - <path:line> — <smell> — <suggested fix>
    deferred <C> finding(s):
      - <path:line> — <observation> — <decision the author needs to make>
  reported <Y> finding(s) for review:
    - <path:line> — <issue> — <suggested fix>
```

Empty sections — drop them rather than writing "None". If the
subagent fan-out returned nothing actionable, omit the entire `reuse
findings` block.

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
system, a creation demo, and an editor file.

```
simplify: 5 files, 14 hunks
  applied 3 auto-fixes:
    - engine/prefabs/irreden/render/systems/IRSGlowPulse.hpp:34
      moved getComponent<C_Color> out of tick, added C_Color to
      system template
    - engine/render/src/RenderManager.cpp:128 — removed
      `std::cout << "made canvas " << id` debug log
    - engine/render/include/irreden/render/IRCanvas.hpp:18 —
      removed tautological `/// Returns the canvas ID` doc comment
  reuse findings (from subagent dispatch):
    applied 1 high-confidence rewrite:
      - creations/demos/IRDemoFoo/src/main.cpp:212 — `mulMat4Vec3`
        duplicated engine/math/include/irreden/math/ir_math.hpp:344
        `IRMath::transformPoint`; replaced with the existing call
    reported 2 medium-confidence findings:
      - creations/editors/IRVoxelEditor/src/main.cpp:1408 — triple-
        nested loop over voxel grid; replace with
        `IRMath::forEachCell3D` (engine/math/include/irreden/math/
        ir_math.hpp:512)
      - creations/editors/IRVoxelEditor/src/main.cpp:1602 —
        `subImage2D` call from creation code; extract pack-and-
        upload into a renderer helper under
        engine/render/include/irreden/render/ (see
        mask_grid_painter.hpp for the canonical pattern)
    deferred 1 finding:
      - creations/demos/IRDemoFoo/src/main.cpp:88 — `clampWrap`
        looks like a utility but mixes with demo-specific state;
        pick a home: extract to IRMath or leave in place
  reported 1 finding for review:
    - creations/demos/IRDemoFoo/src/main.cpp:55 — `shared_ptr<Foo>`
      where `unique_ptr` would do, but the demo passes the pointer
      to a lua callback — may be intentional sharing. Confirm
      before changing.
  build: clean
```

The author commits via `commit-and-push` knowing the diff is
already polished. If the reported findings matter, the author
addresses them; otherwise, ship.
