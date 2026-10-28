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

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

// The one path a triggered `C_MidiNote` takes to the wire. A trigger system
// calls `trigger`; with the spacing policy off that is the NOTE_ON now and
// the NOTE_OFF after the hold, with it on the note waits for
// `System<MIDI_NOTE_SPACING>` to plan the frame (`planFrame`) and emit it.
// `planImmediate` / `planFrame` are pure over the component so they test
// without a world; `emit` turns a plan into message entities.
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
    return IRAudio::buildMidiStatus(IRAudio::kMidiStatus_NOTE_ON, IRAudio::normalizeMidiChannel(channel));
}

inline std::uint8_t noteOffStatus(std::uint8_t channel) {
    return IRAudio::buildMidiStatus(IRAudio::kMidiStatus_NOTE_OFF, IRAudio::normalizeMidiChannel(channel));
}

// The unspaced plan: NOTE_ON this frame, NOTE_OFF after the hold.
inline void planImmediate(const IRComponents::C_MidiNote &note, std::vector<ScheduledMessage> &out) {
    out.push_back({0, noteOnStatus(note.channel_), note.note_, note.velocity_});
    out.push_back({holdFrames(note), noteOffStatus(note.channel_), note.note_, note.velocity_});
}

// Lays the frame's pending notes out and advances the frame: low to high,
// each `staggerFrames_` after the one before it and never before the queue's
// next free frame (capped `maxDelayFrames_` behind this one), a note past
// `maxVoices_` cutting off the oldest sounding note the frame it lands.
// Appends the messages, delays relative to this frame, and clears the
// pending list.
inline void planFrame(IRComponents::C_MidiNoteSpacing &s, std::vector<ScheduledMessage> &out) {
    using Pending = IRComponents::C_MidiNoteSpacing::Pending;
    using Sounding = IRComponents::C_MidiNoteSpacing::Sounding;
    const long now = s.frame_;
    std::stable_sort(s.pending_.begin(), s.pending_.end(), [](const Pending &a, const Pending &b) {
        return a.note_ < b.note_;
    });
    for (const Pending &p : s.pending_) {
        long onFrame = IRMath::max(now, s.nextFreeFrame_);
        if (s.maxDelayFrames_ > 0) {
            onFrame = IRMath::min(onFrame, now + static_cast<long>(s.maxDelayFrames_));
        }
        std::erase_if(s.sounding_, [onFrame](const Sounding &v) { return v.offFrame_ <= onFrame; });
        if (s.maxVoices_ > 0 && static_cast<int>(s.sounding_.size()) >= s.maxVoices_) {
            const auto oldest = std::min_element(
                s.sounding_.begin(), s.sounding_.end(), [](const Sounding &a, const Sounding &b) {
                    return a.onFrame_ < b.onFrame_;
                }
            );
            out.push_back({static_cast<int>(onFrame - now), noteOffStatus(oldest->channel_), oldest->note_, 0});
            s.sounding_.erase(oldest);
        }
        out.push_back({static_cast<int>(onFrame - now), noteOnStatus(p.channel_), p.note_, p.velocity_});
        out.push_back(
            {static_cast<int>(onFrame - now) + p.holdFrames_, noteOffStatus(p.channel_), p.note_, p.velocity_}
        );
        s.sounding_.push_back({p.note_, p.channel_, onFrame, onFrame + p.holdFrames_});
        s.nextFreeFrame_ = onFrame + IRMath::max(s.staggerFrames_, 1);
    }
    s.pending_.clear();
    ++s.frame_;
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
        s.pending_.push_back({note.note_, note.velocity_, note.channel_, holdFrames(note)});
        return;
    }
    ScheduledMessage plan[2];
    plan[0] = {0, noteOnStatus(note.channel_), note.note_, note.velocity_};
    plan[1] = {holdFrames(note), noteOffStatus(note.channel_), note.note_, note.velocity_};
    emit(plan);
}

} // namespace IRPrefab::MidiNotes

#endif /* MIDI_NOTE_SPACING_H */
