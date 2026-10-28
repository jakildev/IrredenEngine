#ifndef COMPONENT_MIDI_NOTE_SPACING_H
#define COMPONENT_MIDI_NOTE_SPACING_H

#include <array>
#include <cstdint>

namespace IRComponents {

// World-scoped singleton: the policy that spaces the notes the MIDI trigger
// systems fire. Off, a trigger sends its NOTE_ON the frame it fires and its
// NOTE_OFF after the hold. On, the frame's notes queue here and
// `MIDI_NOTE_SPACING` lays them out low to high, `staggerFrames_` apart and
// after any note still queued from an earlier frame, holding at most
// `maxVoices_` sounding at once (the oldest is cut off the frame a new one
// needs its voice), so a chord lands as an even upward strum the output can
// voice. Frames are the fixed update step; `maxDelayFrames_` caps how far
// behind the queue a note may wait.
//
// Both lists are fixed-capacity so the trigger and spacing ticks never
// allocate: a note that finds either full is dropped and counted in
// `droppedNotes_`.
struct C_MidiNoteSpacing {
    static constexpr int kMaxPendingNotes = 128;
    static constexpr int kMaxScheduledNotes = 256;

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
    // A planned note the spacing system still owes a message for: its
    // NOTE_ON until `onSent_`, then its NOTE_OFF at `offFrame_`. A cut moves
    // `offFrame_` earlier, so the superseded hold end never goes out.
    struct Scheduled {
        std::uint8_t note_;
        std::uint8_t channel_; // normalized
        std::uint8_t velocity_;
        bool onSent_;
        long onFrame_;
        long offFrame_;
    };
    std::array<Pending, kMaxPendingNotes> pending_{};       // this frame's notes, low to high
    std::array<Scheduled, kMaxScheduledNotes> scheduled_{}; // in plan order
    int pendingCount_ = 0;
    int scheduledCount_ = 0;
    long frame_ = 0;         // the planner's frame counter
    long nextFreeFrame_ = 0; // the first frame a new NOTE_ON may take
    long droppedNotes_ = 0;
};

} // namespace IRComponents

#endif /* COMPONENT_MIDI_NOTE_SPACING_H */
