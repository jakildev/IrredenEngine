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
  helpers, dead code, debug logs left behind, and tautological
  comments — applying safe fixes inline and reporting anything that
  needs human judgment. Saves a review-fix-rereview round-trip.
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

### 3. Naming convention slips

These are the rules from the top-level `CLAUDE.md`:

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
side changed.

Also check:
- Canvas allocation before the canvas entity exists (race in init).
- Hand-rolled compute dispatch sizes — should use
  `voxelDispatchGridForCount()` rather than computing `(n+63)/64`
  manually.
- 3D-world-coord values being mixed with iso-2D-coord values without
  going through `IRMath::pos3DtoPos2DIso` or a named helper. The two
  spaces are not interchangeable.

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

### 9. Format and verify

After applying fixes, run the formatter and rebuild:

```bash
fleet-build --target format
fleet-build --target <touched-target>
```

If `format` rewrote anything, those changes are part of the polish —
keep them. If the build broke, **revert your simplify changes** (or
fix the break before continuing) — never push a simplify pass that
broke the build.

### 10. Report

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
