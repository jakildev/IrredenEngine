/*
 * Project: Irreden Engine
 * File: \irreden-engine\src\game_components\component_midi_message_note_on.hpp
 * Author: Evin Killian jakildev@gmail.com
 * Created Date: October 2023
 * -----
 * Modified By: <your_name> <Month> <YYYY>
 */

#ifndef COMPONENT_MIDI_MESSAGE_NOTE_ON_H
#define COMPONENT_MIDI_MESSAGE_NOTE_ON_H

#include <irreden/ir_math.hpp>
#include <irreden/common/components/component_tags_all.hpp>

using namespace IRMath;

namespace IRComponents {

    struct C_MidiMessageNoteOn {
        unsigned char channel_;
        unsigned char keyNumber_;
        unsigned char velocity_;

        C_MidiMessageNoteOn(
            unsigned char channel,
            unsigned char keyNumber,
            unsigned char velocity
        )
        :   channel_(channel),
            keyNumber_(keyNumber),
            velocity_(velocity)
        {

        }

        C_MidiMessageNoteOn()
        :   channel_(0),
            keyNumber_(0),
            velocity_(0)
        {

        }

    };

} // namespace IRComponents


#endif /* COMPONENT_MIDI_MESSAGE_NOTE_ON_H */
