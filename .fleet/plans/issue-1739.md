# Plan: per-axis store list-walk — split the compacted list 3 ways (#1739)

- **Issue:** #1739   **Model:** opus   **Date:** 2026-06-12 (architect design-unblock of PR #1748)
- **PR:** #1748 (`claude/1739-peraxis-store-list-walk`) — resume from `fleet:design-unblocked`.
- **Context:** Follow-up to #1737. `dispatchPerAxisCanvases`
  (`engine/prefabs/irreden/render/systems/system_voxel_to_trixel.hpp:366`) is the
  ~6 ms rotated-frame overhead at 64³ (IRPerfGrid 13.5 vs 7.5 ms cardinal).

## Decision: implement idea 2 (Metal-safe per-axis compact split). Defer idea 1.

The worker's escalation (PR #1748 `## NEEDS-DESIGN`) is correct and the code
confirms it:

- The per-axis loop (`system_voxel_to_trixel.hpp:405-437`) runs **6 indirect
  dispatches** (3 axes × stage1/stage2), each over the **full** `indirectBuf_`
  compacted list, and the store shader rejects 2/3 of threads via
  `axis = faceId >> 1` (`ir_iso_common.glsl:614`). That is the overhead.
- Each axis already binds **only its own** single distance texture
  (`distances->bindAsImage(1, …)` at lines 412/433) — so the current pass is
  already Metal-safe; the cost is purely the 6× full-list walk + 2/3 rejection.

**Idea 1 (single-pass 3-axis store) stays deferred** — it requires three
foreign-canvas R32I image-atomic-scratch distance images bound in one dispatch,
which is the exact Metal residency/hazard limitation tracked by #1640. With
idea 2 hitting the target, #1640 has no live consumer (see its deferral note);
idea 1 becomes a follow-up only if #1640's backend fix is later pursued.

## Scope (idea 2)

Split the compaction so each axis dispatches over only its ~1/3 of faces:

1. **Compact pass** (`VoxelCompactProgram`, single `CompactedVoxelIndices` +
   one `IndirectDispatchParams` today, `system_voxel_to_trixel.hpp:851-864`):
   append each visible face into **one of three** axis-keyed lists on
   `faceId >> 1`, emitting **three** indirect-dispatch param sets.
2. **Per-axis loop**: dispatch each axis over **its own** list + indirect
   params (still binding only that axis's single distance texture — unchanged,
   Metal-safe). The `faceId >> 1 != axis` early-return in the store shader
   becomes dead (every face in axis list *k* already has `faceId >> 1 == k`) —
   drop it.

Net: 6 full-list walks → 3 dispatch pairs over ~1/3 each ≈ **2 full-walk-
equivalents**, plus the 2/3 thread rejection is gone.

## Implementation checkpoints

- **Confirm the compacted element is face-granular, not voxel-granular.** The
  3-way split is clean only if each list element is a single face (one
  `faceId`). If the compaction is per-voxel (a voxel contributing faces to
  multiple axes), the split must append a voxel into every axis list it has a
  visible face on — verify before wiring the keying. The existing
  `faceId >> 1 != axis` per-thread reject strongly implies face-granular; confirm.
- Three indirect param sets means three atomic append counters in the compact
  shader; zero/clear all three each frame (mirror the existing single-counter
  reset at `system_voxel_to_trixel.hpp:645-656`).
- GLSL + Metal store/compact shaders stay in lockstep (backend-parity).

## Acceptance

- IRPerfGrid 64³ rotated overhead drops from ~6 ms toward the ≤9-10 ms total
  target (issue DoD); cardinal path byte-identical.
- Rendered frame unchanged while rotating (per-axis canvases still feed the
  same T3 composite); render-verify on the rotating shot list shows no drift.
- GLSL and Metal in lockstep.

## Revision history

- 2026-06-12 — architect design-unblock of PR #1748: ratified idea 2,
  deferred idea 1 (gated on #1640). Direction mirrored in the PR
  `## Architect direction` comment; #1640 deferred in parallel.
