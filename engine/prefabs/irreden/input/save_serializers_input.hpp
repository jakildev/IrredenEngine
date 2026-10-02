#ifndef IR_SAVE_SERIALIZERS_INPUT_H
#define IR_SAVE_SERIALIZERS_INPUT_H

#include <irreden/input/components/component_hitbox_2d.hpp>
#include <irreden/world/save_migration.hpp>
#include <irreden/world/save_serialize.hpp>
#include <irreden/world/save_serialize_common.hpp>

#include <irreden/asset/binary_io.hpp>
#include <irreden/asset/math_binary_io.hpp>

#include <cstdint>
#include <type_traits>
#include <utility>
#include <vector>

namespace IRWorld {

/// Hitbox configuration survives a world snapshot; per-frame hover, screen
/// placement, and resolved depth are rebuilt by the input/render pipelines.
template <> struct SaveSerialize<IRComponents::C_HitBox2D> {
    static void write(IRAsset::BinaryWriter &writer, const IRComponents::C_HitBox2D &value) {
        IRMath::BinaryIO::writeVec2(writer, value.halfExtent_);
        writer.writeF32(value.padding_);
        writer.writeU8(value.enabled_ ? 1 : 0);
        writer.writeI32(value.pickPriority_);
    }

    static IRAsset::Result<IRComponents::C_HitBox2D> read(IRAsset::BinaryReader &reader) {
        using Res = IRAsset::Result<IRComponents::C_HitBox2D>;
        IRComponents::C_HitBox2D value{};
        IR_SAVE_READ(value.halfExtent_, IRMath::BinaryIO::readVec2(reader));
        IR_SAVE_READ(value.padding_, reader.readF32());
        IR_SAVE_READ_BOOL(value.enabled_, reader.readU8());
        IR_SAVE_READ(value.pickPriority_, reader.readI32());
        return Res::success(std::move(value));
    }
};

template <> struct SaveMigration<IRComponents::C_HitBox2D> {
    static std::vector<std::pair<std::uint32_t, ColumnMigratorFn<IRComponents::C_HitBox2D>>>
    migrators() {
        return {
            {1u, [](IRAsset::BinaryReader &reader) -> IRAsset::Result<IRComponents::C_HitBox2D> {
                 struct LegacyHitBox2D {
                     IRMath::vec2 halfExtent_;
                     bool hovered_;
                 };
                 static_assert(std::is_trivially_copyable_v<LegacyHitBox2D>);
                 static_assert(sizeof(LegacyHitBox2D) == 12);

                 LegacyHitBox2D legacy{};
                 IRAsset::BinaryStatus status = reader.readBytes(&legacy, sizeof(legacy));
                 if (!status.ok()) {
                     return IRAsset::Result<IRComponents::C_HitBox2D>::error(
                         status.code_,
                         std::move(status.message_)
                     );
                 }

                 IRComponents::C_HitBox2D value{legacy.halfExtent_};
                 value.hovered_ = legacy.hovered_;
                 return IRAsset::Result<IRComponents::C_HitBox2D>::success(std::move(value));
             }},
        };
    }
};

} // namespace IRWorld

#endif /* IR_SAVE_SERIALIZERS_INPUT_H */
