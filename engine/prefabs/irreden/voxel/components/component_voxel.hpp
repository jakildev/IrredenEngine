#ifndef COMPONENT_VOXEL_H
#define COMPONENT_VOXEL_H

#include <cstddef>
#include <cstdint>

#include <irreden/ir_math.hpp>

using IRMath::Color;

namespace IRComponents {

/// Per-voxel flag bits. Bit-packed into `C_Voxel::flags_`.
///
/// Layout:
///   bit 0     — `kAoContrib`.
///   bit 1     — `kEmissive`.
///   bits 2..7 — face-occlusion bits (`kFaceOccluded*`). Each bit is set
///               when the neighbor on that face exists, so the renderer
///               can skip emitting that face. Default = 0 (all faces
///               visible). Maintained at edit time by
///               `IRPrefab::Voxel::recomputeFaceOccupancy` (see
///               `voxel/face_occupancy.hpp`).
///
/// The byte is stored verbatim in `.vxs` records, so the bit positions are
/// part of the on-disk format: bits 0..1 are fixed, and a record that
/// carries zeros in bits 2..7 decodes as "no face occluded", which is the
/// reading every save without face bits needs.
///
/// The GPU mirror reads the same byte at offset 5 of the 12 B record
/// (`(materialFlagBone >> 8) & 0xFFu` in stage 1). Face-bit indices
/// match `kFace*` directions in `ir_iso_common.glsl`.
namespace VoxelFlags {
constexpr std::uint8_t kAoContrib = 1u << 0;
constexpr std::uint8_t kEmissive = 1u << 1;
constexpr std::uint8_t kFaceOccludedNegX = 1u << 2;
constexpr std::uint8_t kFaceOccludedPosX = 1u << 3;
constexpr std::uint8_t kFaceOccludedNegY = 1u << 4;
constexpr std::uint8_t kFaceOccludedPosY = 1u << 5;
constexpr std::uint8_t kFaceOccludedNegZ = 1u << 6;
constexpr std::uint8_t kFaceOccludedPosZ = 1u << 7;
constexpr std::uint8_t kFaceOccludedMask = kFaceOccludedNegX | kFaceOccludedPosX |
                                           kFaceOccludedNegY | kFaceOccludedPosY |
                                           kFaceOccludedNegZ | kFaceOccludedPosZ;
} // namespace VoxelFlags

/// Bit layout of the trailing 32-bit `reserved_` word. bits[1:0] carry the
/// per-trixel priority tier. Bit 2 (`kRotatedEmit`) marks a voxel
/// whose render-frame CELL positions are a ROTATED re-voxelization
/// (`REBUILD_GRID_VOXELS` for GRID-mode sets; the detached path uses the
/// `visibleFaceIds.w` re-voxelize uniform instead). The voxel→trixel raster
/// reads it to enable the silhouette-riser face selection — emit the exposed
/// opposite-polarity face that the convex visible-triplet drops on a
/// rotated staircase's grazing edge. Non-rotated voxels never set it, so the
/// strict-triplet fast path (and its byte-identity) is preserved.
/// Bit 3 (`kFogBody`) marks a fog BODY: a set governed by one entity-anchor
/// verdict, so its live voxels bypass per-column fog rejection and stage 2
/// folds the bit into entity-id bit 28. Bits[11:4] carry that verdict as an
/// 8-bit reveal factor (`C_FogRevealed::revealFactor_ * 255`, round half up)
/// which stage 2 folds into entity-id bits 27:20; FOG_TO_TRIXEL paints every
/// BODY pixel at that one factor. An EXEMPT set pins the factor at 255.
namespace VoxelReserved {
constexpr std::uint32_t kPriorityMask = 0x3u;   // bits[1:0]
constexpr std::uint32_t kRotatedEmit = 1u << 2; // bit 2
constexpr std::uint32_t kFogBody = 1u << 3;     // bit 3
constexpr std::uint32_t kFogWholeBodyExempt = kFogBody;
constexpr int kFogBodyFactorShift = 4;
constexpr std::uint32_t kFogBodyFactorMask = 0xFFu << kFogBodyFactorShift; // bits[11:4]
constexpr std::uint32_t kFogCarrierMask = kFogBody | kFogBodyFactorMask;
} // namespace VoxelReserved

/// Per-voxel record. 12 B std430 layout — matches the v2 entity-editor record
/// budget (`docs/design/entity-editor-epic.md` "Per-voxel record extension").
///
/// Layout (one record per voxel-pool slot, uploaded to SSBO @ slot 6):
///   [0:3]  color_         packed RGBA8
///   [4]    material_id_   material registry index (0 = default)
///   [5]    flags_         bit-packed VoxelFlags bits (faces + AO + emissive)
///   [6]    bone_id_       skeletal-rig joint index (0 = identity)
///   [7]    layer_id_      editor layer membership (0 = default layer); keeps
///                         the trailing uint32 4-byte aligned
///   [8:11] reserved_      bits[1:0] = per-trixel priority tier carrier;
///                         bit 2 = kRotatedEmit (rotated re-voxelize, see
///                         VoxelReserved); bit 3 = kFogBody; bits[11:4] =
///                         the BODY reveal factor; bits[31:12] reserved
///
/// The compute shaders (`c_voxel_to_trixel_stage_*`) read `color_` from
/// offset 0; `flags_` is consumed by stage 1 to skip occluded faces;
/// `bone_id_` and `layer_id_` ride along for stage 1, which applies the
/// skeletal joint matrix and layer visibility.
struct C_Voxel {
    IRMath::Color color_;
    std::uint8_t material_id_;
    std::uint8_t flags_;
    std::uint8_t bone_id_;
    std::uint8_t layer_id_;
    std::uint32_t reserved_;

    C_Voxel(
        IRMath::Color color,
        std::uint8_t material_id = 0,
        std::uint8_t flags = VoxelFlags::kAoContrib,
        std::uint8_t bone_id = 0,
        std::uint8_t layer_id = 0
    )
        : color_{color}
        , material_id_{material_id}
        , flags_{flags}
        , bone_id_{bone_id}
        , layer_id_{layer_id}
        , reserved_{0} {}

    C_Voxel()
        : C_Voxel{IRMath::Color{0, 0, 0, 255}} {}

    std::uint8_t materialId() const {
        return material_id_;
    }
    std::uint8_t flags() const {
        return flags_;
    }
    std::uint8_t boneId() const {
        return bone_id_;
    }
    std::uint8_t layerId() const {
        return layer_id_;
    }

    void activate() {
        color_.alpha_ = 255;
    }

    void deactivate() {
        color_.alpha_ = 0;
    }
};

static_assert(sizeof(C_Voxel) == 12, "C_Voxel must be 12 bytes to match GPU std430 layout");
static_assert(alignof(C_Voxel) == 4, "C_Voxel must be 4-byte aligned for std430 stride");
static_assert(offsetof(C_Voxel, color_) == 0, "C_Voxel::color_ must be at offset 0");
static_assert(offsetof(C_Voxel, material_id_) == 4, "C_Voxel::material_id_ must be at offset 4");
static_assert(offsetof(C_Voxel, flags_) == 5, "C_Voxel::flags_ must be at offset 5");
static_assert(offsetof(C_Voxel, bone_id_) == 6, "C_Voxel::bone_id_ must be at offset 6");
static_assert(offsetof(C_Voxel, layer_id_) == 7, "C_Voxel::layer_id_ must be at offset 7");
static_assert(offsetof(C_Voxel, reserved_) == 8, "C_Voxel::reserved_ must be at offset 8");

} // namespace IRComponents

#endif /* COMPONENT_VOXEL_H */
