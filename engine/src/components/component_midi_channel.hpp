/*
 * Project: Irreden Engine
 * File: \irreden-engine\src\game_components\component_midi_channel.hpp
 * Author: Evin Killian jakildev@gmail.com
 * Created Date: October 2023
 * -----
 * Modified By: <your_name> <Month> <YYYY>
 */

#ifndef COMPONENT_MIDI_CHANNEL_H
#define COMPONENT_MIDI_CHANNEL_H

#include "../audio/ir_audio.hpp"

using namespace IRAudio;

namespace IRComponents {

    struct C_MidiChannel {
        IRMidiChannel channel_;

        C_MidiChannel(
            IRMidiChannel channel
        )
        :   channel_(channel)
        {

        }

        C_MidiChannel()
        :   channel_(0)
        {

        }

    };
}

#endif /* COMPONENT_MIDI_CHANNEL_H */
