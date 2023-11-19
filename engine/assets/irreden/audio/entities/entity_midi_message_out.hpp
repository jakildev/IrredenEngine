/*
 * Project: Irreden Engine
 * File: entity_midi_message_out.hpp
 * Author: Evin Killian jakildev@gmail.com
 * Created Date: November 2023
 * -----
 * Modified By: <your_name> <Month> <YYYY>
 */

#ifndef ENTITY_MIDI_MESSAGE_OUT_H
#define ENTITY_MIDI_MESSAGE_OUT_H

#include <irreden/ir_ecs.hpp>
#include <irreden/audio/ir_audio_types.hpp>

#include <irreden/update/components/component_lifetime.hpp>
#include <irreden/audio/components/component_midi_message.hpp>
#include <irreden/audio/components/component_midi_channel.hpp>

namespace IRECS {

    template <>
    struct Prefab<PrefabTypes::kMidiMessageOut> {
        static EntityId create(
            MidiStatus status,
            MidiChannel channel,
            unsigned char data1,
            unsigned char data2 = 0
        )
        {
            return IRECS::createEntity(
                C_MidiMessage{
                    status | channel,
                    data1,
                    data2
                },
                C_MidiOut{},
                C_Lifetime{1}
            );
        }
    };

}

#endif /* ENTITY_MIDI_MESSAGE_OUT_H */
