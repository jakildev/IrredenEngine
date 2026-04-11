#ifndef ENTITY_MIDI_SEQUENCE_ANIMATED_H
#define ENTITY_MIDI_SEQUENCE_ANIMATED_H

#include <irreden/ir_math.hpp>
#include <irreden/ir_ecs.hpp>

#include <irreden/common/components/component_tags_all.hpp>

using namespace IRMath;
using namespace IRComponents;

namespace IRECS {

// STATUS: stub. PrefabTypes::kMidiSequenceAnimated is declared but no
// creation instantiates it yet. Intended to wrap a MIDI sequence entity
// with C_ProceduralAnimation parameters so the playhead can drive
// per-note animation.
template <> struct Prefab<PrefabTypes::kMidiSequenceAnimated> {
    static EntityHandle create(

    ) {
        EntityHandle entity{};

        return entity;
    }
};

} // namespace IRECS

#endif /* ENTITY_MIDI_SEQUENCE_ANIMATED_H */
