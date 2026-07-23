---
name: review-pr
description: >-
  Review an open GitHub PR on the Irreden Engine repo and post a structured
  review. Use when the user says "review PR <N>", "review #<N>", "review the
  last PR", "check my PR", "review any new PRs", "review the PR queue", "check
  for new PRs to review", "review the open PRs", or otherwise asks for a code
  review — and also invoke automatically from a persistent reviewer-loop session
  when `gh pr list` surfaces a PR that has not yet been reviewed by this fleet.
  Designed to be invoked by a dedicated reviewer agent (dispatched into a pool
  worktree) whose job
  is to look at another agent's work with a fresh context. Writes a structured
  review covering ownership, ECS invariants, allocation hot paths, naming,
  tests, and project-specific smells, then posts it as a PR comment.
---

# review-pr (Irreden Engine)

**The flow lives in [`docs/agents/skills/review-pr.md`](../../../docs/agents/skills/review-pr.md).**
Read it first, then apply the engine deltas below — most importantly the
**engine review checklist** (the shared flow's step 4) and the exact
verdict-label swap commands (step 5b). See
[`docs/design/skill-sharing.md`](../../../docs/design/skill-sharing.md) for why.

## Deltas (Irreden Engine)

| Delta key | Engine value |
|---|---|
| **repo** | `jakildev/IrredenEngine` (inline comments: `gh api repos/jakildev/IrredenEngine/pulls/<N>/comments`) |
| **claim tool** | `fleet-claim` (bail release: `fleet-claim review-release <N> <worktree>`) |
| **default branch** | `master` |
| **review checklist** | the engine checklist below |
| **acceptance grader** | [`review-acceptance`](../../agents/review-acceptance.md) — dispatch per shared-flow step 4b; verdict mapping below |
| **smoke procedure** | [`procedures/cross-host-smoke.md`](procedures/cross-host-smoke.md) |
| **re-review procedure** | [`procedures/re-review.md`](procedures/re-review.md) |
| **stacked-review procedure** | [`procedures/stacked-pr-review.md`](procedures/stacked-pr-review.md) |
| **fleet doc** | [`docs/agents/FLEET.md`](../../../docs/agents/FLEET.md) — "Model split" |

## Engine review checklist (shared-flow step 4)

Go through each explicitly. For every item, confirm compliance or raise an
issue.

**ECS invariants** — check against [`.claude/rules/cpp-ecs-smells.md`](../../rules/cpp-ecs-smells.md).

**Ownership / lifetime**
- `shared_ptr` where `unique_ptr` would do.
- Raw owning pointers (raw pointer = non-owning, always).
- Storing references or pointers to ECS component storage across ticks —
  archetype changes invalidate addresses.
- Capturing `this` or references to World managers in lambdas that outlive
  the World (e.g. lua callbacks registered before World teardown).
- Stored `g_*Manager` pointer or reference in any object whose lifetime can
  outlive `World` (background threads, sol2 callback closures, long-lived
  caches) — see `engine/world/CLAUDE.md` and `engine/CLAUDE.md`.
- New mutable namespace-scope variable in a header (`inline` or `extern`)
  outside a module `ir_*.hpp` entry point — state with no owner, teardown,
  or reset participation. Belongs on a manager, a singleton component
  (`IREntity::singleton<T>`), or a `.cpp` anonymous namespace; sanctioned
  patterns + detection grep in
  [`.claude/rules/cpp-globals.md`](../../rules/cpp-globals.md).

**Render pipeline**
- CPU frame-data struct out of sync with its GLSL `layout(std140)`
  counterpart. `vec3` members pad to 16 bytes; array elements stride to 16
  bytes; members crossing a 16-byte boundary need `alignas(16)`. If either
  the C++ struct (in `engine/render/include/irreden/render/`) or its shader
  `uniform` block changed, cross-reference both sides.
- shader references `binding = N` but the C++ `kBufferIndex_*` constant was
  not updated — the mismatch is silent. Confirm every bind-point index
  agrees on both sides.
- New shader file not following the `c_` / `v_` / `f_` / `g_` prefix.
- Canvas allocation before the canvas entity exists.
- Compute dispatch size doesn't match `voxelDispatchGridForCount()`.
- A shader that writes indirect-dispatch dims from a runtime count without
  capping `numGroupsX` at 1024 and spilling to `numGroupsY` (mirror
  `writeDispatchDims()` in `c_voxel_visibility_compact.glsl`) — an uncapped
  1-D grid is undefined past 65535 groups.
- A new runtime *uniform* mode-branch added to a hot compute kernel (voxel
  stage 1/2, `c_shapes_to_trixel`, lighting / sun-shadow / particle
  kernels) — predicated, not skipped; should be a compile-time `#if`
  specialization from a shared body
  (`docs/design/gpu-stage-timing-cost-model.md` §2).
- A rebased shader-kernel/encoding PR whose fork predates a carrier or
  encoding migration on master: verify every `encode*`/`decode*` call's
  arity and that the migration's carrier is threaded on the enabled path —
  byte-identity at default doesn't prove it
  (`engine/render/CLAUDE.md` §"Verifying render changes").
- new `*.glsl` added without a matching `*.metal` counterpart (if parity is
  intentionally deferred, the PR body must acknowledge it and reference a
  follow-up task).

**Lighting** (if the diff touches `system_*ao*`, `system_*shadow*`,
`system_*flood*`, `system_*fog*`, `system_build_light_occlusion_grid*`, or
any `c_compute_*shadow*.glsl` / `.metal`)
- Check 1 (grid coverage): `system_build_light_occlusion_grid.hpp` iterates
  the full voxel pool — it does **not** include `cull_viewport_state.hpp`
  and does not call `visibleIsoViewport`. Off-screen geometry participates
  in lighting by design.
- Check 2 (shadow-ring extent): if chunk streaming is involved, the
  resident-chunk set extends past the view frustum by
  `maxCasterHeight × cot(sunAltitude)` in the sun-projection direction.
- Check 3 (light-seed expansion): the flood-fill seed gather does not filter
  by `visibleIsoViewport` without expanding by `C_LightSource::radius_`.
  Off-screen light sources within radius must still seed on-screen tiles.
- Check 4 (AO/shadow guard band): when chunk streaming is active, the
  resident chunk set includes a 1-chunk guard band in all six directions for
  correct AO neighbor-voxel sampling.

**Math / coordinates** (see [`.claude/rules/cpp-math.md`](../../rules/cpp-math.md) for the full glm→IRMath substitution table)
- Mixing 3D world coords with iso 2D coords without going through
  `IRMath::pos3DtoPos2DIso` or a named helper.
- Hard-coded `x + y + z` where `IRMath::pos3DtoDistance` exists.
- PlaneIso axis swapped.

**Serialization** (when the diff touches `engine/asset/`, `engine/prefabs/irreden/voxel/`, or `engine/world/`)
- A struct annotated `// IRAsset: serialized` gained, lost, or renamed a
  field without bumping `static constexpr uint16_t kSaveVersion = N;` in the
  same diff. Fix: increment `kSaveVersion` and add a reader migration keyed
  on `(structType, oldVersion)` in the format's load function. (Save Format
  Extensibility Rule #3.)
- A new serialized record type with no `// IRAsset: serialized` annotation —
  the simplify and review-pr checks cannot guard it without the annotation.
  Add the annotation and set `kSaveVersion = 1` on the struct.
- A loader that mutates live state (entity splice, `setParent`, id-watermark
  advance) with a fallible decode positioned **after** its first mutation —
  a mid-apply decode failure leaves the world partially mutated. All decoding
  must complete (into staged buffers) before the first live mutation; see
  `engine/asset/CLAUDE.md` Extensibility Rule #5 (recurred #2228 → #2230).
- `SaveSerialize<C>::write` reading a per-frame-**derived** field instead of
  the authored source-of-truth. When a system re-derives the component's live
  representation each frame (rotation re-voxelize, GPU-owned state), the
  primary field holds a derived arrangement and the authored data sits in a
  companion field (`*Source*` / `*Snapshot*` / `pending*`, e.g.
  `rotationSourceVoxels_`) — verify the write reads the companion (PR #2240).

**Naming / style** — follow the table in [`docs/agents/CLAUDE-BASELINE.md`](../../../docs/agents/CLAUDE-BASELINE.md) §Naming.
- Private members take `m_`; public members take a trailing `_`. The reversed
  form — `m_` on a *public* member, or a trailing `_` on a *private* member —
  is the single most common slip. A private member that already carries `m_`
  is correct; do **not** flag it (this inversion produced a false-positive nit
  on PR #1706).
- Block comments that narrate change / investigation history — issue-by-issue
  forensics, `Before #X / now Y`, `was a misdiagnosis`, `retired (T-323)` —
  instead of stating a durable invariant. Durable *why* stays; history moves
  to the commit / PR / a `docs/design/` doc with at most a one-line `// see #N`
  backref. Single-sourced in
  [`CLAUDE-BASELINE.md`](../../../docs/agents/CLAUDE-BASELINE.md) §Style; most
  common in render-pipeline diffs.

**Tests / build**
- Code change with no corresponding test change where a test existed.
- New **public surface** with no covering test — judge per *surface*, not
  per PR: a PR that tests one surface while shipping others uncovered still
  fails this check; flag each uncovered surface as needs-fix unless the
  human explicitly waived tests ("no tests" stays a human-explicit waiver).
  Specifically call out:
  - a new **Lua binding** needs a `test/script/lua_*_test.cpp` exercising
    the sol2 seam (the existing `lua_*_test.cpp` files there are the
    precedent);
  - a facility with **two registration paths** (free-function params AND
    `System<N>` spec-member detection) needs both paths covered.
  Worked example (#2425): the per-system cadence gate shipped 13 solid
  SystemManager tests, but the new `IRSystem.*` cadence Lua surface and the
  `kCadence`/`kCadenceOffset` spec-member path shipped with zero coverage —
  under the old "a new test exists" wording that was unflaggable; under
  per-surface wording each gap is a needs-fix hook. Suite layout and
  authoring conventions: [`test/CLAUDE.md`](../../../test/CLAUDE.md).
- Build or format-check not run before opening the PR (check the commit
  message, or run `fleet-build --target format-changed` yourself if cheap).
- Verification claims green over an unclean exit: a PR body or run log that
  reports a crash / `RESULT=CRASH` (teardown included) but presents the
  verification as passing — needs-fix per
  [`docs/agents/FLEET.md`](../../../docs/agents/FLEET.md) §"Clean-exit
  policy". A crash observed but out of reach must be filed with forensics
  AND the lane reported failed.

**Acceptance vs the originating issue** (shared-flow step 4b)
- When the PR body carries `Closes #N` and issue N has a `## Plan` with
  `### Acceptance criteria`, dispatch the **acceptance grader**
  (`review-acceptance`) with the PR + issue numbers and fold its fragment
  into the review as `### Acceptance (issue #N)`.
- Verdict mapping: any **unmet** criterion is at least needs-fix — blocker
  if the miss means the shipped mechanism doesn't function at all. A
  missing `## Acceptance evidence` PR-body section whose criteria you can
  still establish from the diff is a nit; a criterion you cannot establish
  is unmet, not a nit.
- **unverifiable on <host>** rows are not penalized — confirm the smoke
  labels from step 5c cover the lane the grader named.

**Opportunistic fixes (fix-forward covenant)**
- A clearly-sectioned `## Opportunistic fixes` block in the PR body is the
  expected shape under FLEET.md §"Fix-forward", not scope creep — review the
  bundled fixes on their merits. Ask for a split only when a bundled fix
  materially raises the PR's risk (schema changes, cross-module refactors,
  behavior changes beyond what the section claims).

**Opus-only items** (Sonnet should not attempt these — escalate via the
verdict footer if any of these surfaces are touched)
- **GPU buffer lifetime across frames.** An SSBO/UBO bound on frame N and
  read on frame N+1 without a fence or explicit double-buffer swap; async
  readback recycled before completion (use-after-free).
- **Archetype-mutation race during structural-change deferral.** A tick that
  calls `addComponent`/`removeComponent`/`removeEntity` must use the
  deferred variant; touching the live archetype while a parallel system
  iterates invalidates component addresses silently.
- **Race between `flushStructuralChanges` and async GPU readback.** The
  readback's destination (or the entity it indexes) may vanish if the flush
  runs first — verify the readback's lifetime is decoupled (stable ID or its
  own refcount).
- **Allocator behavior in long-lived caches.** An `unordered_map<EntityId,T>`
  with a poor hash, or a never-shrunk `vector<T>`, grows unbounded — confirm
  an eviction path or clear teardown order.
- **Hot-path register pressure.** A per-entity tick touching >8 components or
  carrying >12 live locals across a loop body approaches the register file;
  flag tick paths that grew significantly, especially in `engine/render/` or
  `engine/world/`.

**Creation- or implementation-specific criteria**
- If the PR touches a subdirectory under `creations/` (or any implementation
  layered on the engine), read the nearest `CLAUDE.md` there **in addition
  to** this engine checklist. The engine checklist is the baseline; the
  creation's `CLAUDE.md` is the delta. Apply both. If the subdirectory has a
  dedicated `REVIEW.md`, prefer it for review-specific rules.

**Flow docs** (markdown-only PRs skip the code checklist — still check this)
- A bash fence in a changed `.md` that runs `gh pr edit|create` /
  `gh issue create` with `--body "$var"` instead of `--body-file`, or that
  consumes a `$var` in a `--body`/substitution line never assigned in the
  same fence — a worker running it literally executes `--body ""` and wipes
  the PR body (#2342; the `--body-file` rule generalizes
  REVIEWER-PROTOCOL.md §Posting the review body to flow docs).

**Fleet scripts** (if the diff touches `scripts/fleet/`)
- Inline comments — layout diagrams, role-list blocks, `--help` banners —
  still naming the old identifiers after a rename. `grep` the renamed-from
  names across the touched scripts; comment blocks drift silently because
  nothing compiles them.
- A `~/.fleet/state/state.json` consumer judging cache freshness from file
  mtime instead of the in-file `generated_at` (UTC epoch; `st_mtime` fallback
  only when `generated_at` is missing/unparseable) — followers rewrite the
  file each tick with the leader's `generated_at` preserved, so mtime lies.
  Shared helper: `fleet_poll_topology.state_age_seconds`; rule + rationale in
  `docs/agents/FLEET-RUNTIME.md` (recurred: fleet-dispatcher, #2231).

## Verdict-label swap commands (shared-flow step 5b)

Run the matching command as your very next bash call after `gh pr review`:

```bash
# approve, no nits in body:
gh pr edit <N> --remove-label "fleet:needs-fix" --remove-label "fleet:blocker" --remove-label "fleet:has-nits" --remove-label "fleet:awaiting-upstream-review" --remove-label "fleet:stacked-rebase" --remove-label "fleet:needs-base-update" --add-label "fleet:approved"

# approve WITH a non-empty Nits section:
gh pr edit <N> --remove-label "fleet:needs-fix" --remove-label "fleet:blocker" --remove-label "fleet:awaiting-upstream-review" --remove-label "fleet:stacked-rebase" --remove-label "fleet:needs-base-update" --add-label "fleet:approved" --add-label "fleet:has-nits"

# needs-fix (nits roll into the fix work; no separate label):
gh pr edit <N> --remove-label "fleet:approved" --remove-label "fleet:blocker" --remove-label "fleet:has-nits" --remove-label "fleet:awaiting-upstream-review" --remove-label "fleet:stacked-rebase" --remove-label "fleet:needs-base-update" --add-label "fleet:needs-fix"

# blocker:
gh pr edit <N> --remove-label "fleet:approved" --remove-label "fleet:needs-fix" --remove-label "fleet:has-nits" --remove-label "fleet:awaiting-upstream-review" --remove-label "fleet:stacked-rebase" --remove-label "fleet:needs-base-update" --add-label "fleet:blocker"
```

`fleet:has-nits` rides on top of `fleet:approved` and tells the author
"approved, clean up the nits before this lands" — author roles poll for it.

**Feedback-fix model class:** the dispatcher routes the fix iteration from
your labels — nits-only feedback runs on sonnet, `fleet:needs-fix` /
`fleet:blocker` runs on the opus class. If your findings indicate the
*approach itself* is wrong (not fixable with scoped edits), additionally
`--add-label "fleet:fable"` so the fix dispatches on the fable class — or
escalate `fleet:design-blocked` if it needs an architect decision.

## Engine notes

- Step 1's metadata fetch uses `gh api repos/jakildev/IrredenEngine/pulls/<N>/comments`
  for inline comments; `gh pr view --json` does not include them.
- Bail-label release uses `fleet-claim review-release <N> <your-worktree-name>`.
- The procedure files (`procedures/*.md`) say "`SKILL.md` step N" — read it as
  **step N of the shared flow** in
  [`docs/agents/skills/review-pr.md`](../../../docs/agents/skills/review-pr.md)
  (the step numbers are preserved); this wrapper is the entry point that
  points there.
