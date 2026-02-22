#ifndef COMPONENT_MIDI_NOTE_H
#define COMPONENT_MIDI_NOTE_H

#include <irreden/ir_audio.hpp>
#include <irreden/audio/ir_audio_types.hpp>

namespace IRComponents {

struct C_MidiNote {
    unsigned char note_;
    unsigned char velocity_;
    unsigned char channel_;
    float holdSeconds_;

    C_MidiNote(
        unsigned char note,
        unsigned char velocity,
        unsigned char channel = 0,
        float holdSeconds = 0.1f
    )
        : note_{note}
        , velocity_{velocity}
        , channel_{channel}
        , holdSeconds_{holdSeconds} {}

    C_MidiNote()
        : note_{60}
        , velocity_{100}
        , channel_{0}
        , holdSeconds_{0.1f} {}

    void onDestroy() {
        const unsigned char normalizedChannel = IRAudio::normalizeMidiChannel(channel_);
        IRAudio::sendMidiMessage(
            {IRAudio::buildMidiStatus(IRAudio::kMidiStatus_NOTE_OFF, normalizedChannel), note_, 0}
        );
    }
};

} // namespace IRComponents

#endif /* COMPONENT_MIDI_NOTE_H */
