/*
 * Project: Irreden Engine
 * File: component_midi_device.hpp
 * Author: Evin Killian jakildev@gmail.com
 * Created Date: October 2023
 * -----
 * Modified By: <your_name> <Month> <YYYY>
 */

#ifndef COMPONENT_MIDI_DEVICE_H
#define COMPONENT_MIDI_DEVICE_H

namespace IRComponents {

    struct C_MidiDevice {
        int portIndex_;

        C_MidiDevice(int id)
        :   portIndex_(id)
        {

        }

        C_MidiDevice()
        :   C_MidiDevice(-1)
        {

        }

    };

} // namespace IRComponents

#endif /* COMPONENT_MIDI_DEVICE_H */
