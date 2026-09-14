#ifndef IR_VOXEL_FACE_OCCUPANCY_H
#define IR_VOXEL_FACE_OCCUPANCY_H

#include <cstdint>
#include <span>
#include <unordered_set>

#include <irreden/ir_math.hpp>

#include <irreden/voxel/components/component_voxel.hpp>

namespace IRPrefab::Voxel {

namespace detail {

inline bool voxelIsActive(const IRComponents::C_Voxel &voxel) {
    return voxel.color_.alpha_ > 0;
}

// Pack an integer voxel cell into one 64-bit key for hash-set membership.
// 21 bits per axis with a +2^20 bias spans ±2^20 = ±1,048,576 cells — far
// beyond any detached re-voxelize pool's rotated AABB (demo pool ~10 cells)
// — so distinct cells never collide.
inline std::int64_t packCellKey(IRMath::ivec3 cell) {
    constexpr std::int64_t kBias = std::int64_t{1} << 20;
    constexpr std::int64_t kMask = (std::int64_t{1} << 21) - 1;
    return ((static_cast<std::int64_t>(cell.x) + kBias) & kMask) |
           (((static_cast<std::int64_t>(cell.y) + kBias) & kMask) << 21) |
           (((static_cast<std::int64_t>(cell.z) + kBias) & kMask) << 42);
}

} // namespace detail

/// Recompute per-voxel face-occlusion bits for every voxel in a regular
/// `size.x × size.y × size.z` grid stored in `voxels` (row-major via
/// `IRMath::index3DtoIndex1D`). Each active voxel's six face bits are set
/// when the in-grid neighbor at that face is active; inactive voxels are
/// cleared to all-zero (no faces blocked). Existing non-face bits in
/// `flags_` (`kAoContrib`, `kEmissive`) are preserved.
///
/// Neighbor lookup is in-grid only — voxels at the grid boundary do not
/// see voxels in adjacent sets. That matches the acceptance benchmark (a
/// solid 64³ cube is a single set; interior faces all block, surface faces
/// all emit) and avoids a global spatial index.
///
/// Pushed at mutation time per `.claude/rules/cpp-ecs.md` §"No dirty flags":
/// callers invoke this from any set-level mutator that toggles voxel
/// occupancy (`activateAll`, `deactivateAll`, `fillPlane`, `reshape`,
/// dense-data ctor). Per-voxel `activate`/`deactivate` on
/// `C_VoxelSetNew::voxels_[i]` do not auto-update — the caller pays the
/// per-set recompute cost if needed.
inline void recomputeFaceOccupancy(std::span<IRComponents::C_Voxel> voxels, IRMath::ivec3 size) {
    if (size.x <= 0 || size.y <= 0 || size.z <= 0) {
        return;
    }
    const std::size_t requiredCount = static_cast<std::size_t>(size.x) *
                                      static_cast<std::size_t>(size.y) *
                                      static_cast<std::size_t>(size.z);
    if (voxels.size() < requiredCount) {
        return;
    }
    for (int x = 0; x < size.x; ++x) {
        for (int y = 0; y < size.y; ++y) {
            for (int z = 0; z < size.z; ++z) {
                const int idx = IRMath::index3DtoIndex1D(IRMath::ivec3(x, y, z), size);
                auto &voxel = voxels[idx];
                std::uint8_t face = 0u;
                if (detail::voxelIsActive(voxel)) {
                    if (x > 0 &&
                        detail::voxelIsActive(
                            voxels[IRMath::index3DtoIndex1D(IRMath::ivec3(x - 1, y, z), size)]
                        )) {
                        face |= IRComponents::VoxelFlags::kFaceOccludedNegX;
                    }
                    if (x + 1 < size.x &&
                        detail::voxelIsActive(
                            voxels[IRMath::index3DtoIndex1D(IRMath::ivec3(x + 1, y, z), size)]
                        )) {
                        face |= IRComponents::VoxelFlags::kFaceOccludedPosX;
                    }
                    if (y > 0 &&
                        detail::voxelIsActive(
                            voxels[IRMath::index3DtoIndex1D(IRMath::ivec3(x, y - 1, z), size)]
                        )) {
                        face |= IRComponents::VoxelFlags::kFaceOccludedNegY;
                    }
                    if (y + 1 < size.y &&
                        detail::voxelIsActive(
                            voxels[IRMath::index3DtoIndex1D(IRMath::ivec3(x, y + 1, z), size)]
                        )) {
                        face |= IRComponents::VoxelFlags::kFaceOccludedPosY;
                    }
                    if (z > 0 &&
                        detail::voxelIsActive(
                            voxels[IRMath::index3DtoIndex1D(IRMath::ivec3(x, y, z - 1), size)]
                        )) {
                        face |= IRComponents::VoxelFlags::kFaceOccludedNegZ;
                    }
                    if (z + 1 < size.z &&
                        detail::voxelIsActive(
                            voxels[IRMath::index3DtoIndex1D(IRMath::ivec3(x, y, z + 1), size)]
                        )) {
                        face |= IRComponents::VoxelFlags::kFaceOccludedPosZ;
                    }
                }
                voxel.flags_ = static_cast<std::uint8_t>(
                                   voxel.flags_ & ~IRComponents::VoxelFlags::kFaceOccludedMask
                               ) |
                               face;
            }
        }
    }
}

/// Recompute face bits from arbitrary integer destination cells. Several source
/// voxels may share a cell; any active one makes it occupied. Non-face flags are
/// preserved, and inactive voxels have their face bits cleared.
/// The parallel spans map each voxel to its rotated cell. The scratch set retains
/// buckets across calls, but clearing it releases its occupied nodes.
inline void recomputeFaceOccupancyOnCells(
    std::span<const IRMath::ivec3> cells,
    std::span<IRComponents::C_Voxel> voxels,
    int count,
    std::unordered_set<std::int64_t> &occupancy
) {
    const int n = IRMath::min(count, static_cast<int>(IRMath::min(cells.size(), voxels.size())));
    if (n <= 0) {
        return;
    }

    occupancy.clear();
    for (int i = 0; i < n; ++i) {
        if (detail::voxelIsActive(voxels[i])) {
            occupancy.insert(detail::packCellKey(cells[i]));
        }
    }

    const auto occupied = [&occupancy](IRMath::ivec3 cell) {
        return occupancy.find(detail::packCellKey(cell)) != occupancy.end();
    };
    for (int i = 0; i < n; ++i) {
        IRComponents::C_Voxel &voxel = voxels[i];
        std::uint8_t face = 0u;
        if (detail::voxelIsActive(voxel)) {
            const IRMath::ivec3 c = cells[i];
            if (occupied(c + IRMath::ivec3(-1, 0, 0))) {
                face |= IRComponents::VoxelFlags::kFaceOccludedNegX;
            }
            if (occupied(c + IRMath::ivec3(1, 0, 0))) {
                face |= IRComponents::VoxelFlags::kFaceOccludedPosX;
            }
            if (occupied(c + IRMath::ivec3(0, -1, 0))) {
                face |= IRComponents::VoxelFlags::kFaceOccludedNegY;
            }
            if (occupied(c + IRMath::ivec3(0, 1, 0))) {
                face |= IRComponents::VoxelFlags::kFaceOccludedPosY;
            }
            if (occupied(c + IRMath::ivec3(0, 0, -1))) {
                face |= IRComponents::VoxelFlags::kFaceOccludedNegZ;
            }
            if (occupied(c + IRMath::ivec3(0, 0, 1))) {
                face |= IRComponents::VoxelFlags::kFaceOccludedPosZ;
            }
        }
        voxel.flags_ =
            static_cast<std::uint8_t>(voxel.flags_ & ~IRComponents::VoxelFlags::kFaceOccludedMask) |
            face;
    }
}

} // namespace IRPrefab::Voxel

#endif /* IR_VOXEL_FACE_OCCUPANCY_H */
