---
name: simplify
description: >-
  Polishes the dirty working tree before a commit: fixes the Irreden-Engine
  smells a reviewer would flag (per-entity getComponent in ticks, hot-loop
  allocation, naming slips, duplicated helpers, dead code, debug logs,
  narrating comments, renderer leaks from creation code, drifting docs) and
  reports what needs judgment. Use before committing, after addressing
  review feedback or amending, or whenever the user says "simplify", "clean
  up", "polish", "self-review", or "review my changes"; commit-and-push
  invokes it automatically before drafting the commit message.
---

# simplify

Applies safe fixes inline and reports the rest. Module `CLAUDE.md` files
override the defaults below for their subtree — read the ones the diff
touches first.

## Flow

### 1. Read what changed

```bash
git diff --stat
git diff
git ls-files --others --exclude-standard    # new untracked files count as changes
git diff @{upstream}..HEAD                   # unpushed commits, when the diff is empty
```

All of these empty → "nothing to simplify", exit. Untracked files alone
are a working tree to polish.

### 1b. Dispatch the reuse-detection subagents

In one message, dispatch all five via the `Agent` tool; they run while
sections 2–5 proceed inline and feed section 6.

| Subagent | Tier | What it finds |
|---|---|---|
| `simplify-grep-function-names` | Haiku | New function names that duplicate existing ones in the tree. |
| `simplify-grep-utility-candidates` | Haiku | New functions that look like utilities and should live in `engine/math`, `ir_container_utils.hpp`, the renderer, etc. |
| `simplify-scan-loop-patterns` | Haiku | Triple-nested voxel/grid loops, per-entity loops that allocate, repeated `getComponent` in inner loops, linear-search in save/load paths. |
| `simplify-scan-render-leak` | Sonnet | Non-render code calling renderer primitives directly (`subImage2D`, vertex composition, GL/Metal calls); CPU-side SDF grid evaluation; math that belongs in a shader. |
| `simplify-scan-call-sequence-dup` | Sonnet | New function bodies with ≥70% call-sequence overlap to existing functions (catches structural duplicates that pure name-match misses). |

Brief each with the explicit changed-path list — the union of `git diff
--name-only HEAD` and `git ls-files --others --exclude-standard` — plus a
note that you are the parent simplify skill; the agent definitions in
`.claude/agents/` carry the rules. `git diff --name-only` alone omits
untracked files, and a subagent can only `Read` the paths it was handed.

```
Subagent: simplify-scan-loop-patterns
Prompt: "Diff scope (working-tree paths, may include new untracked
files): <paste path list>. Read each cited path directly, then scan for
the loop-pattern smells documented in your agent definition. Return the
findings list only — no preamble. Cap at 20 findings."
```

A subagent that errors or times out is skipped; the inline checks stand
on their own.

### 2. ECS smells

Check the diff against
[`.claude/rules/cpp-ecs-smells.md`](../../rules/cpp-ecs-smells.md).
Auto-fix an unconditional `getComponent` on a small component: add the
type to `createSystem<...>` and use the iteration variable. Flag for
judgment when the call is conditional, the component may be absent on
some archetype, the system signature spans files outside the diff, or the
smell is a tier-c component method, a mid-iteration structural change, a
missing `SystemName` entry, or a Lua binding.

### 2b. Mechanical checks

One file per check under [`checks/`](checks/); for each row whose trigger
matches the diff, read that file and follow it — grep shapes,
false-positive guards, allowlists, and live deviations live there. Check
numbers are stable IDs cited elsewhere; retired numbers are never reused.

| # | Check | Trigger (run when…) | Mode |
|---|---|---|---|
| 1 | [`glm::` / `std::` math outside the IRMath allowlist](checks/check-01-math-primitives.md) | any C++ change in the diff | flag |
| 2 | [function-local `static` in system tick files](checks/check-02-tick-local-static.md) | the diff touches `engine/system/**` or `system_*` files (prefab/creation system headers) | report |
| 3 | *retired* — raw stdout/`printf` is owned by §6 (reuse) and §7 (debug logs) | — | — |
| 4 | [location-reference comment narration](checks/check-04-location-narration-comments.md) | any C++ change in the diff | auto-fix |
| 5 | [non-C++ text hygiene (final newline; Lua dead locals)](checks/check-05-non-cpp-text-hygiene.md) | the diff touches `.cmake`, `.md`, `.lua`, `.txt`, or `CMakeLists.txt` files | auto-fix |
| 6 | [hand-rolled demo asset-copy blocks](checks/check-06-demo-asset-copy.md) | the diff touches `creations/demos/*/CMakeLists.txt` | report |
| 7 | [task-reference comments / motivation prose](checks/check-07-reference-comments.md) | the diff adds comments in any class `scripts/lint_comment_refs.py` scans: C/C++/ObjC++ (incl. `.c`/`.hh`/`.cxx`/`.tpp`/`.inl`/`.mm`/`.m`), shader, `.lua`, build (`.cmake`/`CMakeLists.txt`/`.bzl`/`.bazel`) or tooling (`.py`/`.sh`/`.bash`/`.zsh`/`.ps1`/extensionless `scripts/**` + `engine/tools/bin/**`) files | auto-fix |
| 8 | [unreplaced scaffold placeholder sentinels](checks/check-08-scaffold-sentinels.md) | the diff touches `creations/**` (especially a new creation) | auto-fix |
| 9 | [template functions added with no instantiation](checks/check-09-uninstantiated-templates.md) | the diff adds a `template <...>` function or member | report |
| 10 | [new fleet tool / workflow logic with no test](checks/check-10-fleet-tool-tests.md) | the diff adds an executable under `scripts/fleet/`, a function to an already-tested `scripts/**` module, or non-trivial logic in a workflow `run:` block | report |
| 11 | [mutable namespace-scope variables in headers](checks/check-11-header-globals.md) | the diff touches `.hpp`/`.h` files | report |
| 12 | [printf-style conversions in fmt log macros](checks/check-12-printf-in-log-macros.md) | the diff adds `IR_LOG_*` / `IRE_LOG_*` / `IRE_GL_LOG_*` calls | auto-fix |
| 13 | [added constant duplicating an existing definition](checks/check-13-duplicate-constants.md) | the diff adds a `constexpr` / `const` named constant | report |
| 14 | [removal of a still-used std include](checks/check-14-still-used-include.md) | the diff removes an `#include <...>` line | auto-fix |
| 15 | [retirement sweep: the old literal value](checks/check-15-sentinel-literal-sweep.md) | the diff introduces a named sentinel/constant that replaces a prior value, or migrates sites onto one | report |
| 16 | [`save_component_inventory.hpp` include order](checks/check-16-save-inventory-order.md) | the diff touches `engine/world/include/irreden/world/save_component_inventory.hpp` | auto-fix |
| 17 | [invariant guard with no firing test](checks/check-17-guard-needs-test.md) | the diff adds an `IR_ASSERT` in a non-test file, or deletes a member/flag/special-case with a stated defensive purpose | report |
| 18 | [raw `assert()` instead of the engine convention](checks/check-18-raw-assert.md) | the diff adds a raw `assert(` call | auto-fix |
| 19 | [citations resolve at base, via the right resolver](checks/check-19-citation-resolution.md) | added lines carry `docs/**.md` paths, `§<id>` section citations, or bare `#<N>` GitHub references (any changed file type) | fix or report |
| 20 | [added daemon warn with no escalate-then-quiet](checks/check-20-daemon-warn-escalation.md) | the diff adds a log/warn emission to a `scripts/fleet/**` file containing an unattended repeat loop | report |
| 21 | [`cmake --preset` snippets name their source dir](checks/check-21-preset-snippet-source-dir.md) | the diff adds a fenced block containing `cmake --preset` to any `.md` | report |

### 2c. Serialized-struct version bump

`engine/asset/CLAUDE.md` §"Automated version-bump detection", for any
`.hpp`/`.cpp` under `engine/asset/`, `engine/prefabs/irreden/voxel/`, or
`engine/world/`.

### 3. Naming

[`docs/agents/CLAUDE-BASELINE.md`](../../../docs/agents/CLAUDE-BASELINE.md)
§Naming. Fix inline: `m_` on public / trailing `_` on private (the most
common slip), missing `C_` on a component, missing shader prefixes,
anonymous namespaces in headers (nested `detail` instead), feature-named
helper namespaces (`MinimapDetail`). Flag abbreviations in new
identifiers (`vcIso` for `viewCenterIso`) as nits.

### 4. Ownership and lifetime

- `shared_ptr` where `unique_ptr` would do — fix when the lifetime is
  obviously a tree, report otherwise.
- Raw pointers are non-owning, always.
- References or pointers into ECS component storage held across ticks —
  archetype changes invalidate them; cache the entity ID and re-fetch.
- Lambdas capturing `this` or World-manager references that outlive the
  World (lua callbacks registered before teardown) — flag.

### 5. Render pipeline

For `engine/render/` and shader diffs:

- The CPU frame-data struct in `engine/render/include/irreden/render/` is
  in sync with its GLSL `layout(std140)` block: `vec3` pads to 16 bytes,
  array elements stride 16, members crossing a 16-byte boundary need
  `alignas(16)`. Every shader `binding = N` matches its C++
  `kBufferIndex_*` — the mismatch is silent.
- Canvas allocation before the canvas entity exists.
- Hand-rolled compute dispatch sizes — use `voxelDispatchGridForCount()`.
- A shader writing indirect-dispatch dims from a runtime count caps
  `numGroupsX` at `kMaxDispatchGroupsX` (1024) and spills into
  `numGroupsY` (mirror `writeDispatchDims()` in
  `c_voxel_visibility_compact.glsl`; consumers read `groupId.x +
  groupId.y * numGroupsX`). An uncapped 1-D grid drops cells past 65535
  groups with no error.
- A dispatch function that `bindRange`s/`bindBase`s a shared
  `kBufferIndex_*` slot (`engine/render/CLAUDE.md` §Gotchas) restores the
  original binding itself, not via a downstream system.
- Removing a system's `GpuStageTimingObserver` tag without adding
  `IR_PROFILE_SCOPE("<stageName>")` to its tick — the perf overlay's CPU
  row for that stage reads 0.0 forever; `IR_PROFILE_FUNCTION` feeds
  easy_profiler, not that histogram.
- A stats row in `system_perf_stats_overlay.hpp` whose minimum rendered
  width (label + each conversion's minimum field width + separators, per
  `\n`-delimited row) exceeds `kPerfStatsColumnTrixels / kGlyphStepX`
  (22 chars) — split it.
- 3D world coords mixed with iso-2D coords without
  `IRMath::pos3DtoPos2DIso` or a named helper.

For `system_*ao*`, `system_*shadow*`, `system_*flood*`, `system_*fog*`,
`system_build_light_occlusion_grid*`, or `c_compute_*shadow*` shaders,
also flag grid-build code that includes `cull_viewport_state.hpp` or calls
`visibleIsoViewport` (the light-occlusion grid covers the full voxel
pool), and flood-fill seed gathers filtered by `visibleIsoViewport`
without expanding by `C_LightSource::radius_`.

### 6. Reuse — consume the subagent results

Skip any subagent that has not returned. Act by confidence tier:

- **High — auto-apply.** `simplify-grep-function-names`: exact name match
  in the same module subtree with a compatible signature → call the
  existing function, delete the body. `simplify-grep-utility-candidates`:
  a body using only math/std/container types → move it to the cited home,
  update call sites. `simplify-scan-call-sequence-dup` ≥90 % overlap on a
  function under 30 lines → rewrite as a call to the existing one. Re-run
  section 10 after every auto-applied rewrite.
- **Medium — report.** Cross-module name matches; 70–89 % overlap on small
  functions or ≥90 % on large ones; utility candidates carrying one
  engine-specific dependency; every `simplify-scan-loop-patterns` hit
  (triple-nested voxel/grid loops in `creations/` or editors, repeated
  `getComponent` in inner loops, allocation in per-entity loops, linear
  search in save/load, CPU-side SDF grid evaluation); every
  `simplify-scan-render-leak` hit (direct backend texture writes,
  pixel-pack code, framebuffer/canvas allocation outside `engine/render/`).
  An SDF-grid loop flagged by both scanners is reported once, under
  renderer-leak.
- **Deferred — surface.** Utility candidates with no canonical home ("pick
  a home"); 50–69 % call-sequence overlap ("worth a glance").

Inline rules the subagents don't cover: a math sequence repeated in
shaders → `engine/math/` (CPU) or `ir_iso_common.glsl` (GPU); use the
helper if it exists, propose one at 3+ occurrences. Raw `std::cout` /
`printf` diagnostics → `IRE_LOG_*` (engine) / `IR_LOG_*` (game) from
`engine/profile/include/irreden/ir_profile.hpp`. Prefer an existing
helper over inline duplication even when the duplicate is shorter.

If the whole fan-out failed, do the pass inline: for each new function or
block, grep the engine + creations tree for its name and its first two
distinctive call targets.

### 7. Dead code, debug logs, comments

Remove: unused functions, includes, and unreachable branches;
commented-out code; debug logging left from troubleshooting (downgrade to
`IRE_LOG_WARN` / `IRE_LOG_ERROR` when it has rare-path value);
tautological comments; change-narration comments (`// Refactored from X`,
`// Now uses Y`) — at block scale too: a multi-line block tracing
issue-by-issue history is the same smell, cut to the durable invariant
with no backref — issue numbers never belong in comments
(`.claude/rules/comments.md`; the `lint_comment_refs.py` ratchet fails CI
on a file that gains one) — and a block repeated near-verbatim at 3+
sites is hoisted to `docs/design/<topic>.md`; location-reference
narration (`// set above`, `// see below`; Check 4); stale `TODO`/`FIXME`
on work finished this session; "old code" markers. Task-reference
comments are Check 7.

Keep: durable **why** — rationale that would still be written if the code
had always existed in its current form — and doc comments on public
surface whose name alone does not state the contract.

### 8. Style

- Early return over nested guards.
- No `try`/`catch` for control flow inside the engine.
- No abstractions for hypothetical requirements.
- No validation of impossible states; validate at system boundaries.
- The counterweight: a guard, clamp, or asymmetry whose correctness
  rationale lives in a **different file** says so at its own site;
  otherwise it reads as the deletable defensive hygiene the previous
  bullet targets, and removing or "unifying" it breaks a cross-file
  invariant with no local signal. A declaration-order or layout invariant
  lists its consumers at the declaration site — that is where the
  breaking edit is made. Before deleting any guard under the previous
  bullet, check it is not one of these.
- Domain-meaningful magic numbers (`count > 64` for a dispatch group
  size, `sleep(900)` for a cooldown) become a named `constexpr` at the
  right scope. Throwaway numbers in tests, init lists, axis vectors, and
  one-off math stay.

### 9. Doc-side checks (always run)

9a checks that markdown in the diff still describes reality; 9b checks
that the docs describing the diff's non-doc files still do. Pure formatter
diffs skip both.

#### 9a. Doc → code drift

- Stale cross-references: a cited path, label, role, task ID, PR number,
  or skill name that no longer exists or means something else.
- Retired-entity paraphrases: when the diff retires a label, flag,
  script, or body marker, grep for prose paraphrases (`fleet:stacked` →
  `stacked label|stack label`) and the docs that *delegate* to the
  changed file, not only the literal token. Code-side literals: Check 15.
- Stale restatements of a corrected claim — grep the superseded phrase
  across the module subtree (changed file plus nearest `CLAUDE.md`), also
  when the correction sits in a C++ doc comment.
- Examples using a superseded API, script, or flag.
- Claims about a named artifact the diff introduces: run the grep,
  command, or step that would refute the claim, at the PR's current head;
  anchor `path:N-M` citations on the symbol instead.
- Change-narration prose ("Updated this section to reflect…") — doc
  bodies describe the current state.
- Redundant paragraphs or bullets; headers whose body covers something
  else; contradictions within a doc.
- Point-don't-dump violations
  ([`CLAUDE-BASELINE.md`](../../../docs/agents/CLAUDE-BASELINE.md)
  §"What belongs in agent-facing docs"): file trees, symbol catalogs,
  restated baseline rules, `SKILL.md` sections paraphrasing the
  `description:`, anti-pattern entries restating flow steps, decorative
  `❌`/`✅` bullets — replace with a one-line pointer to the canonical
  home.
- A bash fence running `gh pr edit|create` / `gh issue create` with
  `--body "$var"` instead of `--body-file`, or consuming a `$var` never
  assigned in the same fence.
- Broken cross-refs: every `[text](path#anchor)` resolves; every `§Foo`
  matches a heading; named symbols exist. Bare `#N` citations and
  citations that must hold at the PR's base are Check 19.

For role and skill docs, also report (never auto-fix) cross-doc
duplication that has grown unmanageable and stale instructions that
contradict newer ones in the same doc.

#### 9b. Code → doc drift

For each non-doc file in the diff the target set is: the nearest
`CLAUDE.md`, the file's own prose (`--help`/USAGE, `argparse
description=`, header comment), the other half of a `fleet_<x>.py` /
`fleet-<x>` pair, `docs/**` references naming it, and a fleet rule's
mirrors across `.claude/rules/` ↔ `.claude/agents/` ↔ `.claude/skills/` ↔
`docs/agents/` ↔ `.claude/commands/role-*.md`. Key the sweep on the
subject, not the file; "what changed" includes a tool's coverage (a new
lane, mode, target set, or glob), not only counts. Flag, never auto-edit:

- A new system, component, prefab, shader, helper namespace, toggle,
  preset, label, role, or skill where the doc enumerates the category.
- A removed or renamed symbol, file, label, or skill the doc cites.
- A documented count or membership list that drifted.
- A new constraint future contributors will trip over undocumented, or a
  documented warning the diff made obsolete.
- Enforcing a documented rule (checker, lint, CI step, `static_assert`)
  or carrying `Closes #N` falsifies prose outside the diff: sweep with
  `fleet-rules-sweep --pattern '<enforced symbol>'` / `--pattern '#<N>'`
  and give every hit a disposition (fixed / not stale / out of scope)
  reconciled against the match count.
- A rule the diff writes into a `CLAUDE.md` or rules file, re-read
  against the rest of the same diff — the diff that writes a rule is the
  one most likely to violate it.

Skip 9b when the diff only changes function bodies, the directory has no
relevant `CLAUDE.md` (fixture, generated, vendored), or the change is a
typo/format/comment edit — except the corrective sweep above.

Report one line per target with the gap and a one-line suggestion; a
clean pass produces no output.

### 10. Format and verify

```bash
fleet-build --target format-changed
fleet-build --target <touched-target>
```

`format-changed` scopes clang-format to files changed on the branch; the
bare `format` target is whole-tree and belongs only on intentional cleanup
PRs. Keep any reformatting as part of the polish. Doc-only diffs skip the
build. A broken build means revert the simplify changes or fix the break —
never leave the tree broken.

### 11. Report

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
  build: clean
```

Drop empty sections. A clean tree lets `commit-and-push` proceed.

## What this skill does NOT do

- Run tests — `fleet-run` / `ctest` are the author's.
- Refactor across modules or redesign — report instead.
- Push. Read-only on history; edits the working tree only.
- Bundle drift in files the PR does not touch — report it or file an
  issue.
