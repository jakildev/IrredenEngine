#ifndef ENTITY_MIDI_SEQUENCE_ANIMATED_H
#define ENTITY_MIDI_SEQUENCE_ANIMATED_H

#include <irreden/ir_math.hpp>
#include <irreden/ir_ecs.hpp>

#include <irreden/common/components/component_tags_all.hpp>

using namespace IRMath;
using namespace IRComponents;

namespace IRECS {

// TODO
template <> struct Prefab<PrefabTypes::kMidiSequenceAnimated> {
    static EntityHandle create(

    ) {
        EntityHandle entity{};

        return entity;
    }
};

} // namespace IRECS

#endif /* ENTITY_MIDI_SEQUENCE_ANIMATED_H */
