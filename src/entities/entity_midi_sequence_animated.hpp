/*
 * Project: Irreden Engine
 * File: \irreden-engine\src\game_entities\entity_midi_sequence_animated.hpp
 * Author: Evin Killian jakildev@gmail.com
 * Created Date: October 2023
 * -----
 * Modified By: <your_name> <Month> <YYYY>
 */

#ifndef ENTITY_MIDI_SEQUENCE_ANIMATED_H
#define ENTITY_MIDI_SEQUENCE_ANIMATED_H

#include "..\entity\entity_handle.hpp"
#include "..\entity\prefabs.hpp"
#include "..\math\ir_math.hpp"
#include "..\game_components\component_tags_all.hpp"

using namespace IREntity;
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
