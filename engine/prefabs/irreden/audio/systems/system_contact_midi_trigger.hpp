#ifndef SYSTEM_CONTACT_MIDI_TRIGGER_H
#define SYSTEM_CONTACT_MIDI_TRIGGER_H

#include <irreden/ir_system.hpp>

#include <irreden/audio/components/component_midi_note.hpp>
#include <irreden/audio/components/component_midi_note_spacing.hpp>
#include <irreden/audio/midi_note_spacing.hpp>
#include <irreden/update/components/component_contact_event.hpp>

namespace IRSystem {

// A `C_ContactEvent` enter fires the entity's `C_MidiNote` through the note
// spacing policy (`IRPrefab::MidiNotes::trigger`): NOTE_ON now and NOTE_OFF
// after the hold while the policy is off, a queued note while it is on.
template <> struct System<CONTACT_MIDI_TRIGGER> {
    IRComponents::C_MidiNoteSpacing *spacing_ = nullptr;

    void beginTick() {
        spacing_ = &IRPrefab::MidiNotes::spacing();
    }

    void tick(const IRComponents::C_ContactEvent &contact, IRComponents::C_MidiNote &midiNote) {
        if (!contact.entered_) {
            return;
        }
        IRPrefab::MidiNotes::trigger(*spacing_, midiNote);
    }

    static SystemId create() {
        IRPrefab::MidiNotes::ensureSpacingSingleton();
        return registerSystem<CONTACT_MIDI_TRIGGER, IRComponents::C_ContactEvent, IRComponents::C_MidiNote>(
            "ContactMidiTrigger"
        );
    }
};

} // namespace IRSystem

#endif /* SYSTEM_CONTACT_MIDI_TRIGGER_H */
