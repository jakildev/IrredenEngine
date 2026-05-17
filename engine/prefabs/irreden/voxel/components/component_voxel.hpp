#ifndef COMPONENT_VOXEL_H
#define COMPONENT_VOXEL_H

#include <cstddef>
#include <cstdint>

#include <irreden/ir_math.hpp>

using IRMath::Color;

namespace IRComponents {

/// Per-voxel flag bits. Bit-packed into `C_Voxel::flags_`.
///
/// Reserved bits 3-7 are available for future flags without re-versioning
/// the 12 B record (the trailing pad bytes also leave room for new fields).
/// Match the GPU-side constants if shader code starts to consume these.
namespace VoxelFlags {
constexpr std::uint8_t kAoContrib = 1u << 0;
constexpr std::uint8_t kEmissive = 1u << 1;
constexpr std::uint8_t kInteractive = 1u << 2;
} // namespace VoxelFlags

/// Per-voxel record. 12 B std430 layout — matches the v2 entity-editor record
/// budget (`docs/design/entity-editor-epic.md` "Per-voxel record extension").
///
/// Layout (one record per voxel-pool slot, uploaded to SSBO @ slot 6):
///   [0:3]  color_         packed RGBA8
///   [4]    material_id_   material registry index (0 = default)
///   [5]    flags_         bit-packed VoxelFlags bits
///   [6]    bone_id_       skeletal-rig joint index (0 = identity)
///   [7]    layer_id_      editor layer membership (0 = default layer); keeps
///                         the trailing uint32 4-byte aligned
///   [8:11] reserved_      reserved for future per-voxel fields
///
/// Phase 1 (this PR) widens the in-memory + SSBO layout only. The compute
/// shaders (`c_voxel_to_trixel_stage_*`) read `color_` from offset 0 just
/// like before — the new fields ride along for Phase 2 (#605) where stage 1
/// multiplies the voxel position by `bone_matrix[bone_id]`.
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
