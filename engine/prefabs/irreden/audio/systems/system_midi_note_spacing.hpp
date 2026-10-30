#ifndef SYSTEM_MIDI_NOTE_SPACING_H
#define SYSTEM_MIDI_NOTE_SPACING_H

#include <irreden/ir_system.hpp>

#include <irreden/audio/components/component_midi_note_spacing.hpp>
#include <irreden/audio/midi_note_spacing.hpp>

#include <cstddef>
#include <span>

namespace IRSystem {

// Plans the frame's queued notes and emits the messages due this frame
// (`IRPrefab::MidiNotes::planFrame`) from the `C_MidiNoteSpacing` singleton,
// draining its schedule whether or not the policy is still on. Pipeline
// placement: UPDATE after every trigger system that queues notes
// (`CONTACT_MIDI_TRIGGER`, `PERIODIC_IDLE_MIDI_TRIGGER`) and before
// `OUTPUT_MIDI_MESSAGE_OUT`, so a message due this frame goes out on it.
// Without this system in UPDATE the policy cannot be enabled: the Lua
// setter refuses, and the triggers keep sending as they always did.
template <> struct System<MIDI_NOTE_SPACING> {
    IRPrefab::MidiNotes::FramePlan plan_{};

    void tick(IRComponents::C_MidiNoteSpacing &spacing) {
        const int count = IRPrefab::MidiNotes::planFrame(spacing, plan_);
        IRPrefab::MidiNotes::emit(std::span{plan_.data(), static_cast<std::size_t>(count)});
    }

    static SystemId create() {
        IRPrefab::MidiNotes::ensureSpacingSingleton();
        return registerSystem<MIDI_NOTE_SPACING, IRComponents::C_MidiNoteSpacing>(
            "MidiNoteSpacing"
        );
    }
};

} // namespace IRSystem

#endif /* SYSTEM_MIDI_NOTE_SPACING_H */
