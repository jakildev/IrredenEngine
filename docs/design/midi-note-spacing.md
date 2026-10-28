# MIDI note spacing

A scene whose notes come from physics lands several at once: two blocks on
the same frame, a chord the output has to voice in the same millisecond. A
hardware synth with a handful of voices drops or chokes them, and a chord
struck dead together reads as one thud. `C_MidiNoteSpacing` is the policy
that spaces what the trigger systems fire: the frame's notes go out low to
high, a whole number of frames apart, behind any note still queued, within
the output's voice count.

Code: [`midi_note_spacing.hpp`](../../engine/prefabs/irreden/audio/midi_note_spacing.hpp)
(`IRPrefab::MidiNotes`: `trigger`, `planImmediate`, `queue`, `planFrame`, `emit`),
[`component_midi_note_spacing.hpp`](../../engine/prefabs/irreden/audio/components/component_midi_note_spacing.hpp),
[`system_midi_note_spacing.hpp`](../../engine/prefabs/irreden/audio/systems/system_midi_note_spacing.hpp),
the `IRAudio.setNoteSpacing` / `noteSpacing` bindings in
[`lua_audio_bindings.hpp`](../../engine/script/include/irreden/script/lua_audio_bindings.hpp).

## Using it

```cpp
updatePipeline.push_back(IRSystem::createSystem<IRSystem::CONTACT_MIDI_TRIGGER>());
updatePipeline.push_back(IRSystem::createSystem<IRSystem::MIDI_NOTE_SPACING>());   // after every trigger, before OUTPUT_MIDI_MESSAGE_OUT
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
2. **The spacing system owns a spaced note to the end.** A planned note
   stays in the singleton's schedule until its NOTE_ON and NOTE_OFF have
   both gone out, each on the frame it is due as an immediate message; it
   never enters the `C_MidiDelay` queue, so its hold end can still move.
3. **Frames, not milliseconds.** The delay queue sends on the fixed update
   step, so the stagger is a whole number of frames and the spacing is
   even by construction. A creation converts its milliseconds to frames
   once, where it knows the step.
4. **Low to high, then behind the queue.** The frame's notes sort by pitch
   (stable, so equal pitches keep trigger order). The first takes the
   later of this frame and the queue's next free frame; each next one
   follows a stagger later, and the queue's free frame advances, so notes
   on consecutive frames stay a stagger apart too. `maxDelayFrames_` caps
   how far behind a note may wait: past it the notes bunch rather than lag
   the picture.
5. **A cut moves the hold end.** A note that lands on a pitch and channel
   still sounding ends that note there, and with `maxVoices_` set a note
   that needs a voice then ends the oldest sounding one. The cut note's
   NOTE_OFF moves to the frame the new note lands (a frame's NOTE_OFFs go
   out before its NOTE_ONs), and its original hold end is gone, so it can
   never silence a later note of the same pitch; a cut note whose NOTE_ON
   had not gone out yet never sounds.
6. **Fixed capacity.** The pending list (128) and the schedule (256) are
   fixed arrays, so neither the trigger tick nor the spacing tick
   allocates. A note that finds either full is dropped and counted in
   `droppedNotes_` (`IRAudio.noteSpacing().droppedNotes`); reaching it
   means a backlog of seconds, which `maxDelayFrames_` bounds.
7. **Enabling needs the flush.** A queued note only leaves through
   `System<MIDI_NOTE_SPACING>`; the Lua setter refuses `enabled = true`
   while that system is not in the UPDATE pipeline
   (`IRSystem::isInPipeline`), so a pipeline without it keeps the immediate
   behaviour instead of swallowing notes. Its order there is not checked:
   placed before a trigger, a note waits one more frame.
8. **Opt-out of the snapshot.** The singleton holds one frame's queue and
   the schedule; a save never carries a frame in flight.

## Headless coverage

`test/audio/midi_note_spacing_test.cpp` drives `planImmediate`, `queue` and
`planFrame` a frame at a time without a world: order, stagger, the queue
across frames, the voice cut, a repeated pitch through the cut note's old
hold end, the delay cap, channel normalization, and a dense frame against
the fixed capacity. `test/script/lua_midi_note_spacing_test.cpp` drives the
bindings: read-back, every rejection, and the refusal to enable while the
system is unregistered or registered but outside UPDATE, then composing it
into UPDATE by `IRSystem.SystemName.MIDI_NOTE_SPACING`.
