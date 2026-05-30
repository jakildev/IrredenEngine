#include "ir_iso_common.metal"
#include "ir_constants.metal"

// Voxel visibility + compaction pass.  Each thread inspects one voxel,
// performs chunk-level visibility, alpha rejection, and iso-bounds culling,
// then atomically appends its index into the compacted indices buffer.  The
// last group to finish (tracked via the completedGroups counter) writes the
// indirect dispatch params used by the stage 1 / stage 2 voxel-to-trixel
// passes.
//
// IndirectDispatchParams memory layout (matches the GLSL std430 SSBO):
//   [0] numGroupsX        (written by last group)
//   [1] numGroupsY        (written by last group)
//   [2] numGroupsZ        (written by last group)
//   [3] visibleCount      (atomic increment, read by stage 1 / stage 2)
//   [4] completedGroups   (atomic increment, last-group barrier)
// All five fields are accessed through a single `device atomic_uint*` so
// that we can mix atomic_fetch_add (3, 4) with atomic_store (0, 1, 2)
// without rebinding the buffer.
//
// memory_order_relaxed is intentional. The happens-before edge that makes
// the indirect-dispatch params visible to the following pipeline stage
// comes from the encoder boundary between this kernel and stage 1 — Metal
// orders device-memory operations between command-buffer encoders for us.
// Within this kernel, threadgroup_barrier(mem_device) handles intra-group
// ordering; the last-group pattern relies on atomic_fetch_add being a
// coherent RMW across threadgroups, not on C++ memory-order semantics.
// Don't "fix" these to acquire/release — it adds barrier cost on Apple
// GPUs without changing observed behavior.

constant uint kSlotNumGroupsX     = 0;
constant uint kSlotNumGroupsY     = 1;
constant uint kSlotNumGroupsZ     = 2;
constant uint kSlotVisibleCount   = 3;
constant uint kSlotCompletedGroups = 4;

// T-287 / #950: this kernel no longer reads the per-voxel color SSBO. The
// per-slot active bit at `activeMask[idx >> 5] & (1 << (idx & 31))` is the
// CPU-pushed mirror of `m_voxelColors[idx].color_.alpha_ != 0`, kept in
// sync at mutation time. Replacing the alpha read with a 1-bit lookup
// halves the per-thread SSBO bandwidth on the dominant hollow / sparse
// cases (the visibility-test path) and lets inactive slots short-circuit
// before the 12 B color load.

kernel void c_voxel_visibility_compact(
    constant FrameDataVoxelToTrixel& frameData [[buffer(7)]],
    device const float4* positions [[buffer(5)]],
    device const uint* activeMask [[buffer(8)]],
    device const uint* chunkVisible [[buffer(24)]],
    device uint* compactedVoxelIndices [[buffer(25)]],
    device atomic_uint* indirectParams [[buffer(26)]],
    uint3 groupId [[threadgroup_position_in_grid]],
    uint3 groupCount [[threadgroups_per_grid]],
    uint3 localId [[thread_position_in_threadgroup]],
    uint localIndex [[thread_index_in_threadgroup]]
) {
    const int cardinalIndex = rasterYawCardinalIndex(frameData.rasterYaw);
    const uint workGroupIndex = groupId.x + groupId.y * groupCount.x;
    const uint idx = workGroupIndex * 64u + localId.x;

    if (idx < uint(frameData.voxelCount)) {
        const uint chunkIdx = idx / uint(VOXEL_CHUNK_SIZE);
        if (chunkVisible[chunkIdx] != 0u) {
            const uint maskWord = activeMask[idx >> 5u];
            if (((maskWord >> (idx & 31u)) & 1u) != 0u) {
                const int3 voxelPosRaw = int3(round(positions[idx].xyz));
                int2 isoPos;
                int cullMargin = 0;
                // Smooth camera Z-yaw (T3 / #1310) — see the GLSL mirror for the
                // rationale: while rotating, project the cull with the same
                // continuous yaw the per-axis scatter raster uses (cardinal snap
                // drops off-center voxels), widened by the sqrt2 face footprint.
                // residual == 0 keeps the byte-identical cardinal-snap path.
                if (frameData.residualYaw != 0.0f) {
                    isoPos = roundHalfUp(
                        pos3DtoPos2DIsoYawed(float3(voxelPosRaw), frameData.visualYaw)
                    );
                    cullMargin = 2;
                } else {
                    int3 voxelPos = voxelPosRaw;
                    if (cardinalIndex != 0) {
                        voxelPos = rotateCardinalZ(voxelPos, cardinalIndex);
                        voxelPos += cardinalLowerCornerShift(cardinalIndex);
                    }
                    isoPos = pos3DtoPos2DIso(voxelPos);
                }
                if (isoPos.x >= frameData.cullIsoMin.x - cullMargin &&
                    isoPos.x <= frameData.cullIsoMax.x + cullMargin &&
                    isoPos.y >= frameData.cullIsoMin.y - cullMargin &&
                    isoPos.y <= frameData.cullIsoMax.y + cullMargin) {
                    const uint slot = atomic_fetch_add_explicit(
                        &indirectParams[kSlotVisibleCount],
                        1u,
                        memory_order_relaxed
                    );
                    compactedVoxelIndices[slot] = idx;
                }
            }
        }
    }

    threadgroup_barrier(mem_flags::mem_device);

    if (localIndex == 0u) {
        const uint finished = atomic_fetch_add_explicit(
            &indirectParams[kSlotCompletedGroups],
            1u,
            memory_order_relaxed
        ) + 1u;
        const uint totalGroups = groupCount.x * groupCount.y;
        if (finished == totalGroups) {
            const uint count = atomic_load_explicit(
                &indirectParams[kSlotVisibleCount],
                memory_order_relaxed
            );
            const uint gx = min(count, 1024u);
            const uint gxClamped = max(gx, 1u);
            const uint gy = max((count + gxClamped - 1u) / gxClamped, 1u);
            const int subdivisions = max(frameData.voxelRenderOptions.y, 1);
            const uint gz = (frameData.voxelRenderOptions.x != 0)
                ? uint(subdivisions * subdivisions)
                : 1u;
            atomic_store_explicit(
                &indirectParams[kSlotNumGroupsX],
                gxClamped,
                memory_order_relaxed
            );
            atomic_store_explicit(
                &indirectParams[kSlotNumGroupsY],
                gy,
                memory_order_relaxed
            );
            atomic_store_explicit(
                &indirectParams[kSlotNumGroupsZ],
                gz,
                memory_order_relaxed
            );
        }
    }
}
