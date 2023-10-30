/*
 * Project: Irreden Engine
 * File: \irreden-engine\src\game_components\component_midi_device.hpp
 * Author: Evin Killian jakildev@gmail.com
 * Created Date: October 2023
 * -----
 * Modified By: <your_name> <Month> <YYYY>
 */

#ifndef COMPONENT_MIDI_DEVICE_H
#define COMPONENT_MIDI_DEVICE_H

#include <irreden/ir_ecs.hpp>

namespace IRComponents {

    struct C_MidiDevice {
        int id_;
        // TODO: This should be stored elsewhere prob, like
        // as a relationship in ecs
        // std::unordered_map<unsigned char, EntityId> ccMessageMap_;

        C_MidiDevice(int id)
        :   id_(id)
        {

        }

        C_MidiDevice()
        :   C_MidiDevice(-1)
        {

        }

    };

} // namespace IRComponents

#endif /* COMPONENT_MIDI_DEVICE_H */
