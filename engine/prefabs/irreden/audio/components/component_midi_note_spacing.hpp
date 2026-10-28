#ifndef COMPONENT_MIDI_NOTE_SPACING_H
#define COMPONENT_MIDI_NOTE_SPACING_H

#include <cstdint>
#include <vector>

namespace IRComponents {

// World-scoped singleton: the policy that spaces the notes the MIDI trigger
// systems fire. Off, a trigger sends its NOTE_ON the frame it fires and its
// NOTE_OFF after the hold. On, the frame's notes queue here and
// `MIDI_NOTE_SPACING` lays them out low to high, `staggerFrames_` apart and
// after any note still queued from an earlier frame, holding at most
// `maxVoices_` sounding at once (the oldest is cut off the frame a new one
// needs its voice), so a chord lands as an even upward strum the output can
// voice. Frames are the fixed update step, the cadence the delay queue
// sends on; `maxDelayFrames_` caps how far behind the queue a note may wait.
struct C_MidiNoteSpacing {
    bool enabled_ = false;
    int staggerFrames_ = 1;
    int maxVoices_ = 0;      // 0: no limit
    int maxDelayFrames_ = 0; // 0: no cap

    struct Pending {
        std::uint8_t note_;
        std::uint8_t velocity_;
        std::uint8_t channel_; // as the C_MidiNote carries it, normalized at plan time
        int holdFrames_;
    };
    struct Sounding {
        std::uint8_t note_;
        std::uint8_t channel_;
        long onFrame_;
        long offFrame_;
    };
    std::vector<Pending> pending_;   // this frame's triggered notes
    std::vector<Sounding> sounding_; // notes scheduled on and not yet off
    long frame_ = 0;                 // the planner's frame counter
    long nextFreeFrame_ = 0;         // the first frame a new NOTE_ON may take
};

} // namespace IRComponents

#endif /* COMPONENT_MIDI_NOTE_SPACING_H */
