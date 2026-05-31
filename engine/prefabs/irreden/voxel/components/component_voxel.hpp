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
/// Bits 0..1 keep their pre-B2 positions so .vxs files saved before the
/// face-occlusion bits existed still round-trip semantically — the old
/// `kInteractive` bit (formerly bit 2) was never written by engine code
/// (default ctor set only `kAoContrib`), so reusing bit 2 for the first
/// face bit is safe for legacy saves.
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
///   [8:11] reserved_      reserved for future per-voxel fields
///
/// The compute shaders (`c_voxel_to_trixel_stage_*`) read `color_` from
/// offset 0; `flags_` is consumed by stage 1 to skip occluded faces;
/// `bone_id_` and `layer_id_` ride along for Phase 2 (#605) where stage 1
/// applies the skeletal joint matrix and layer visibility.
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

/// Per-voxel SO(3) visible-triplet packing for `C_Voxel::reserved_` (#1299,
/// PR-A). For a main-canvas entity in `RotationMode::MAIN_CANVAS_SO3`, the GPU
/// voxel-position prepass (`UPDATE_VOXEL_POSITIONS_GPU`) octahedral-snaps the
/// entity orientation and stamps the three camera-visible faces of that snapped
/// orientation into every owned voxel's `reserved_`, so the raster stages render
/// the entity's own visible triplet instead of the shared per-canvas
/// `visibleFaceIds_` UBO. The same 12 B record is already bound at SSBO slot 6
/// and re-uploaded every frame, so the stamp rides along with no new binding or
/// buffer (the architect's "zero new bindings" decision for #1299).
///
/// Layout (low bits of the 32 b `reserved_`; the upper bits stay reserved for a
/// future per-voxel transform slot if the skeletal / horde prepass path needs
/// one):
///   bit 0       kValidBit — this voxel carries a per-entity SO(3) triplet.
///   bits 1..3   slot 0 FaceId (X axis), 0..5
///   bits 4..6   slot 1 FaceId (Y axis), 0..5
///   bits 7..9   slot 2 FaceId (Z axis), 0..5
///
/// The shader-side unpack (`reservedHasSO3` / `unpackReservedFaceId` in
/// `engine/render/src/shaders/ir_iso_common.glsl` + its `.metal` mirror) MUST
/// match these offsets — keep CPU and GPU in lockstep.
namespace VoxelReservedSO3 {
constexpr std::uint32_t kValidBit = 1u << 0;
constexpr int kSlot0Shift = 1; // X axis
constexpr int kSlot1Shift = 4; // Y axis
constexpr int kSlot2Shift = 7; // Z axis
constexpr std::uint32_t kFaceMask = 0x7u; // 3 bits: FaceId 0..5
} // namespace VoxelReservedSO3

// FaceId 0..5 (X_NEG..Z_POS; NONE=6 never appears in a visible triplet) must
// fit the 3-bit per-slot field.
static_assert(
    static_cast<int>(IRMath::FaceId::Z_POS) <= static_cast<int>(VoxelReservedSO3::kFaceMask),
    "FaceId must fit in the 3-bit reserved_ SO(3) triplet slot"
);

/// Pack the three axis-ordered camera-visible faces (exactly as
/// `IRMath::visibleTriplet` returns them) into the `reserved_` SO(3) triplet
/// encoding, with the valid bit set. CPU mirror of the shader-side unpack.
inline std::uint32_t
packVoxelVisibleTriplet(IRMath::FaceId slot0, IRMath::FaceId slot1, IRMath::FaceId slot2) {
    using namespace VoxelReservedSO3;
    return kValidBit |
           ((static_cast<std::uint32_t>(slot0) & kFaceMask) << kSlot0Shift) |
           ((static_cast<std::uint32_t>(slot1) & kFaceMask) << kSlot1Shift) |
           ((static_cast<std::uint32_t>(slot2) & kFaceMask) << kSlot2Shift);
}

} // namespace IRComponents

#endif /* COMPONENT_VOXEL_H */
