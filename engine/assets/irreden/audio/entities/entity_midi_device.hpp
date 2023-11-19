/*
 * Project: Irreden Engine
 * File: entity_midi_device.hpp
 * Author: Evin Killian jakildev@gmail.com
 * Created Date: October 2023
 * -----
 * Modified By: <your_name> <Month> <YYYY>
 */

#ifndef ENTITY_MIDI_DEVICE_H
#define ENTITY_MIDI_DEVICE_H

#include <irreden/ir_math.hpp>
#include <irreden/ir_audio.hpp>
#include <irreden/ir_ecs.hpp>

#include <irreden/common/components/component_name.hpp>
#include <irreden/audio/components/component_midi_channel.hpp>
#include <irreden/audio/components/component_midi_device_in.hpp>

using namespace IRComponents;
using namespace IRAudio;

namespace IRECS {

    template <>
    struct Prefab<PrefabTypes::kMidiDevice> {
        static EntityHandle create(
            MidiChannel channel,
            std::string name,
            MidiDeviceType type
        )
        {
            EntityHandle entity{};
            entity.set(C_Name{name})
            entity.set(C_MidiChannel{channel});
            if(type == MidiDeviceType::MIDI_DEVICE_TYPE_IN) {
                entity.set(C_MidiDeviceIn{});
            }
            return entity;
        }
    };

}


#endif /* ENTITY_MIDI_DEVICE_H */
