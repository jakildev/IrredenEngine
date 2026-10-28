#ifndef SYSTEM_PERIODIC_IDLE_MIDI_TRIGGER_H
#define SYSTEM_PERIODIC_IDLE_MIDI_TRIGGER_H

#include <irreden/ir_system.hpp>

#include <irreden/audio/components/component_midi_note.hpp>
#include <irreden/audio/components/component_midi_note_spacing.hpp>
#include <irreden/audio/midi_note_spacing.hpp>
#include <irreden/update/components/component_periodic_idle.hpp>

namespace IRSystem {

// A completed `C_PeriodicIdle` cycle fires the entity's `C_MidiNote` through
// the note spacing policy (`IRPrefab::MidiNotes::trigger`), as a contact does.
template <> struct System<PERIODIC_IDLE_MIDI_TRIGGER> {
    IRComponents::C_MidiNoteSpacing *spacing_ = nullptr;
    bool spacingDrains_ = false;

    void beginTick() {
        spacing_ = &IRPrefab::MidiNotes::spacing();
        spacingDrains_ = IRPrefab::MidiNotes::spacingDrains();
    }

    void tick(IRComponents::C_PeriodicIdle &periodicIdle, IRComponents::C_MidiNote &midiNote) {
        if (!periodicIdle.cycleCompleted_) {
            return;
        }
        IRPrefab::MidiNotes::trigger(*spacing_, midiNote, spacingDrains_);
    }

    static SystemId create() {
        IRPrefab::MidiNotes::ensureSpacingSingleton();
        return registerSystem<
            PERIODIC_IDLE_MIDI_TRIGGER,
            IRComponents::C_PeriodicIdle,
            IRComponents::C_MidiNote>("PeriodicIdleMidiTrigger");
    }
};

} // namespace IRSystem

#endif /* SYSTEM_PERIODIC_IDLE_MIDI_TRIGGER_H */
