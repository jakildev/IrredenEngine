# TASKS

Shared task queue for parallel agents. Both human and agent maintainers
append here, and the next unblocked item is what an idle agent should pick up.

## How to use this file

1. **Picking a task:** skim the `## Open` section. Find the first `[ ]` item
   whose **Owner** is `free` or your worktree name, and whose **Blocked by**
   list is empty. **Then cross-check `gh pr list --state open`** — if any
   open PR's title or branch name looks like it's already working on that
   task, skip to the next candidate. The open-PR list is the authoritative
   claim signal; the `[~]` flip on a feature branch is invisible to other
   agents until merge, so two agents can race to claim the same task in the
   ~minutes-to-hours window between picking and merging. Cross-checking
   `gh pr list` closes most of that race.

   Once you've picked, change the status to `[~]` (in progress), set Owner
   to your worktree, and push the edit in your first commit so other agents
   see it once your PR merges.
2. **Finishing a task:** change `[~]` to `[x]`, set the final commit or PR
   URL in the **Links** line, and move the item to `## Done — last 20` at
   the bottom. Keep only the last 20 done items; prune older ones.
3. **Adding a task:** append to `## Open` with the template below. Err on the
   side of creating small tasks (one PR's worth of work). If a task needs
   research first, file it as `Research:` — the deliverable is a short
   findings note, not code. The fastest way to add a task is to ask the
   `queue-manager` pane in the fleet — paste a rough description and it
   will categorize, tag, format, and file the queue-update PR for you.
4. **Blocking on another task:** put the blocking task's title in
   **Blocked by**. An agent should skip blocked items. For cross-repo
   blocks (game blocked on engine), put the engine PR URL in **Blocked by**
   so any agent can resolve it without context.
5. **Touching this file:** always stage and commit `TASKS.md` edits in the
   same PR as the work they describe, so history stays consistent.
   Queue-maintenance-only PRs (e.g. `queue: add task X`, batched task
   adds) are also explicitly allowed and merge fast.

### Race conditions and how the fleet handles them

`TASKS.md` is git-versioned, which means an agent's `[~]` claim only
becomes visible to other agents after its PR merges. Between picking and
merging, two agents can independently pick the same task. The fleet
defends against this in three layers:

1. **Pre-pick `gh pr list` cross-check** (rule 1 above) — closes most
   of the window.
2. **Merge conflict on the second `[~]` flip** — both PRs edit the same
   line in `TASKS.md`, so whichever one merges second will hit a
   GitHub-side merge conflict and refuse to auto-merge. The human
   reviewer sees the conflict before merging and rejects the loser.
3. **Loser requeues and picks again** — the agent whose PR conflicts
   uses `start-next-task` to reset to a fresh branch off `origin/master`,
   picks the next available task, and moves on. The work isn't lost; it
   just gets rescheduled.

The local `fleet-claim` script adds a fourth layer: agents call
`fleet-claim claim "T-NNN" <agent>` using the task's **ID** (not the
free-text title) before starting work. The short deterministic ID
prevents the failure mode where two agents slugify different
paraphrasings of the same title and both succeed. If `fleet-claim`
returns exit 1 (already taken), skip to the next task.

This file is the **engine-level** task queue. Private creations that live
under `creations/` may define their own `TASKS.md` inside their own
directory — those are tracked independently and should not be mixed here.
Do not queue game or creation-specific gameplay tasks in this file; queue
them in the creation's own `TASKS.md`.

## Task template

```
- [ ] **<short title>** — <one-line goal>
  - **ID:** T-NNN  (sequential, assigned by the queue-manager)
  - **Area:** engine/render | engine/entity | engine/prefabs/... | docs | build | creations/demos/... | ...
  - **Model:** opus | sonnet  (which model should run this)
  - **Owner:** free | <worktree-name>
  - **Blocked by:** (none) | <title of blocking task>
  - **Stack:** T-XXX..T-YYY <slug>  (optional — only for tasks in a stacked chain sharing a parent epic; omit for standalone tasks)
  - **Acceptance:** <concrete check: build passes, test X passes, PR merged, screenshot Y looks like Z>
  - **Issue:** (none) | #N  (GitHub issue number, if task originated from an issue)
  - **Notes:** <context, links, prior attempts>
  - **Links:** (fill in PR URL when done)
```

The **ID** is the canonical claim key. When calling `fleet-claim`, pass the
task ID (e.g. `fleet-claim claim "T-003" sonnet-fleet-1`), **not** the
free-text title. IDs are short and unambiguous — agents can't accidentally
paraphrase them, which is the failure mode that free-text title slugification
is vulnerable to.

The **Stack** field groups child tasks of a shared parent epic so a
human can follow the chain across `## Open`. Format:
`T-<min>..T-<max> <slug>`; slug is a kebab-case identifier shared by
all siblings. Informational only — `fleet-claim` and the scout cache
ignore it. Standalone tasks omit the field entirely. The queue-manager
populates it during ingestion when a child issue declares membership;
see `role-queue-manager.md` for the detection rule.

Status markers: `[ ]` open, `[~]` in progress, `[x]` done, `[!]` blocked/stuck.

### Model tagging (important)

Tag every task with the intended model. Default assumption:

- `[opus]` — anything touching `engine/render/`, `engine/entity/`,
  `engine/system/`, `engine/world/`, `engine/audio/`, `engine/video/`,
  `engine/math/` (non-trivial), or ECS/render optimization, or concurrency,
  or ownership/lifetime rules. Also final review on anything important.
- `[sonnet]` — test generation, doc passes, mechanical refactors with a
  clear spec, first-pass code review, clearly-scoped items already thought
  through, anything in `creations/demos/`, small bounded shader tweaks.

A Sonnet agent that picks up an `[opus]` task should escalate instead of
charging ahead. A Sonnet agent that finds a `[sonnet]` task is subtler
than expected (touches an invariant, a lifetime, a race) should stop and
requeue with `[opus]`. [`docs/agents/FLEET.md`](docs/agents/FLEET.md) "Model split" has the full split.

## Good tasks to queue here (engine-only)

Small and bounded is the target. Good shapes for this queue:

- **Test generation** — "write exhaustive tests for `engine/math/physics.hpp`
  ballistic helpers"
- **Docs / API reference** — "document every `IRRender::` free function in
  `engine/render/CLAUDE.md`"
- **Benchmark / profiling report** — "profile `IRShapeDebug` at zoom 4 with
  N voxels and file a report"
- **Isolated refactor** — "port `engine/common/ir_constants.hpp` to constexpr"
- **Build / CI hardening** — "add a `format-check` CI target that fails on
  stale clang-format output"
- **FFmpeg / audio interface hardening** — "add bounds checks to
  `VideoRecorder::submitVideoFrame` stride handling"
- **Compile-time cleanup** — "reduce `engine/render/` TU rebuild cascade by
  moving X out of the low header"
- **Shader hygiene** — "extract repeated iso-projection math in
  `engine/render/src/shaders/` into `ir_iso_common.glsl`"

Avoid:

- Tasks that touch core ECS types (`engine/entity/`) — do those by hand.
- "Refactor the render loop" — too broad, no single PR scope.
- Anything that would require changing the public `ir_*.hpp` surface across
  multiple modules in one PR.
- Gameplay or content work for any specific creation — belongs in that
  creation's own task queue.

---

## Open

<!-- Add tasks below this line. -->

- [~] **Lua-driven ECS: Lua port of perf_grid + perf parity gate** — new demo creations/demos/lua_perf_grid/ mirroring perf_grid (262k entities, wave animation, same render pipeline) entirely in Lua; parity gate: Lua wave-animation per-tick cost <= 1.5x C++ equivalent
  - **ID:** T-104
  - **Area:** engine/script, creations/demos/lua_perf_grid
  - **Model:** opus
  - **Owner:** claude/T-104-lua-perf-grid
  - **Blocked by:** (none)
  - **Acceptance:** (1) fleet-build --target IRLuaPerfGrid clean on linux-debug; (2) fleet-run IRLuaPerfGrid runs without crash (64x64x64 voxel grid, wave animation, same render pipeline as perf_grid); (3) parity gate: Lua wave-animation system per-tick cost <= 1.5x C++ SystemPeriodicIdlePositionOffset per-tick cost measured via IRProfile with profiling_enabled=true; (4) measured ratio documented in docs/design/lua-driven-ecs.md retrospective; (5) if gate fails: design doc PR amended with corrective decision before further work
  - **Issue:** #492
  - **Notes:** PR 6 of 6 for parent epic #293 — formal acceptance gate for the entire Lua-driven ECS stack. Full architect plan in .fleet/plans/T-104.md. Blocked by T-103 (hot-reload). If parity gate fails, this PR does not merge; instead amend T-099's design doc with corrective decision (LuaJIT migration, codegen-bound bodies, etc.).
  - **Links:**




- [ ] **LuaJIT 2.1 runtime migration** — swap Lua 5.4 → LuaJIT 2.1 as the engine's Lua runtime; update cmake dep, audit .lua files for 5.4-only features, verify all bindings and tests pass under LuaJIT, document EVAL-mode perf floor
  - **ID:** T-105
  - **Area:** engine/script, build
  - **Model:** opus
  - **Owner:** free
  - **Blocked by:** (none)
  - **Acceptance:** (1) LuaJIT 2.1 replaces Lua 5.4 in cmake/; all sol2 usertype/registerType bindings compile and function; (2) Lua 5.4-only feature audit complete — goto, integer subtypes, <const>/<close>, generational GC, bit32 — findings documented in PR body; (3) fleet-build --target IrredenEngineTest clean + 100% pass; fleet-build --target IRPerfGrid clean; creations/demos/lua_pipeline_demo runs end-to-end; all Lua-driven ECS tests pass; (4) EVAL-mode perf re-run on testbed from PR #563 documented (target 2–10× C++ perf_grid baseline); (5) engine/script/CLAUDE.md updated with LuaJIT 2.1 runtime note (available: bit ops, FFI; not available: 5.4-only features)
  - **Issue:** #585
  - **Notes:** T-104 measurement showed ≥10 000× EVAL-mode slowdown on Lua 5.4 sol2 dispatch; LuaJIT trace JIT collapses that inner-loop shape. Architect decision tracked in #566. Blocks T-106 (codegen pipeline foundation) and PR #563 (perf-grid testbed needs LuaJIT measurement). Out of scope: codegen pipeline (T-106/107/108), parity gate (T-109), LuaJIT FFI in engine bindings.
  - **Links:**


- [~] **Render: HDR pipeline — RGBA16F canvas, tonemap pass, exposure control, sky term** — grow LDR pipeline into HDR; RGBA16F canvas color attachment; tonemap pass between LIGHTING_TO_TRIXEL and TRIXEL_TO_FRAMEBUFFER; exposure uniform; additive sky-term from emissive top hemisphere
  - **ID:** T-118
  - **Area:** engine/render, shaders/glsl, shaders/metal
  - **Model:** opus
  - **Owner:** claude/T-118-hdr-pipeline
  - **Blocked by:** (none)
  - **Acceptance:** (1) bright emissive lights no longer clip at white; saturation preserved through lighting → tonemap chain; (2) new lighting demo (IRLightingHDR or similar) exercises full HDR pipeline; (3) existing lighting demos (IRLightingCombined, IRLightingPoint, IRLightingSpot, IRLightingEmissive, IRLightingSunShadow) look identical to pre-HDR LDR output at default exposure; (4) fleet-build clean on linux-debug AND macos-debug
  - **Issue:** #366
  - **Notes:** Follow-up from lighting-fidelity-polish PR (audit findings #35-#38). Not in the lighting-fidelity-polish PR because HDR is a separate correctness dimension requiring its own tonemap tuning, demo screenshots, and perf measurement. Pick one tonemap operator and ship it (Reinhard, ACES, or Uncharted-2). Sky term: emissive top hemisphere driving additive contribution that cuts off at occlusion — cheap and visually impactful.
  - **Links:**


- [~] **Render: 2x2 PCF in screen-space sun shadow lookup** — replace single-texel sun shadow sample with bilinearly-weighted 2x2 kernel to soften shadow boundary aliasing
  - **ID:** T-133
  - **Area:** shaders/glsl, shaders/metal
  - **Model:** sonnet
  - **Owner:** claude/T-133-2x2-pcf-sun-shadow
  - **Blocked by:** (none)
  - **Acceptance:** (1) IRShapeDebug --auto-screenshot 10 at zoom 4 and zoom 8 — shadow boundaries soften by ~1 sun-space texel, no jaggies or broken-comb edges; (2) main_sun_shadow.cpp and main_combined.cpp show smooth shadow edges at all four cardinal yaws; (3) per-frame cost regression ≤2% of total render-pipeline time (measure via IR_PROFILE_BLOCK("ComputeSunShadow")); (4) linux-debug OpenGL and macos-debug Metal produce visually equivalent softening; (5) fleet-build clean on linux-debug AND macos-debug
  - **Issue:** #445
  - **Notes:** Edit c_compute_sun_shadow.glsl:112-128 and metal/c_compute_sun_shadow.metal:85-107. Pseudocode in issue body. Both sibling tickets (T-131 AABB widening, T-132 normal-bias) are independent and can land in any order.
  - **Links:**


- [~] **C_SpriteSheet: onDestroy GPU texture cleanup** — add onDestroy() to C_SpriteSheet calling IRRender::destroyResource(textureHandle_) to prevent GPU texture leak on entity destroy
  - **ID:** T-134
  - **Area:** engine/prefabs/irreden/render
  - **Model:** sonnet
  - **Owner:** claude/T-134-sprite-sheet-on-destroy
  - **Blocked by:** (none)
  - **Acceptance:** (1) C_SpriteSheet gains onDestroy() that calls IRRender::destroyResource(textureHandle_); (2) pattern matches C_TriangleCanvasTextures and C_TrixelFramebuffer; (3) fleet-build clean on linux-debug; (4) no crash on sprite_demo
  - **Issue:** #535
  - **Notes:** Escalated from PR #527 (T-098 sprite Lua bindings review), Nit 4. Currently harmless for single-load demos; becomes a GPU leak for long-running scenes with sheet reload.
  - **Links:**


- [ ] **fleet-up.conf concurrency cap — design refinement** — implement fleet-up.conf bash-sourceable config for per-role concurrency caps in fleet-dispatcher, resolving design questions from T-125 pickup attempt
  - **ID:** T-135
  - **Area:** tooling
  - **Model:** opus
  - **Owner:** free
  - **Blocked by:** (none)
  - **Acceptance:** (1) ~/.fleet/fleet-up.conf created by fleet-up on first run with documented defaults (FLEET_CAP_OPUS_WORKER=2, FLEET_CAP_SONNET_AUTHOR=4); (2) dispatcher honors cap edits on next tick; (3) removing conf file restores no-cap behavior; (4) with cap=1 and both opus-worker panes idle, only one picks up the trigger; (5) cap-defer reason logged when cap kicks in
  - **Issue:** #547
  - **Notes:** Design questions resolved in issue body: sonnet-fleet key maps to FLEET_CAP_SONNET_AUTHOR (canonical role name); bash-sourceable format at ~/.fleet/fleet-up.conf; count = active dispatch records + reservations mapped to role; absent conf → no cap. Full design spec in #547.
  - **Links:**


- [ ] **Systems: member-on-System<N> registration helper** — add registerSystem<> to ir_system.hpp so systems use member fields/functions instead of explicit Params + setSystemParams boilerplate; migrate all prefab systems; update CLAUDE.md
  - **ID:** T-136
  - **Area:** engine/system, engine/prefabs/irreden/render, engine/prefabs/irreden/input
  - **Model:** opus
  - **Owner:** free
  - **Blocked by:** (none)
  - **Acceptance:** (1) registerSystem<SystemName, Components...>(name) lands in ir_system.hpp with concepts for tick/beginTick/endTick/relationTick hooks; (2) all prefab systems using setSystemParams migrated to member-on-System<N> shape (grep -l "setSystemParams(systemId" engine/prefabs/); (3) engine/system/CLAUDE.md "Per-system parameters" section rewritten with preferred/escape-hatch forms; (4) new test covers all four hooks with per-tick field persistence; (5) no frame-time regression on IRShapeDebug; (6) fleet-build clean on linux-debug
  - **Issue:** #580
  - **Notes:** Multiple PRs expected: PR 1 lands helper + CLAUDE.md update; subsequent PRs migrate one system cluster each. Full spec and migration list in #580. Backward-compat preserved — createSystem/setSystemParams/getSystemParams stay public.
  - **Links:**


- [~] **fleet-claim: hard model-tag gate** — refuse claim when role model mismatches the task's Model: field; exits non-zero with clear error; no-op when task has no Model: field
  - **ID:** T-137
  - **Area:** tooling
  - **Model:** sonnet
  - **Owner:** claude/T-137-fleet-claim-model-gate
  - **Blocked by:** (none)
  - **Acceptance:** (1) fleet-claim T-X from sonnet-author shell on opus-tagged task exits non-zero with clear error, writes nothing; (2) fleet-claim T-X from opus-worker shell proceeds normally; (3) task with no Model: field claims from either role; (4) each role wrapper sets FLEET_ROLE_MODEL before invoking fleet-claim
  - **Issue:** #582
  - **Notes:** Filed from T-130 double-claim incident (PRs #575 + #579). Atomic master-side lock is a separate issue (#583, T-138). Model tag is advisory today — this makes it a hard gate.
  - **Links:**


- [ ] **fleet-claim: atomic master-side TASKS.md lock** — push [~] + Owner to master as part of claim handshake so concurrent claims fail fast on non-fast-forward rather than silently racing to [~]
  - **ID:** T-138
  - **Area:** tooling
  - **Model:** opus
  - **Owner:** free
  - **Blocked by:** (none)
  - **Acceptance:** (1) two concurrent fleet-claim T-X invocations: exactly one succeeds, other exits "already claimed" and writes nothing; (2) after successful claim, TASKS.md on master shows [~] + Owner within 30s; (3) fleet-claim --reclaim T-X resets [~] → [ ] if Owner branch doesn't exist on remote
  - **Issue:** #583
  - **Notes:** Filed from T-130 double-claim incident (PRs #575 + #579). Model-tag gate is a separate issue (#582, T-137). Implementation: ephemeral branch claude/queue-claim-T-NNN, one-line TASKS.md update pushed before worker starts feature branch; direct push or tiny merger-handled PR both viable.
  - **Links:**


---

## In progress

<!-- Tasks currently being worked on. Mirror of [~] items above. -->


---

## Done — last 20

<!-- Completed tasks, newest first. Prune older entries beyond 20. -->

- [x] **T-130** — Hover: unify GUI / world / trixel sources in SYSTEM_ENTITY_HOVER_DETECT · Owner: claude/T-130-gui-hitbox-source · PR: https://github.com/jakildev/IrredenEngine/pull/575
- [x] **T-131** — Render — widen iso rasterization to shadow-feeder AABB · Owner: claude/T-131-shadow-feeder-aabb · PR: https://github.com/jakildev/IrredenEngine/pull/576
- [x] **T-132** — Render: sun shadow normal-bias offset + slope-scale bias tuning · Owner: claude/T-132-shadow-normal-bias · PR: https://github.com/jakildev/IrredenEngine/pull/573
- [x] **T-103** — Lua-driven ECS — hot-reload of Lua system bodies · Owner: claude/T-103-lua-system-body-hot-reload · PR: https://github.com/jakildev/IrredenEngine/pull/559
- [x] **T-092** — Render: final occupancy-grid teardown — comment cleanup · Owner: claude/T-092-occupancy-grid-teardown · PR: https://github.com/jakildev/IrredenEngine/pull/567
- [x] **T-129** — Fleet: fleet-up bootstrap-trigger extension for worker/reviewer roles · Owner: claude/T-129-fleet-up-bootstrap-triggers · PR: https://github.com/jakildev/IrredenEngine/pull/562
- [x] **T-102** — Lua-driven ECS: pipeline composition + enum bindings + modifier-framework bindings · Owner: claude/T-102-lua-pipeline-modifier-bindings · PR: https://github.com/jakildev/IrredenEngine/pull/544
- [x] **T-128** — Fleet: demote queue-manager to scout-driven script — add fleet-queue-tick, remove LLM pane · Owner: claude/T-128-relanded · PR: https://github.com/jakildev/IrredenEngine/pull/564
- [x] **T-115** — Docs: cross-author stacking lifecycle in FLEET.md · Owner: claude/T-115-cross-author-stacking-docs · PR: https://github.com/jakildev/IrredenEngine/pull/557
- [x] **T-127** — Fleet: queue-manager role doc — replace hand-edit loop with fleet-tasks-render call · Owner: claude/T-127-queue-manager-fleet-tasks-render · PR: https://github.com/jakildev/IrredenEngine/pull/556
- [x] **T-126** — Render: migrate light-volume propagation off CPU-built OccupancyGrid SSBO · Owner: claude/T-126-occupancy-ssbo-decouple · PR: https://github.com/jakildev/IrredenEngine/pull/546
- [x] **T-125** — Fleet: per-role concurrency cap config + dispatcher enforcement · Owner: claude/T-125-fleet-concurrency-cap · PR: https://github.com/jakildev/IrredenEngine/pull/548
- [x] **T-112** — Worker role docs: stackable-blocked fallback pickup tier · Owner: claude/T-112-stackable-blocked-pickup · PR: https://github.com/jakildev/IrredenEngine/pull/545
- [x] **T-116** — Render: per-canvas light scope via CHILD_OF relation · Owner: claude/T-116-per-canvas-light-scope · PR: https://github.com/jakildev/IrredenEngine/pull/541
- [x] **T-124** — Fleet: stuck-worktree staleness escalation · Owner: claude/T-124-stuck-worktree-escalation · PR: https://github.com/jakildev/IrredenEngine/pull/542
- [x] **T-123** — Fleet: worktree naming migration (opus-worker-N → worktree-N) · Owner: claude/T-123-fleet-up-boot-reconciliation · PR: https://github.com/jakildev/IrredenEngine/pull/540
- [x] **T-122** — Fleet: role docs startup reservation check · Owner: claude/T-122-worker-resumption-step · PR: https://github.com/jakildev/IrredenEngine/pull/539
- [x] **T-121** — Fleet: dispatcher reservation-aware pane selection · Owner: claude/T-121-auto-reserve-on-claim · PR: https://github.com/jakildev/IrredenEngine/pull/538
- [x] **T-113** — Merger: cascade rebase on upstream force-push for stacked PRs · Owner: claude/T-113-merger-cascade-rebase · PR: https://github.com/jakildev/IrredenEngine/pull/537
- [x] **T-111** — Scout: pre-compute stackable_blocker_pr field · Owner: claude/T-111-scout-stackable-blocker-pr · PR: https://github.com/jakildev/IrredenEngine/pull/536
