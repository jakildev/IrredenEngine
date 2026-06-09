# Entity editor Phase 2 — Hierarchies & skeletal voxels (re-plan)

**Umbrella:** #605
**Repo:** jakildev/IrredenEngine
**Author:** opus-architect

## Context

#605 was filed in May against a foundation that has since landed, so half its
sub-tasks describe deprecated machinery. This re-plan rewrites Phase 2 against
current reality and takes one architectural simplification.

What landed since #605 was written:

- **#731 / #734** — `C_LocalTransform` (SQT) + `C_WorldTransform` +
  `SYSTEM_PROPAGATE_TRANSFORM` (two-pass topological compose, T-378). The
  `CHILD_OF` parent chain composes uniformly. **FK is free** — rotate a
  joint's local transform and propagation deforms the chain.
- **#737** — joints are first-class entities: `C_Skeleton.joints_` (ordered
  `EntityId` list; the index *is* `bone_id`) + `C_Joint` tag + `CHILD_OF`.
  `C_JointHierarchy` is a deprecation shim.
- **#1396** — GPU voxel-position prepass: binding 17 carries a **per-voxel**
  transform slot in the `.w` lane; binding 18 is a 4096-entry `mat4` buffer
  with a slot allocator; `c_update_voxel_positions.glsl:48` computes
  `world = transforms[slot] * localPos`. This is the load-bearing insight.
- **#666** — `.rig` v1 format (JNTS chunk; BIND chunk for bind pose).

### Architectural decision (locked with the human)

**Unify skeletal skinning on the #1396 prepass.** The design doc and
`component_skeleton.hpp` assume a *separate* joint-matrix SSBO (binding 21)
plus a new `bone_id` branch in `c_voxel_to_trixel_stage_1.glsl`. That buffer
is allocated (sized for SDF shapes) but never filled — speculative
scaffolding. Rigid skinning (one bone per voxel, exactly what a single
`uint8 bone_id` expresses — there are no per-voxel weights) is *already* the
#1396 operation with the per-voxel slot pointing at a **bone** instead of an
**entity**. So:

1. Per joint, CPU computes `skinMatrix = jointWorld × bindInverse`
   (cost = #joints, not #voxels) and writes it to a transform slot in the
   existing binding-18 buffer.
2. At voxel-set seed time, set each voxel's `.w` to `slotBase + bone_id`.
3. The existing, render-verify-passing `c_update_voxel_positions.glsl` does
   the skin. **No new shader, no stage-1 change, binding 21 retired for the
   voxel path.** Unrigged voxels (`bone_id → identity slot`) cost nothing.

`bone_id` is per-voxel `uint8` (≤256 joints/skeleton), per-skeleton-local;
`slotBase` is the skeleton's global slot offset in the shared 4096 budget.
A 30-joint snake uses 30 slots — no indexing ambiguity.

### Deferred (not in this epic)

- **Transform/relation perf hardening** (parent-node cache on `ArchetypeNode`
  to kill the per-frame O(archetype) parent scan in propagation;
  partition-cache invalidation granularity; `removeBySource` reverse index).
  Skeletons already query joints O(joints) via `C_Skeleton.joints_` and
  parent-of-X is O(1). Land these **only if the 30-joint snake shows
  propagation in the profile**.
- **Severance / dismemberment** — gameplay, depends on dynamic adaptation;
  Phase 4+. The entity-based model already permits it; Phase 2 stays
  authoring + deformation only.
- **Curve-approximated rigs** (bezier/Catmull-Rom skeleton to approximate a
  long snake with fewer joints) — interesting follow-up, sits with IK in
  Phase 4 (#607). Phase 2 ships the explicit 30-joint snake.

## Child tickets

Model tag in brackets. Dependency chain is a DAG with 2.1 as the single
unblocked head; 2.5 fans the editor track off 2.1.

| Sub-task | Issue | Model | Blocked by |
|---|---|---|---|
| 2.1 bind-pose + skin-matrix helper | #1602 | opus | — |
| 2.2 SYSTEM_UPDATE_JOINT_MATRICES | #1603 | opus | #1602 |
| 2.3 per-voxel bone→slot skinning | #1605 | opus | #1603 |
| 2.4 retire binding-21 | #1606 | sonnet | #1605 |
| 2.5 joint authoring | #1604 | opus | #1602 |
| 2.6 skeleton tree panel | #1607 | sonnet | #1604 |
| 2.7 bone_id painting | #1608 | sonnet | #1604 |
| 2.8 FK pose editing | #1610 | opus | #1605, #1604 |
| 2.9 .rig save/load | #1609 | sonnet | #1604 |
| 2.10 demo entities (snake/desk/+2) | #1611 | sonnet | #1610, #1609 |
| 2.11 render-verify + consistency | #1612 | sonnet | #1611 |

### 2.1 — engine/voxel: bind-pose on C_Skeleton + skin-matrix helper [opus] (#1602)
Add `std::vector<IRMath::SQT> bindPose_` to `C_Skeleton` (parallel to
`joints_`), load it from the `.rig` BIND chunk on rig instantiate
(`IRPrefab::Rig::bindPose`), and add `IRPrefab::Skeleton::skinMatrix(joint)`
= `jointWorld × bindInverse`. Unit-test the skinning math on a 3-bone chain.
**Blocked by:** (none).

### 2.2 — render: SYSTEM_UPDATE_JOINT_MATRICES → binding-18 transform buffer [opus] (#1603)
New render system: each frame, per `C_Skeleton`, acquire a contiguous block
of transform slots (one per joint), compute each joint's skin matrix (2.1),
write into the **existing** binding-18 `EntityTransformBuffer`. Runs before
the voxel-position prepass. Reuses the #1396 slot allocator; document the
shared 4096-slot budget. **Blocked by:** 2.1.

### 2.3 — render: per-voxel skinning via bone→slot in the binding-17 seed path [opus] (#1605)
The crux of the unification. When seeding a skeletal voxel set's binding-17
local positions, set each voxel's `.w = slotBase + bone_id` instead of the
entity slot. Unrigged voxels resolve to an identity slot. No stage-1 shader
change. Verify byte-identical output to the CPU path at bind pose.
**Blocked by:** 2.2.

### 2.4 — render: retire/contain binding-21 JointTransformBuffer for the voxel path [sonnet] (#1606)
Once 2.3 proves the unified path, remove the unused binding-21 allocation
from the voxel pipeline (keep only if SDF-shape skinning still needs it; if
so, document it as shapes-only). Update `component_skeleton.hpp`'s binding-21
reference. **Blocked by:** 2.3.

### 2.5 — editor: joint authoring (spawn C_Joint, CHILD_OF parenting, gizmo placement) [opus] (#1604)
In the voxel editor, spawn `C_Joint` entities in the viewport, parent them
via `CHILD_OF` (rig root or parent joint), position with the
already-interactive translate gizmo, and maintain `C_Skeleton.joints_` +
bind pose. **Blocked by:** 2.1.

### 2.6 — editor: skeleton tree panel (joint list, select, rename, reparent) [sonnet] (#1607)
Widget-list panel driven by `C_Skeleton.joints_` (O(joints), no children
scan). Select highlights the joint marker gizmo; rename writes `C_JointName`;
reparent rewrites `CHILD_OF`. **Blocked by:** 2.5.

### 2.7 — editor: bone_id painting (bone selector + paint, tint by bone) [sonnet] (#1608)
Bone selector (palette-like) + paint `bone_id` onto voxels, reusing the
place/erase modal and the existing `C_Voxel.bone_id_`. Tint voxels by bone in
the viewport for authoring feedback. **Blocked by:** 2.5.

### 2.8 — editor: FK pose editing (rotate-gizmo → live deform + bind capture) [opus] (#1610)
Rotate a selected joint's `C_LocalTransform` via the rotate gizmo;
propagation composes the chain; the skinning substrate (2.1–2.3) deforms
voxels live. "Set current pose as bind" captures bind pose.
**Blocked by:** 2.3, 2.5.

### 2.9 — editor: .rig save/load (C_Skeleton + joints + bindPose via BIND/JNTS) [sonnet] (#1609)
Persist/restore the rig through the `.rig` format (#666): joints (JNTS),
bind pose (BIND), and the `C_Skeleton.joints_` ordering.
**Blocked by:** 2.5.

### 2.10 — demos: skeletal demo entities (30-joint snake + desk + 2 more) [sonnet] (#1611)
Author demo-appropriate (not game-specific) assets: the **30-joint snake**
(skeletal acceptance), a **desk** (static authoring showcase), and 2 more
generic demo creatures (e.g. articulated lamp, simple quadruped). Doubles as
#604's F-1.6 sample-entity deliverable. **Blocked by:** 2.8, 2.9.

### 2.11 — test: render-verify + CPU↔GPU skinning consistency for rigged demos [sonnet] (#1612)
render-verify reference shots for the rigged demos; a consistency check that
CPU-computed skin positions match the GPU prepass under identical input.
**Blocked by:** 2.10.

## Acceptance (epic)

Rig the 30-joint snake in the editor; every segment deforms when a joint
rotates. Unrigged voxels (`bone_id → identity`) keep rendering unchanged. The
rig round-trips through `.rig` save/load. render-verify is green.

## Dependency chain

```
#1602 ─┬─ #1603 ── #1605 ─┬─ #1606
       │                  └─ #1610 ─┐
       └─ #1604 ─┬─ #1607           ├─ #1611 ── #1612
                 ├─ #1608           │
                 ├─ #1610 ──────────┤
                 └─ #1609 ──────────┘
```
