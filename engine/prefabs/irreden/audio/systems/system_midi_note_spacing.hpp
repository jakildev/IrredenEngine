#ifndef SYSTEM_MIDI_NOTE_SPACING_H
#define SYSTEM_MIDI_NOTE_SPACING_H

#include <irreden/ir_system.hpp>

#include <irreden/audio/components/component_midi_note_spacing.hpp>
#include <irreden/audio/midi_note_spacing.hpp>

#include <vector>

namespace IRSystem {

// Plans and emits the frame's queued notes (`IRPrefab::MidiNotes::planFrame`)
// from the `C_MidiNoteSpacing` singleton. Pipeline placement: UPDATE after
// every trigger system that queues notes (`CONTACT_MIDI_TRIGGER`,
// `PERIODIC_IDLE_MIDI_TRIGGER`) and before `MIDI_DELAY_PROCESS` /
// `OUTPUT_MIDI_MESSAGE_OUT`, so a note planned for this frame goes out on
// it. Without this system the policy cannot be enabled: the Lua setter
// refuses, and the triggers keep sending as they always did.
template <> struct System<MIDI_NOTE_SPACING> {
    std::vector<IRPrefab::MidiNotes::ScheduledMessage> plan_;

    void tick(IRComponents::C_MidiNoteSpacing &spacing) {
        plan_.clear();
        IRPrefab::MidiNotes::planFrame(spacing, plan_);
        IRPrefab::MidiNotes::emit(plan_);
    }

    static SystemId create() {
        IRPrefab::MidiNotes::ensureSpacingSingleton();
        return registerSystem<MIDI_NOTE_SPACING, IRComponents::C_MidiNoteSpacing>("MidiNoteSpacing");
    }
};

} // namespace IRSystem

#endif /* SYSTEM_MIDI_NOTE_SPACING_H */
