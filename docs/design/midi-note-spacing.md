# MIDI note spacing

A scene whose notes come from physics lands several at once: two blocks on
the same frame, a chord the output has to voice in the same millisecond. A
hardware synth with a handful of voices drops or chokes them, and a chord
struck dead together reads as one thud. `C_MidiNoteSpacing` is the policy
that spaces what the trigger systems fire: the frame's notes go out low to
high, a whole number of frames apart, behind any note still queued, within
the output's voice count.

Code: [`midi_note_spacing.hpp`](../../engine/prefabs/irreden/audio/midi_note_spacing.hpp)
(`IRPrefab::MidiNotes`: `trigger`, `planTrigger`, `planImmediate`, `queue`, `planFrame`, `emit`),
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
   `IRPrefab::MidiNotes::trigger`. With the policy off that is a NOTE_ON
   this frame and a NOTE_OFF after the hold (decision 6 adds one ahead of a
   retriggered pitch the schedule still owns); with it on
   the note waits in the singleton for the frame's plan. A creation never
   wires spacing per note source.
2. **The spacing system owns a note to the end.** A planned note stays in
   the singleton's schedule until its NOTE_ON and NOTE_OFF have both gone
   out, each on the frame it is due as an immediate message; it never
   enters the `C_MidiDelay` queue, so its hold end can still move. While
   `MIDI_NOTE_SPACING` is in UPDATE (`spacingDrains`, read by each trigger
   in `beginTick`) an immediate note's hold end goes into the schedule too,
   so a note of its pitch can cut it whichever way the policy has been
   switched since. Without the system, or with the schedule full, the hold
   end waits in `C_MidiDelay` as before.
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
5. **A cut moves the hold end.** A planned note on a pitch and channel
   still sounding where it will land ends that note there. With
   `maxVoices_` set, each note, as it lands, ends the oldest sounding ones
   until it has a voice; the cap is read on the landing frame, so a cap
   lowered after notes were planned holds where each of them lands. The
   cut note's NOTE_OFF moves to the frame the new note lands (a frame's
   NOTE_OFFs go out before its NOTE_ONs), and its original hold end is
   gone, so it can never silence a later note of the same pitch; a cut
   note whose NOTE_ON had not gone out yet, one landing the same frame
   included, never sounds.
6. **Off, an immediate note takes over its pitch.** Turning the policy off
   leaves the planned notes draining through the schedule. A trigger that
   now sends at once first ends an owned note of its pitch and channel
   (its NOTE_OFF ahead of the new NOTE_ON, its scheduled hold end gone) and
   drops a queued or planned one, so no earlier message can silence or
   follow the immediate note. Turning it back on, decision 5 covers the
   immediate notes still held: their hold ends are in the schedule.
7. **Fixed capacity.** The pending list (128) and the schedule (256) are
   fixed arrays, so neither the trigger tick nor the spacing tick
   allocates. A note that finds either full is dropped and counted in
   `droppedNotes_` (`IRAudio.noteSpacing().droppedNotes`); reaching it
   means a backlog of seconds, which `maxDelayFrames_` bounds.
8. **Enabling needs the flush.** A queued note only leaves through
   `System<MIDI_NOTE_SPACING>`; the Lua setter refuses `enabled = true`
   while that system is not in the UPDATE pipeline
   (`IRSystem::isInPipeline`), so a pipeline without it keeps the immediate
   behaviour instead of swallowing notes. Its order there is not checked:
   placed before a trigger, a note waits one more frame.
9. **Messages are staged.** The triggers and the spacing system emit from
   inside their ticks, so `emit` stages each message entity for the next
   `flushStructuralChanges`; the group boundary after the emitting system
   drains it, ahead of `OUTPUT_MIDI_MESSAGE_OUT`.
10. **Opt-out of the snapshot.** The singleton holds one frame's queue and
   the schedule; a save never carries a frame in flight.

## Headless coverage

`test/audio/midi_note_spacing_test.cpp` drives `planImmediate`, `queue` and
`planFrame` / `planTrigger` a frame at a time without a world: order,
stagger, the queue across frames, the voice cut, a cap lowered under
several sounding notes and under notes already planned, a note cut the
frame it lands, a repeated pitch through the cut note's old hold end, the
policy turned off mid-hold under an immediate retrigger, an owned immediate
hold ended by its retrigger or by a spaced note once the policy is back on,
the delay cap, channel normalization, and a dense frame against the fixed
capacity. `MidiNoteSpacingPipelineTest` runs `CONTACT_MIDI_TRIGGER`,
`MIDI_NOTE_SPACING` and an output probe in UPDATE order: the staged
messages reach the probe on their frame, and enabling mid-hold never lets
the immediate note's hold end silence the spaced retrigger. `test/script/lua_midi_note_spacing_test.cpp` drives the
bindings: read-back, every rejection, and the refusal to enable while the
system is unregistered or registered but outside UPDATE, then composing it
into UPDATE by `IRSystem.SystemName.MIDI_NOTE_SPACING`.
