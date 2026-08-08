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
//     raw-image contract the primary template uses for POD components),
//   - the owning canvas EntityId, for post-load canvas resolution, and
//   - `anchor_` (v2, #2563). For a non-CORNER set the anchor — NOT `boundsMin`
//     — is what reconstructs the local origin on load: that origin is
//     half-integer (always for GROUND, on even axes for CENTER) and the
//     `ivec3` boundsMin cannot represent it. v1 records predate the field and
//     read as CORNER via `SaveMigration<C_VoxelSetNew>` below.
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
#include <irreden/world/save_migration.hpp>
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

        // v2 (#2563): the anchor. It is the ONLY record of a non-CORNER set's
        // local origin that survives the round trip — the `boundsMin` above is
        // an ivec3 and GROUND's z origin is half-integer for every size — so
        // `read` reconstructs the origin from this rather than from boundsMin.
        w.writeU8(static_cast<std::uint8_t>(set.anchor_));

        const std::size_t count = set.recordCount();
        w.writeVarUInt(count);
        // C_Voxel is a fixed 12 B std430 POD; write the raw image per record.
        if (staged) {
            for (const IRComponents::C_Voxel &voxel : set.pendingVoxels_) {
                w.writeBytes(&voxel, sizeof(IRComponents::C_Voxel));
            }
        } else {
            // A GRID-mode set saved mid-rotation has a DERIVED pool span:
            // REBUILD_GRID_VOXELS rearranges `voxels_` into dest-cell order with
            // colors duplicated wherever one source voxel covers several dest
            // cells, and stashes the authored per-voxel records in
            // `rotationSourceVoxels_` (see that system + the component header's
            // `rotationSourceVoxels_` contract). The authored snapshot — not the
            // resampled span — is the pool-independent truth, so a `saveWorld()`
            // that lands while an entity is spinning round-trips the source
            // arrangement instead of the frame's derived colors. Both are
            // dense-box-index ordered, so `read()` (which rebuilds geometry from
            // `boundsMin + index`) restores identically either way. The snapshot
            // is non-empty only while rotating and holds `numVoxels_` records
            // then; fall back to the span if the sizes ever diverge (a rare
            // span-clamp) rather than risk a mixed/short read.
            const bool rotated = set.rotationSourceVoxels_.size() == count;
            for (std::size_t i = 0; i < count; ++i) {
                const IRComponents::C_Voxel &voxel =
                    rotated ? set.rotationSourceVoxels_[i] : set.voxels_[i];
                w.writeBytes(&voxel, sizeof(IRComponents::C_Voxel));
            }
        }
    }

    static IRAsset::Result<IRComponents::C_VoxelSetNew> read(IRAsset::BinaryReader &r) {
        return readVersioned(r, /*hasAnchor=*/true);
    }

    // Shared body for the current layout and every retired one. `hasAnchor`
    // is the only axis the v1 -> v2 bump moved, so the two readers differ by
    // one field rather than by a copied function — a direct per-version
    // reader per `save_migration.hpp`'s contract, not a v1 -> v2 chain.
    static IRAsset::Result<IRComponents::C_VoxelSetNew>
    readVersioned(IRAsset::BinaryReader &r, bool hasAnchor) {
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
        IRComponents::EntityAnchor anchor = IRComponents::EntityAnchor::CORNER;
        if (hasAnchor) {
            IRAsset::Result<std::uint8_t> rawAnchor = r.readU8();
            if (!rawAnchor.ok()) {
                return Res::error(rawAnchor.status_.code_, std::move(rawAnchor.status_.message_));
            }
            // Range-check before the cast — a corrupt or hand-edited byte must
            // not become an out-of-range enum that `anchorOffset` then reads as
            // CORNER by falling through its switch (silently misplacing the set
            // rather than failing the load).
            if (rawAnchor.value_ > static_cast<std::uint8_t>(IRComponents::EntityAnchor::kLast)) {
                return Res::error(
                    IRAsset::BinaryIOError::UnknownTag,
                    "C_VoxelSetNew: EntityAnchor value out of range"
                );
            }
            anchor = static_cast<IRComponents::EntityAnchor>(rawAnchor.value_);
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
                static_cast<IREntity::EntityId>(canvas.value_),
                anchor
            }
        );
    }
};

// v1 predates the anchor byte (#2563). Every v1 set was authored through the
// bool ctor, so its origin is exactly the `boundsMin` the record already
// carries and CORNER is the faithful reading — CENTER sets round-trip through
// boundsMin as they always did, with the pre-existing even-size lossiness the
// v1 format had and this migrator deliberately reproduces rather than
// silently "fixing" on load.
template <> struct SaveMigration<IRComponents::C_VoxelSetNew> {
    static std::vector<std::pair<std::uint32_t, ColumnMigratorFn<IRComponents::C_VoxelSetNew>>>
    migrators() {
        return {
            {1u, [](IRAsset::BinaryReader &r) -> IRAsset::Result<IRComponents::C_VoxelSetNew> {
                 return SaveSerialize<IRComponents::C_VoxelSetNew>::readVersioned(
                     r,
                     /*hasAnchor=*/false
                 );
             }},
        };
    }
};

} // namespace IRWorld

#endif /* IR_VOXEL_SET_SERIALIZE_H */
