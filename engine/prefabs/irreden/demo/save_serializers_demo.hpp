#ifndef IR_SAVE_SERIALIZERS_DEMO_H
#define IR_SAVE_SERIALIZERS_DEMO_H

/// `SaveSerialize<C>` specialization for `engine/prefabs/irreden/demo/`
/// (#2242). One component, one string. It lives in its own header rather
/// than folding into `common/save_serializers_common.hpp` because a prefab
/// header must not reach across domains (`engine/prefabs/CLAUDE.md`
/// anti-patterns) — the serializer headers mirror the domain split of the
/// components they cover.

#include <irreden/demo/components/component_example.hpp>
#include <irreden/world/save_serialize.hpp>
#include <irreden/world/save_serialize_common.hpp>

#include <irreden/asset/binary_io.hpp>

#include <utility>

namespace IRWorld {

template <> struct SaveSerialize<IRComponents::C_Example> {
    static void write(IRAsset::BinaryWriter &w, const IRComponents::C_Example &value) {
        w.writeString(value.exampleSentence_);
    }

    static IRAsset::Result<IRComponents::C_Example> read(IRAsset::BinaryReader &r) {
        using Res = IRAsset::Result<IRComponents::C_Example>;
        IRComponents::C_Example value{};
        IR_SAVE_READ(value.exampleSentence_, r.readString());
        return Res::success(std::move(value));
    }
};

} // namespace IRWorld

#endif /* IR_SAVE_SERIALIZERS_DEMO_H */
