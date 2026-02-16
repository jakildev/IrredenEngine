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
#include <irreden/ir_entity.hpp>

#include <irreden/common/components/component_tags_all.hpp>
#include <irreden/audio/components/component_midi_device.hpp>
#include <irreden/common/components/component_name.hpp>
#include <irreden/audio/components/component_midi_channel.hpp>

using namespace IRComponents;
using namespace IRAudio;

namespace IREntity {

    template <>
    struct Prefab<PrefabTypes::kMidiDevice> {
        static EntityId create(
            // MidiChannel channel,
            std::string name,
            MidiDeviceType type
        )
        {
            EntityId entity = IREntity::createEntity(
                C_Name{name}
            );
            if(type == MidiDeviceType::MIDI_DEVICE_TYPE_IN) {
                IREntity::setComponent(entity, C_MidiDevice{
                    IRAudio::openPortMidiIn(
                        name
                    )
                });
                IREntity::setComponent(
                    entity,
                    C_MidiIn{}
                );
            }
            if(type == MidiDeviceType::MIDI_DEVICE_TYPE_OUT) {
                IREntity::setComponent(entity, C_MidiDevice{
                    IRAudio::openPortMidiOut(
                        name
                    )
                });
                IREntity::setComponent(
                    entity,
                    C_MidiOut{}
                );
            }
            return entity;
        }
    };

}


#endif /* ENTITY_MIDI_DEVICE_H */
