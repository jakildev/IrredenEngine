#version 450 core

layout(local_size_x = 64, local_size_y = 1, local_size_z = 1) in;

#include "ir_iso_common.glsl"
#include "ir_constants.glsl"

layout(std140, binding = 7) uniform FrameDataVoxelToTrixel {
    uniform vec2 frameCanvasOffset;
    uniform ivec2 trixelCanvasOffsetZ1;
    uniform ivec2 voxelRenderOptions;
    uniform ivec2 voxelDispatchGrid;
    uniform int voxelCount;
    // Per-axis store list-walk split (#1739): the per-region element capacity
    // (the perAxisRoute_ slot, dead during the compact). 0 = single full list
    // (byte-identical). Non-zero = split mode: append each visible voxel into the
    // axis regions it has an exposed face on; the value is the stride between
    // those three regions in compactedVoxelIndices.
    uniform int perAxisSplitStride;
    uniform ivec2 canvasSizePixels;
    uniform ivec2 cullIsoMin;
    uniform ivec2 cullIsoMax;
    uniform float visualYaw;
    uniform float rasterYaw;
    uniform float residualYaw;
    uniform float _yawPadding;
};

layout(std430, binding = 5) readonly buffer PositionBuffer {
    vec4 positions[];
};

// Per-voxel material/flag/bone word. Read ONLY in per-axis split mode
// (perAxisSplitStride != 0): the face-occlusion flags byte (bits [2..7] of
// byte 5 = `materialFlagBone >> 8`) routes each visible voxel into the axis
// regions it has an exposed face on. Layout must match C_Voxel and the `Voxel`
// struct in c_voxel_to_trixel_stage_1.glsl (12 B). Binding 6 (VoxelColorBuffer)
// is bound every frame, so this is safe to declare unconditionally.
struct Voxel {
    uint colorPacked;
    uint materialFlagBone;
    uint reserved;
};
layout(std430, binding = 6) readonly buffer ColorBuffer {
    Voxel voxels[];
};

// Per-slot active bitmask uploaded from `C_VoxelPool::m_activeMask`:
// one uint32 per `kVoxelActiveMaskBits` (= 32) voxel slots; bit i mirrors
// `m_voxelColors[i].color_.alpha_ != 0` at frame-upload time. The previous
// path read `voxels[idx].colorPacked` purely to test alpha; this SSBO
// replaces that read with a 1-bit lookup so inactive slots short-circuit
// before touching the wider color SSBO. T-287 / #950.
layout(std430, binding = 8) readonly buffer VoxelActiveMaskBuffer {
    uint activeMask[];
};

layout(std430, binding = 24) readonly buffer ChunkVisibility {
    uint chunkVisible[];
};

layout(std430, binding = 25) writeonly buffer CompactedIndices {
    uint compactedVoxelIndices[];
};

// Indirect dispatch params, declared flat so one kernel writes either layout:
//   single-canvas mode  -> the 32-byte IndirectDispatchParams buffer (struct 0)
//   per-axis split mode  -> PerAxisIndirectDispatchParams: three structs spaced
//                           kPerAxisIndirectStrideUints apart (256 B), so the CPU
//                           can bindRange each at an SSBO-offset-aligned boundary.
// Per-struct slots: 0 = numGroupsX, 1 = numGroupsY, 2 = numGroupsZ,
//                   3 = visibleCount (atomic append counter), 4 = completedGroups.
// Slot 4 of struct 0 (params[4]) is the shared cross-group completion counter in
// BOTH modes.
const uint kPerAxisIndirectStrideUints = 64u;  // 256 B / 4 — mirrors C++ kPerAxisSsboAlignBytes
layout(std430, binding = 26) buffer IndirectDispatchParamsBuf {
    uint params[];
};

// Compute the indirect dispatch grid for the struct at `base` from its
// visibleCount slot (matches the single-canvas numGroups math exactly). The
// count is read atomically so the last group sees every other group's appends.
void writeDispatchDims(uint base) {
    uint count = atomicAdd(params[base + 3u], 0u);
    uint gx = max(min(count, 1024u), 1u);
    params[base + 0u] = gx;
    params[base + 1u] = max((count + gx - 1u) / gx, 1u);
    int subdivisions = max(voxelRenderOptions.y, 1);
    params[base + 2u] = (voxelRenderOptions.x != 0) ? uint(subdivisions * subdivisions) : 1u;
}

void main() {
    const int cardinalIndex = rasterYawCardinalIndex(rasterYaw);
    uint workGroupIndex = gl_WorkGroupID.x + gl_WorkGroupID.y * gl_NumWorkGroups.x;
    uint idx = workGroupIndex * 64u + gl_LocalInvocationID.x;

    if (idx < uint(voxelCount)) {
        uint chunkIdx = idx / uint(VOXEL_CHUNK_SIZE);
        if (chunkVisible[chunkIdx] != 0u) {
            // 32-bit word stride matches `kVoxelActiveMaskBits` on the CPU side.
            uint maskWord = activeMask[idx >> 5u];
            if (((maskWord >> (idx & 31u)) & 1u) != 0u) {
                ivec3 voxelPosRaw = ivec3(round(positions[idx].xyz));
                ivec2 isoPos;
                int cullMargin = 0;
                // Smooth camera Z-yaw (T3 / #1310): while the per-axis canvases
                // are active (residual yaw != 0) the framebuffer scatter
                // rasterizes each voxel at its CONTINUOUS yawed iso position, so
                // the cull must project the same way — the cardinal-snapped iso
                // disagrees by the residual and drops off-center voxels (the
                // "missing objects during rotation" symptom). Widen by the
                // deformed-face sqrt2 footprint (~2 iso px) so a voxel whose
                // center is just off-screen but whose face reaches on-screen
                // still rasterizes. residual == 0 keeps the byte-identical
                // cardinal-snap path.
                if (residualYaw != 0.0) {
                    isoPos = roundHalfUp(pos3DtoPos2DIsoYawed(vec3(voxelPosRaw), visualYaw));
                    cullMargin = 2;
                } else {
                    ivec3 voxelPos = voxelPosRaw;
                    if (cardinalIndex != 0) {
                        voxelPos = rotateCardinalZ(voxelPos, cardinalIndex);
                        voxelPos += cardinalLowerCornerShift(cardinalIndex);
                    }
                    isoPos = pos3DtoPos2DIso(voxelPos);
                }
                if (isoPos.x >= cullIsoMin.x - cullMargin &&
                    isoPos.x <= cullIsoMax.x + cullMargin &&
                    isoPos.y >= cullIsoMin.y - cullMargin &&
                    isoPos.y <= cullIsoMax.y + cullMargin) {
                    if (perAxisSplitStride == 0) {
                        // Single full list (byte-identical to master).
                        uint slot = atomicAdd(params[3], 1u);
                        compactedVoxelIndices[slot] = idx;
                    } else {
                        // Per-axis split (#1739): append this voxel into each axis
                        // region whose axis it has an exposed face on. The store
                        // shader re-checks the precise visible-face exposure per
                        // axis, so an over-inclusive entry here is harmless; a
                        // fully-interior voxel (every face occluded) lands in no
                        // region, which is tighter than master's full-list walk.
                        uint flagsByte = (voxels[idx].materialFlagBone >> 8u) & 0xFFu;
                        uint stride = uint(perAxisSplitStride);
                        for (int axis = 0; axis < 3; ++axis) {
                            if (faceIsExposed(flagsByte, 2 * axis) ||
                                faceIsExposed(flagsByte, 2 * axis + 1)) {
                                uint base = uint(axis) * kPerAxisIndirectStrideUints;
                                uint slot = atomicAdd(params[base + 3u], 1u);
                                compactedVoxelIndices[uint(axis) * stride + slot] = idx;
                            }
                        }
                    }
                }
            }
        }
    }

    barrier();
    memoryBarrierBuffer();

    if (gl_LocalInvocationIndex == 0u) {
        // params[4] (struct 0's completedGroups slot) is the shared cross-group
        // completion counter in both modes.
        uint finished = atomicAdd(params[4], 1u) + 1u;
        uint totalGroups = gl_NumWorkGroups.x * gl_NumWorkGroups.y;
        if (finished == totalGroups) {
            if (perAxisSplitStride == 0) {
                writeDispatchDims(0u);
            } else {
                for (int axis = 0; axis < 3; ++axis) {
                    writeDispatchDims(uint(axis) * kPerAxisIndirectStrideUints);
                }
            }
        }
    }
}
