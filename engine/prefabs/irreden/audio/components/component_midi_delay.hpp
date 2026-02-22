#ifndef COMPONENT_MIDI_DELAY_H
#define COMPONENT_MIDI_DELAY_H

namespace IRComponents {

struct C_MidiDelay {
    int framesRemaining_;

    C_MidiDelay(int frames) : framesRemaining_{frames} {}

    C_MidiDelay() : framesRemaining_{0} {}
};

} // namespace IRComponents

#endif /* COMPONENT_MIDI_DELAY_H */
