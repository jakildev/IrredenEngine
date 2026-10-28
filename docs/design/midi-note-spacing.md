# MIDI note spacing

A scene whose notes come from physics lands several at once: two blocks on
the same frame, a chord the output has to voice in the same millisecond. A
hardware synth with a handful of voices drops or chokes them, and a chord
struck dead together reads as one thud. `C_MidiNoteSpacing` is the policy
that spaces what the trigger systems fire: the frame's notes go out low to
high, a whole number of frames apart, behind any note still queued, within
the output's voice count.

Code: [`midi_note_spacing.hpp`](../../engine/prefabs/irreden/audio/midi_note_spacing.hpp)
(`IRPrefab::MidiNotes`: `trigger`, `planImmediate`, `planFrame`, `emit`),
[`component_midi_note_spacing.hpp`](../../engine/prefabs/irreden/audio/components/component_midi_note_spacing.hpp),
[`system_midi_note_spacing.hpp`](../../engine/prefabs/irreden/audio/systems/system_midi_note_spacing.hpp),
the `IRAudio.setNoteSpacing` / `noteSpacing` bindings in
[`lua_audio_bindings.hpp`](../../engine/script/include/irreden/script/lua_audio_bindings.hpp).

## Using it

```cpp
updatePipeline.push_back(IRSystem::createSystem<IRSystem::CONTACT_MIDI_TRIGGER>());
updatePipeline.push_back(IRSystem::createSystem<IRSystem::MIDI_NOTE_SPACING>());   // after every trigger, before MIDI_DELAY_PROCESS
```

```lua
-- 20 ms apart at 60 frames a second, six voices, never more than a bar's worth behind
IRAudio.setNoteSpacing({ enabled = true, staggerFrames = 1, maxVoices = 6, maxDelayFrames = 12 })
```

## Decisions

1. **One path for every trigger.** `CONTACT_MIDI_TRIGGER` and
   `PERIODIC_IDLE_MIDI_TRIGGER` both hand their `C_MidiNote` to
   `IRPrefab::MidiNotes::trigger`. With the policy off that is exactly the
   old behaviour (NOTE_ON this frame, NOTE_OFF after the hold through
   `C_MidiDelay`); with it on the note waits in the singleton for the
   frame's plan. A creation never wires spacing per note source.
2. **Frames, not milliseconds.** The delay queue sends on the fixed update
   step, so the stagger is a whole number of frames and the spacing is
   even by construction. A creation converts its milliseconds to frames
   once, where it knows the step.
3. **Low to high, then behind the queue.** The frame's notes sort by pitch
   (stable, so equal pitches keep trigger order). The first takes the
   later of this frame and the queue's next free frame; each next one
   follows a stagger later, and the queue's free frame advances, so notes
   on consecutive frames stay a stagger apart too. `maxDelayFrames_` caps
   how far behind a note may wait: past it the notes bunch rather than lag
   the picture.
4. **Voices are stolen, never dropped.** With `maxVoices_` set, a note that
   needs a voice cuts the oldest sounding note off on the frame it lands
   (its NOTE_OFF precedes the new NOTE_ON). The old note's own hold-end
   NOTE_OFF still fires later and is harmless on a silent pitch.
5. **Enabling needs the flush.** A queued note only leaves through
   `System<MIDI_NOTE_SPACING>`; the Lua setter refuses `enabled = true`
   while that system is not registered, so a pipeline without it keeps
   the immediate behaviour instead of swallowing notes.
6. **Opt-out of the snapshot.** The singleton holds one frame's queue and
   the sounding list; a save never carries a frame in flight.

## Headless coverage

`test/audio/midi_note_spacing_test.cpp` drives `planImmediate` / `planFrame`
without a world: order, stagger, the queue across frames, the voice cut,
the delay cap, channel normalization. `test/script/lua_midi_note_spacing_test.cpp`
drives the bindings: read-back, every rejection, and the refusal to enable
before the system exists.
