#ifndef IR_SAVE_SERIALIZERS_VOXEL_H
#define IR_SAVE_SERIALIZERS_VOXEL_H

/// `SaveSerialize<C>` specializations for the heap-owning rig components in
/// `engine/prefabs/irreden/voxel/` (#2242). `C_VoxelSetNew`'s serializer is
/// the separate, older `voxel_set_serialize.hpp` (persist P6) — it stays on
/// its own because its read path has a pool-interaction contract these do
/// not.
///
/// Opt-in serializer header: include it wherever a registry registers these
/// components; never pulled by the component headers themselves.

#include <irreden/voxel/components/component_bind_points.hpp>
#include <irreden/voxel/components/component_joint_name.hpp>
#include <irreden/voxel/components/component_skeleton.hpp>
#include <irreden/world/save_serialize.hpp>
#include <irreden/world/save_serialize_common.hpp>

#include <irreden/asset/binary_io.hpp>

#include <utility>

namespace IRWorld {

template <> struct SaveSerialize<IRComponents::C_JointName> {
    static void write(IRAsset::BinaryWriter &w, const IRComponents::C_JointName &value) {
        w.writeString(value.name_);
    }

    static IRAsset::Result<IRComponents::C_JointName> read(IRAsset::BinaryReader &r) {
        using Res = IRAsset::Result<IRComponents::C_JointName>;
        IRComponents::C_JointName value{};
        IR_SAVE_READ(value.name_, r.readString());
        return Res::success(std::move(value));
    }
};

/// `joints_` holds joint `EntityId`s and round-trips as plain values: the
/// snapshot restores entity ids **exact** (they never recycle — see
/// `world_snapshot.hpp`), which is what makes a stored id meaningful across a
/// save at all. The index of an entry is the bone_id baked into
/// `C_Voxel.bone_id_`, so slot order is load-bearing and a severance hole
/// (`kNullEntity`) must survive the round trip rather than being compacted
/// away — writing the vector verbatim is what preserves that.
///
/// `bindPose_` is parallel to `joints_` but is NOT required to be the same
/// length (a rig with no bind pose leaves it empty), so the two are written
/// as independent counted vectors rather than one interleaved run.
template <> struct SaveSerialize<IRComponents::C_Skeleton> {
    static void write(IRAsset::BinaryWriter &w, const IRComponents::C_Skeleton &value) {
        detail::writeTrivialVector(w, value.joints_);
        detail::writeTrivialVector(w, value.bindPose_);
    }

    static IRAsset::Result<IRComponents::C_Skeleton> read(IRAsset::BinaryReader &r) {
        using Res = IRAsset::Result<IRComponents::C_Skeleton>;
        IRComponents::C_Skeleton value{};
        IR_SAVE_READ_STATUS(detail::readTrivialVector(r, value.joints_));
        IR_SAVE_READ_STATUS(detail::readTrivialVector(r, value.bindPose_));
        return Res::success(std::move(value));
    }
};

/// `points_` is an `unordered_map`, whose iteration order is not a contract —
/// writing it in hash order would make two saves of the same world differ
/// byte-for-byte. `writeSortedStringMap` emits ascending key order so the
/// double-save byte-identity requirement (world-snapshot criterion 6) holds.
template <> struct SaveSerialize<IRComponents::C_BindPoints> {
    static void write(IRAsset::BinaryWriter &w, const IRComponents::C_BindPoints &value) {
        detail::writeSortedStringMap(w, value.points_);
    }

    static IRAsset::Result<IRComponents::C_BindPoints> read(IRAsset::BinaryReader &r) {
        using Res = IRAsset::Result<IRComponents::C_BindPoints>;
        IRComponents::C_BindPoints value{};
        IR_SAVE_READ_STATUS(detail::readStringMap(r, value.points_));
        return Res::success(std::move(value));
    }
};

} // namespace IRWorld

#endif /* IR_SAVE_SERIALIZERS_VOXEL_H */
