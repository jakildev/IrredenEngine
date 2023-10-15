/*
 * Project: Irreden Engine
 * File: \irreden-engine\src\game_entities\entity_midi_device.hpp
 * Author: Evin Killian jakildev@gmail.com
 * Created Date: October 2023
 * -----
 * Modified By: <your_name> <Month> <YYYY>
 */

#ifndef ENTITY_MIDI_DEVICE_H
#define ENTITY_MIDI_DEVICE_H

#include "../entity/entity_handle.hpp"
#include "../entity/prefabs.hpp"
#include "../math/ir_math.hpp"
#include "../audio/ir_audio.hpp"
#include "../game_components/collections/music.hpp"

using namespace IRComponents;
using namespace IRAudio;

namespace IRECS {

    template <>
    struct Prefab<PrefabTypes::kMidiDevice> {
        static EntityHandle create(
            IRMidiChannel channel,
            std::string name,
            IRMidiDeviceType type
        )
        {
            EntityHandle entity{};
            entity.set(C_Name{name})
            entity.set(C_MidiChannel{channel});
            if(type == IRMidiDeviceType::MIDI_DEVICE_TYPE_IN) {
                entity.set(C_MidiDeviceIn{});
            }
            return entity;
        }
    };

}


#endif /* ENTITY_MIDI_DEVICE_H */
