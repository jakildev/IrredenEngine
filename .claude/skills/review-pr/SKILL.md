---
name: review-pr
description: >-
  Reviews an open Irreden Engine PR with a fresh context and posts a
  structured review (ownership, ECS invariants, allocation hot paths,
  naming, tests, project-specific smells) plus the verdict label. Use when
  the user says "review PR <N>", "review #<N>", "review the last PR",
  "check my PR", "review any new PRs", "review the PR queue", "check for
  new PRs to review", "review the open PRs", or otherwise asks for a code
  review, and from a persistent reviewer-loop session whenever `gh pr
  list` surfaces a PR this fleet has not yet reviewed.
---

# review-pr (Irreden Engine)

**The flow lives in [`docs/agents/skills/review-pr.md`](../../../docs/agents/skills/review-pr.md).**
Read it first, then apply the deltas below. A procedure file's "`SKILL.md`
step N" means step N of the shared flow.

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

Confirm or raise each item.

**ECS invariants** — [`.claude/rules/cpp-ecs-smells.md`](../../rules/cpp-ecs-smells.md).
- A change-gated recompute the diff touches triggers on every mutable
  input its function reads (`.claude/rules/cpp-ecs.md` §"A change-gated
  recompute must trigger on every input its function reads").

**Ownership / lifetime**
- `shared_ptr` where `unique_ptr` would do; raw owning pointers (raw =
  non-owning, always).
- References or pointers into ECS component storage held across ticks.
- `this` or World-manager references captured in lambdas that outlive the
  World; a stored `g_*Manager` pointer in any object that can outlive
  `World` (`engine/world/CLAUDE.md`, `engine/CLAUDE.md`).
- A new mutable namespace-scope header variable outside a module
  `ir_*.hpp` entry point —
  [`.claude/rules/cpp-globals.md`](../../rules/cpp-globals.md).

**Render pipeline**
- CPU frame-data struct vs its GLSL `layout(std140)` block: `vec3` pads
  to 16 bytes, arrays stride 16, boundary-crossing members need
  `alignas(16)`; cross-reference both sides when either changed.
- Every shader `binding = N` agrees with its C++ `kBufferIndex_*`.
- A new shader file without the `c_` / `v_` / `f_` / `g_` prefix.
- Canvas allocation before the canvas entity exists.
- Compute dispatch size not from `voxelDispatchGridForCount()`;
  indirect-dispatch dims from a runtime count without capping
  `numGroupsX` at 1024 and spilling to `numGroupsY` (mirror
  `writeDispatchDims()` in `c_voxel_visibility_compact.glsl`).
- A new runtime uniform mode-branch in a hot compute kernel (voxel stage
  1/2, `c_shapes_to_trixel`, lighting / sun-shadow / particle kernels) —
  should be a compile-time `#if` specialization from a shared body
  (`docs/design/gpu-stage-timing-cost-model.md` §2).
- A rebased shader-kernel/encoding PR forked before a carrier or encoding
  migration on master: check every `encode*`/`decode*` arity and that the
  carrier is threaded on the enabled path — byte-identity at default
  proves nothing (`engine/render/CLAUDE.md` §"Verifying render changes").
- A new `*.glsl` without its `*.metal` counterpart, unless the body
  acknowledges the deferral and names a follow-up.

**Lighting** (`system_*ao*`, `system_*shadow*`, `system_*flood*`,
`system_*fog*`, `system_build_light_occlusion_grid*`, `c_compute_*shadow*`
shaders)
- Grid coverage: `system_build_light_occlusion_grid.hpp` iterates the full
  voxel pool — no `cull_viewport_state.hpp`, no `visibleIsoViewport`.
- Shadow-ring extent: with chunk streaming, the resident set extends past
  the frustum by `maxCasterHeight × cot(sunAltitude)` in the
  sun-projection direction.
- Light-seed expansion: the flood-fill seed gather does not filter by
  `visibleIsoViewport` without expanding by `C_LightSource::radius_`.
- AO/shadow guard band: with chunk streaming, a 1-chunk guard band in all
  six directions.

**Math / coordinates** ([`.claude/rules/cpp-math.md`](../../rules/cpp-math.md))
- 3D world coords mixed with iso 2D coords outside
  `IRMath::pos3DtoPos2DIso` or a named helper; hard-coded `x + y + z`
  where `IRMath::pos3DtoDistance` exists; PlaneIso axis swapped.

**Serialization** (`engine/asset/`, `engine/prefabs/irreden/voxel/`,
`engine/world/`)
- A `// IRAsset: serialized` struct gained, lost, or renamed a field
  without bumping `static constexpr uint16_t kSaveVersion = N;` and adding
  a reader migration keyed on `(structType, oldVersion)` (Extensibility
  Rule #3).
- A new serialized record type with no `// IRAsset: serialized`
  annotation — add it with `kSaveVersion = 1`.
- A loader that mutates live state with a fallible decode after its first
  mutation — decode fully into staged buffers first
  (`engine/asset/CLAUDE.md` Extensibility Rule #5).
- `SaveSerialize<C>::write` reading a per-frame-derived field instead of
  the authored companion (`*Source*` / `*Snapshot*` / `pending*`).

**Naming / style** — [`CLAUDE-BASELINE.md`](../../../docs/agents/CLAUDE-BASELINE.md) §Naming, §Style.
- `m_` on a public member or trailing `_` on a private one (a private
  member with `m_` is correct — do not flag it).
- Block comments narrating change or investigation history instead of a
  durable invariant.

**Tests / build**
- A code change with no test change where a test existed.
- New public surface with no covering test, judged per surface: a new Lua
  binding needs a `test/script/lua_*_test.cpp`; a facility with two
  registration paths (free-function params and `System<N>` spec-member
  detection) needs both covered. Each uncovered surface is needs-fix
  unless the human explicitly waived tests. Layout:
  [`test/CLAUDE.md`](../../../test/CLAUDE.md).
- Build or `fleet-build --target format-changed` not run before opening.
- Verification claimed green over an unclean exit (`RESULT=CRASH`,
  teardown included) — needs-fix per
  [`FLEET.md`](../../../docs/agents/FLEET.md) §"Clean-exit policy"; an
  out-of-reach crash is filed with forensics and the lane reported failed.
- A grep cited as a completeness gate keyed on a retired symbol does not
  cover value-equivalent bare literals (`T{0}`, `return 0;`).

**Acceptance vs the originating issue** (shared-flow step 4b)
- `Closes #N` with planned `### Acceptance criteria` → dispatch
  `review-acceptance`; fold its fragment in as `### Acceptance (issue #N)`.
- Any unmet criterion is at least needs-fix — blocker if the shipped
  mechanism does not function. A missing `## Acceptance evidence` section
  whose criteria the diff still establishes is a nit; a criterion you
  cannot establish is unmet.
- "unverifiable on <host>" rows are not penalized — confirm the step-5c
  smoke labels cover that lane.

**Opportunistic fixes** — a sectioned `## Opportunistic fixes` block is the
expected shape under FLEET.md §"Fix-forward"; review the bundled fixes on
their merits and ask for a split only when one materially raises the PR's
risk.

**Opus-only** (Sonnet escalates via the footer)
- GPU buffer lifetime across frames: an SSBO/UBO bound on frame N and read
  on N+1 without a fence or double-buffer swap; async readback recycled
  before completion.
- Archetype mutation during structural-change deferral:
  `addComponent`/`removeComponent`/`removeEntity` in a tick must use the
  deferred variant.
- `flushStructuralChanges` racing an async GPU readback — the readback's
  lifetime must be decoupled (stable ID or its own refcount).
- Unbounded long-lived caches (`unordered_map<EntityId,T>` with a poor
  hash, a never-shrunk `vector<T>`) — confirm eviction or teardown.
- Register pressure: a per-entity tick touching >8 components or carrying
  >12 live locals across a loop body, especially in `engine/render/` or
  `engine/world/`.

**Creations** — a PR under `creations/` also gets the nearest `CLAUDE.md`
(or `REVIEW.md`) applied on top of this checklist.

**Flow docs** (markdown-only PRs still get this) — a bash fence running
`gh pr edit|create` / `gh issue create` with `--body "$var"` instead of
`--body-file`, or consuming a `$var` never assigned in the same fence.

**Fleet scripts** (`scripts/fleet/`)
- Inline comments, layout diagrams, and `--help` banners still naming
  pre-rename identifiers.
- A `~/.fleet/state/state.json` consumer judging freshness from file mtime
  instead of the in-file `generated_at` — use
  `fleet_poll_topology.state_age_seconds` (`docs/agents/FLEET-RUNTIME.md`).

## Verdict-label swap commands (shared-flow step 5b)

Your very next bash call after `gh pr review`. Fleet reviewers pass
`--agent <your-worktree-name>`; an interactive human omits it.

```bash
fleet-review-verdict verdict-approve <N> --agent <your-worktree-name>        # approve, no nits
fleet-review-verdict verdict-approve-nits <N> --agent <your-worktree-name>   # approve with a non-empty Nits section
fleet-review-verdict verdict-needs-fix <N> --agent <your-worktree-name>      # nits roll into the fix work
fleet-review-verdict verdict-blocker <N> --agent <your-worktree-name>
```

The dispatcher routes the fix iteration from your labels: nits-only on
sonnet, `fleet:needs-fix` / `fleet:blocker` on the opus class. If the
approach itself is wrong, add `--add-label "fleet:fable"` so the fix
dispatches on the fable class, or escalate `fleet:design-blocked` for an
architect decision.
