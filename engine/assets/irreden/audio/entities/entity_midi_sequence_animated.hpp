/*
 * Project: Irreden Engine
 * File: entity_midi_sequence_animated.hpp
 * Author: Evin Killian jakildev@gmail.com
 * Created Date: October 2023
 * -----
 * Modified By: <your_name> <Month> <YYYY>
 */

#ifndef ENTITY_MIDI_SEQUENCE_ANIMATED_H
#define ENTITY_MIDI_SEQUENCE_ANIMATED_H

#include <irreden/ecs/entity_handle.hpp>
#include <irreden/ecs/prefabs.hpp>
#include <irreden/ir_math.hpp>
#include <irreden/common/components/component_tags_all.hpp>

using namespace IRECS;
using namespace IRMath;
using namespace IRComponents;

namespace IRECS {

    // TODO
    template <>
    struct Prefab<PrefabTypes::kMidiSequenceAnimated> {
        static EntityHandle create(

        )
        {
            EntityHandle entity{};

            return entity;
        }
    };

} // namespace IRECS

#endif /* ENTITY_MIDI_SEQUENCE_ANIMATED_H */
