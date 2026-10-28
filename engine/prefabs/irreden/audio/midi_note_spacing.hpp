#ifndef MIDI_NOTE_SPACING_H
#define MIDI_NOTE_SPACING_H

#include <irreden/ir_audio.hpp>
#include <irreden/ir_constants.hpp>
#include <irreden/ir_entity.hpp>
#include <irreden/ir_math.hpp>

#include <irreden/audio/components/component_midi_delay.hpp>
#include <irreden/audio/components/component_midi_message.hpp>
#include <irreden/audio/components/component_midi_note.hpp>
#include <irreden/audio/components/component_midi_note_spacing.hpp>
#include <irreden/common/components/component_tags_all.hpp>
#include <irreden/update/components/component_lifetime.hpp>

#include <array>
#include <cstdint>
#include <span>

// The one path a triggered `C_MidiNote` takes to the wire. A trigger system
// calls `trigger`; with the spacing policy off that is the NOTE_ON now and
// the NOTE_OFF after the hold, with it on the note waits for
// `System<MIDI_NOTE_SPACING>` to plan the frame (`planFrame`) and emit it;
// that system owns a spaced note's NOTE_ON and NOTE_OFF until both go out,
// so a cut cancels the superseded hold end. `planImmediate` / `queue` /
// `planFrame` are pure over the component so they test without a world;
// `emit` turns a plan into message entities.
namespace IRPrefab::MidiNotes {

struct ScheduledMessage {
    int delayFrames_; // 0: this frame
    std::uint8_t status_;
    std::uint8_t data1_;
    std::uint8_t data2_;
};

inline IRComponents::C_MidiNoteSpacing &spacing() {
    return IREntity::singleton<IRComponents::C_MidiNoteSpacing>();
}

// Registration-time singleton touch, so the lazy create never first runs
// inside a tick (`widget_theme.hpp` documents why).
inline void ensureSpacingSingleton() {
    spacing();
}

// A hold in whole frames, at least one.
inline int holdFrames(const IRComponents::C_MidiNote &note) {
    return IRMath::max(static_cast<int>(note.holdSeconds_ * IRConstants::kFPS), 1);
}

inline std::uint8_t noteOnStatus(std::uint8_t channel) {
    return IRAudio::buildMidiStatus(
        IRAudio::kMidiStatus_NOTE_ON,
        IRAudio::normalizeMidiChannel(channel)
    );
}

inline std::uint8_t noteOffStatus(std::uint8_t channel) {
    return IRAudio::buildMidiStatus(
        IRAudio::kMidiStatus_NOTE_OFF,
        IRAudio::normalizeMidiChannel(channel)
    );
}

// The unspaced plan: NOTE_ON this frame, NOTE_OFF after the hold.
inline std::array<ScheduledMessage, 2> planImmediate(const IRComponents::C_MidiNote &note) {
    return {{
        {0, noteOnStatus(note.channel_), note.note_, note.velocity_},
        {holdFrames(note), noteOffStatus(note.channel_), note.note_, note.velocity_},
    }};
}

// The most messages one `planFrame` can produce: each scheduled note owes at
// most its NOTE_ON and its NOTE_OFF on a given frame.
inline constexpr int kMaxFrameMessages = 2 * IRComponents::C_MidiNoteSpacing::kMaxScheduledNotes;
using FramePlan = std::array<ScheduledMessage, kMaxFrameMessages>;

// Queues a note for the next `planFrame`, keeping the pending list low to
// high (equal pitches in trigger order). A full list drops the note.
inline void
queue(IRComponents::C_MidiNoteSpacing &s, const IRComponents::C_MidiNoteSpacing::Pending &p) {
    if (s.pendingCount_ >= IRComponents::C_MidiNoteSpacing::kMaxPendingNotes) {
        ++s.droppedNotes_;
        return;
    }
    int at = s.pendingCount_;
    while (at > 0 && s.pending_[at - 1].note_ > p.note_) {
        s.pending_[at] = s.pending_[at - 1];
        --at;
    }
    s.pending_[at] = p;
    ++s.pendingCount_;
}

namespace detail {

inline void removeScheduled(IRComponents::C_MidiNoteSpacing &s, int index) {
    for (int i = index + 1; i < s.scheduledCount_; ++i) {
        s.scheduled_[i - 1] = s.scheduled_[i];
    }
    --s.scheduledCount_;
}

// Ends `s.scheduled_[index]` at `frame`: a note whose NOTE_ON has not gone
// out by then never sounds, any other one's NOTE_OFF moves to `frame`.
inline void cutScheduled(IRComponents::C_MidiNoteSpacing &s, int index, long frame) {
    IRComponents::C_MidiNoteSpacing::Scheduled &v = s.scheduled_[index];
    if (!v.onSent_ && v.onFrame_ >= frame) {
        removeScheduled(s, index);
        return;
    }
    v.offFrame_ = frame;
}

} // namespace detail

// Plans the frame's pending notes, then appends every message due this
// frame (NOTE_OFFs before NOTE_ONs, all with a delay of 0) and advances the
// frame. A note lands low to high, `staggerFrames_` after the one before it
// and never before the queue's next free frame (capped `maxDelayFrames_`
// behind this one). Where it lands it cuts a still-sounding note of its own
// pitch and channel, then, past `maxVoices_`, the oldest sounding note.
// Returns the message count.
inline int planFrame(IRComponents::C_MidiNoteSpacing &s, FramePlan &out) {
    using Scheduled = IRComponents::C_MidiNoteSpacing::Scheduled;
    const long now = s.frame_;
    for (int pi = 0; pi < s.pendingCount_; ++pi) {
        const IRComponents::C_MidiNoteSpacing::Pending &p = s.pending_[pi];
        if (s.scheduledCount_ >= IRComponents::C_MidiNoteSpacing::kMaxScheduledNotes) {
            ++s.droppedNotes_;
            continue;
        }
        long onFrame = IRMath::max(now, s.nextFreeFrame_);
        if (s.maxDelayFrames_ > 0) {
            onFrame = IRMath::min(onFrame, now + static_cast<long>(s.maxDelayFrames_));
        }
        const std::uint8_t channel = IRAudio::normalizeMidiChannel(p.channel_);
        for (int i = 0; i < s.scheduledCount_; ++i) {
            const Scheduled &v = s.scheduled_[i];
            if (v.offFrame_ > onFrame && v.note_ == p.note_ && v.channel_ == channel) {
                detail::cutScheduled(s, i, onFrame);
                break;
            }
        }
        if (s.maxVoices_ > 0) {
            int sounding = 0;
            int oldest = -1;
            for (int i = 0; i < s.scheduledCount_; ++i) {
                const Scheduled &v = s.scheduled_[i];
                if (v.offFrame_ <= onFrame) {
                    continue;
                }
                ++sounding;
                if (oldest < 0 || v.onFrame_ < s.scheduled_[oldest].onFrame_) {
                    oldest = i;
                }
            }
            if (sounding >= s.maxVoices_) {
                detail::cutScheduled(s, oldest, onFrame);
            }
        }
        s.scheduled_[s.scheduledCount_++] =
            Scheduled{p.note_, channel, p.velocity_, false, onFrame, onFrame + p.holdFrames_};
        s.nextFreeFrame_ = onFrame + IRMath::max(s.staggerFrames_, 1);
    }
    s.pendingCount_ = 0;

    int count = 0;
    for (int i = 0; i < s.scheduledCount_;) {
        const Scheduled &v = s.scheduled_[i];
        if (v.onSent_ && v.offFrame_ <= now) {
            out[count++] = {0, noteOffStatus(v.channel_), v.note_, v.velocity_};
            detail::removeScheduled(s, i);
            continue;
        }
        ++i;
    }
    for (int i = 0; i < s.scheduledCount_; ++i) {
        Scheduled &v = s.scheduled_[i];
        if (!v.onSent_ && v.onFrame_ <= now) {
            out[count++] = {0, noteOnStatus(v.channel_), v.note_, v.velocity_};
            v.onSent_ = true;
        }
    }
    ++s.frame_;
    return count;
}

// Message entities for a plan: a delay of 0 goes out this frame, a later one
// waits in the delay queue (`MIDI_DELAY_PROCESS`), the lifetime outlasting it.
inline void emit(std::span<const ScheduledMessage> plan) {
    for (const ScheduledMessage &m : plan) {
        if (m.delayFrames_ <= 0) {
            IREntity::createEntity(
                IRComponents::C_MidiMessage{m.status_, m.data1_, m.data2_},
                IRComponents::C_MidiOut{},
                IRComponents::C_Lifetime{1}
            );
            continue;
        }
        IREntity::createEntity(
            IRComponents::C_MidiMessage{m.status_, m.data1_, m.data2_},
            IRComponents::C_MidiDelay{m.delayFrames_},
            IRComponents::C_Lifetime{m.delayFrames_ + 10}
        );
    }
}

// A trigger's note: queued under the policy when it is on, else sent now.
inline void trigger(IRComponents::C_MidiNoteSpacing &s, const IRComponents::C_MidiNote &note) {
    if (s.enabled_) {
        queue(s, {note.note_, note.velocity_, note.channel_, holdFrames(note)});
        return;
    }
    emit(planImmediate(note));
}

} // namespace IRPrefab::MidiNotes

#endif /* MIDI_NOTE_SPACING_H */
