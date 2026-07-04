#ifndef IR_VOXEL_SET_SERIALIZE_H
#define IR_VOXEL_SET_SERIALIZE_H

// `SaveSerialize<C_VoxelSetNew>` for the ECS world snapshot — persist P6 / W-10
// (#2217, epic #667). C_VoxelSetNew owns `std::span` views into a process-local
// voxel pool plus (in staged mode) a `std::vector<C_Voxel>`, so it is NOT
// trivially copyable and the primary `SaveSerialize` template (a raw byte
// image) cannot handle it — a memcpy would persist dangling pool spans.
//
// The canonical, pool-independent content of a set is:
//   - `size_`     — the dense box extent (voxel count = product of the axes),
//   - `boundsMin` — the local origin of voxel index (0,0,0); every dense box's
//                   local positions are `boundsMin + index`, so this plus the
//                   colors fully reconstruct the geometry,
//   - the per-voxel `C_Voxel` records (a fixed 12 B std430 POD — the same
//     raw-image contract the primary template uses for POD components), and
//   - the owning canvas EntityId, for post-load canvas resolution.
//
// `read` reconstructs the set in STAGED mode (`numVoxels_ == 0`,
// `pendingVoxels_` populated) via the zero-pool `C_VoxelSetNew::StagedInit`
// ctor, so it performs NO pool allocation — safe to call in the loader's
// mutation-free validate pass (`world_snapshot.cpp` phase 2b dry-runs `read`).
// The post-load `C_VoxelSetNew::attachToCanvas` seed pass then moves the staged
// data into a live pool span. GPU-resident state (pool handles, the GPU
// transform slot) is deliberately NOT persisted — it re-derives on load
// (attach seed + the per-frame render pipeline).
//
// This is an opt-in serializer header: include it wherever a snapshot registry
// registers `C_VoxelSetNew` (e.g. the persist_roundtrip demo). It is not pulled
// into the widely-included component header, so the render-neutral layering of
// `component_voxel_set.hpp` is preserved.

#include <irreden/voxel/components/component_voxel.hpp>
#include <irreden/voxel/components/component_voxel_set.hpp>
#include <irreden/world/save_serialize.hpp>

#include <irreden/asset/binary_io.hpp>

#include <cstdint>
#include <utility>
#include <vector>

namespace IRWorld {

template <> struct SaveSerialize<IRComponents::C_VoxelSetNew> {
    static void write(IRAsset::BinaryWriter &w, const IRComponents::C_VoxelSetNew &set) {
        const IRMath::ivec3 size = set.size_;
        w.writeI32(size.x);
        w.writeI32(size.y);
        w.writeI32(size.z);

        // boundsMin: the local origin of voxel index (0,0,0). A staged set keeps
        // it verbatim; a pool-resident set recovers it from its seeded local
        // position (`positions_[0].pos_ == boundsMin` for a dense box). Integer
        // origins — every dense-authored / size-ctor set — round-trip exactly.
        const bool staged = !set.pendingVoxels_.empty();
        IRMath::ivec3 boundsMin = set.pendingBoundsMin_;
        if (!staged && set.numVoxels_ > 0) {
            const IRMath::vec3 origin = set.positions_[0].pos_;
            boundsMin = IRMath::ivec3(
                IRMath::roundHalfUp(origin.x),
                IRMath::roundHalfUp(origin.y),
                IRMath::roundHalfUp(origin.z)
            );
        }
        w.writeI32(boundsMin.x);
        w.writeI32(boundsMin.y);
        w.writeI32(boundsMin.z);

        w.writeU64(static_cast<std::uint64_t>(set.canvasEntity_));

        const std::size_t count = set.recordCount();
        w.writeVarUInt(count);
        // C_Voxel is a fixed 12 B std430 POD; write the raw image per record.
        if (staged) {
            for (const IRComponents::C_Voxel &voxel : set.pendingVoxels_) {
                w.writeBytes(&voxel, sizeof(IRComponents::C_Voxel));
            }
        } else {
            for (std::size_t i = 0; i < count; ++i) {
                w.writeBytes(&set.voxels_[i], sizeof(IRComponents::C_Voxel));
            }
        }
    }

    static IRAsset::Result<IRComponents::C_VoxelSetNew> read(IRAsset::BinaryReader &r) {
        using Res = IRAsset::Result<IRComponents::C_VoxelSetNew>;

        IRAsset::Result<std::int32_t> sx = r.readI32();
        if (!sx.ok()) {
            return Res::error(sx.status_.code_, std::move(sx.status_.message_));
        }
        IRAsset::Result<std::int32_t> sy = r.readI32();
        if (!sy.ok()) {
            return Res::error(sy.status_.code_, std::move(sy.status_.message_));
        }
        IRAsset::Result<std::int32_t> sz = r.readI32();
        if (!sz.ok()) {
            return Res::error(sz.status_.code_, std::move(sz.status_.message_));
        }
        IRAsset::Result<std::int32_t> bx = r.readI32();
        if (!bx.ok()) {
            return Res::error(bx.status_.code_, std::move(bx.status_.message_));
        }
        IRAsset::Result<std::int32_t> by = r.readI32();
        if (!by.ok()) {
            return Res::error(by.status_.code_, std::move(by.status_.message_));
        }
        IRAsset::Result<std::int32_t> bz = r.readI32();
        if (!bz.ok()) {
            return Res::error(bz.status_.code_, std::move(bz.status_.message_));
        }
        IRAsset::Result<std::uint64_t> canvas = r.readU64();
        if (!canvas.ok()) {
            return Res::error(canvas.status_.code_, std::move(canvas.status_.message_));
        }
        IRAsset::Result<std::uint64_t> count = r.readVarUInt();
        if (!count.ok()) {
            return Res::error(count.status_.code_, std::move(count.status_.message_));
        }

        std::vector<IRComponents::C_Voxel> voxels(count.value_);
        for (std::uint64_t i = 0; i < count.value_; ++i) {
            IRAsset::BinaryStatus status = r.readBytes(&voxels[i], sizeof(IRComponents::C_Voxel));
            if (!status.ok()) {
                return Res::error(status.code_, std::move(status.message_));
            }
        }

        return Res::success(
            IRComponents::C_VoxelSetNew{
                IRComponents::C_VoxelSetNew::StagedInit{},
                IRMath::ivec3(sx.value_, sy.value_, sz.value_),
                IRMath::ivec3(bx.value_, by.value_, bz.value_),
                std::move(voxels),
                static_cast<IREntity::EntityId>(canvas.value_)
            }
        );
    }
};

} // namespace IRWorld

#endif /* IR_VOXEL_SET_SERIALIZE_H */
