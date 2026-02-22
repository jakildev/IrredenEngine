#ifndef SYSTEM_PERIODIC_IDLE_MIDI_TRIGGER_H
#define SYSTEM_PERIODIC_IDLE_MIDI_TRIGGER_H

#include <irreden/ir_system.hpp>
#include <irreden/ir_entity.hpp>
#include <irreden/ir_audio.hpp>
#include <irreden/ir_constants.hpp>

#include <irreden/update/components/component_periodic_idle.hpp>
#include <irreden/audio/components/component_midi_note.hpp>
#include <irreden/audio/components/component_midi_message.hpp>
#include <irreden/audio/components/component_midi_delay.hpp>
#include <irreden/common/components/component_tags_all.hpp>
#include <irreden/update/components/component_lifetime.hpp>

using namespace IRComponents;
using namespace IRAudio;

namespace IRSystem {

template <> struct System<PERIODIC_IDLE_MIDI_TRIGGER> {
    static SystemId create() {
        return createSystem<C_PeriodicIdle, C_MidiNote>(
            "PeriodicIdleMidiTrigger",
            [](C_PeriodicIdle &periodicIdle, C_MidiNote &midiNote) {
                if (!periodicIdle.cycleCompleted_) {
                    return;
                }
                unsigned char channel = normalizeMidiChannel(midiNote.channel_);
                // NOTE_ON: sent immediately
                IREntity::createEntity(
                    C_MidiMessage{buildMidiStatus(kMidiStatus_NOTE_ON, channel),
                                  midiNote.note_, midiNote.velocity_},
                    C_MidiOut{}, C_Lifetime{1});
                // NOTE_OFF: delayed by hold duration
                int holdFrames =
                    static_cast<int>(midiNote.holdSeconds_ * IRConstants::kFPS);
                holdFrames = std::max(holdFrames, 1);
                IREntity::createEntity(
                    C_MidiMessage{buildMidiStatus(kMidiStatus_NOTE_OFF, channel),
                                  midiNote.note_, midiNote.velocity_},
                    C_MidiDelay{holdFrames},
                    C_Lifetime{holdFrames + 10});
            });
    }
};

} // namespace IRSystem

#endif /* SYSTEM_PERIODIC_IDLE_MIDI_TRIGGER_H */
