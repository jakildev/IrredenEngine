#ifndef COMPONENT_MIDI_CHANNEL_H
#define COMPONENT_MIDI_CHANNEL_H

#include <irreden/ir_audio.hpp>

using IRAudio::MidiChannel;

namespace IRComponents {

struct C_MidiChannel {
    MidiChannel channel_;

    C_MidiChannel(MidiChannel channel) : channel_(channel) {}

    C_MidiChannel() : channel_(0) {}
};
} // namespace IRComponents

#endif /* COMPONENT_MIDI_CHANNEL_H */
