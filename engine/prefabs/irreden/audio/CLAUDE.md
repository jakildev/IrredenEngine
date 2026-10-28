# engine/prefabs/irreden/audio/ — MIDI messages and sequences

MIDI composition, sequencing, contact/idle triggers, and device wiring.
Everything here is ECS-driven — there is no standalone audio "engine". A
creation composes MIDI behavior by adding components to entities.

## Key components

- `C_MidiMessage` — status + channel + data1/data2. Helpers for note
  number, velocity, CC value.
- `C_MidiSourcePort` — deprecated source identity for synthetic inbound
  message entities. Hardware input now retains its port identity while
  `MidiIn::tick()` drains the callback queues directly.
- `C_MidiSequence` — BPM, time signature, measures, tick-based playback,
  loop flag, message buffer.
- `C_MidiNote` — note number, velocity, channel, hold duration.
  **`onDestroy()` sends `NOTE_OFF`** — a free cleanup when an entity dies.
- `C_MidiDevice` — wraps a session-local MIDI logical handle (-1 = uninitialized).
  The serialized integer remains version 1, but validity across a process
  restart is not promised. Pair
  with `C_MidiIn` / `C_MidiOut` tags.
- `C_MidiChannel` — 0–15 wrapper. `normalizeMidiChannel()` converts to
  MIDI wire format.
- `C_MidiDelay` — countdown timer for scheduling a delayed `NOTE_OFF`.
- `C_MidiNoteSpacing` — world singleton: the policy that spaces what the
  trigger systems fire (low to high, a stagger of frames apart, behind the
  queue, within a voice count). Off by default; `IRPrefab::MidiNotes`
  (`midi_note_spacing.hpp`) owns the planner
  ([design](../../../../docs/design/midi-note-spacing.md)).
- `C_ContactEvent` — tag for entities whose collision events should trigger
  MIDI.

Many of these have `_lua.hpp` siblings that bind them into Lua.

## Key systems

- `MIDI_SEQUENCE_OUT` (UPDATE pipeline) — ticks a `C_MidiSequence` each
  frame, emits queued messages as ephemeral entities
  (`C_MidiMessage + C_MidiOut + C_Lifetime{1}`). Handles looping.
- `CONTACT_MIDI_TRIGGER` (UPDATE pipeline) — on `C_ContactEvent`
  enter, hands the `C_MidiNote` to `IRPrefab::MidiNotes::trigger`: `NOTE_ON`
  now and `NOTE_OFF` after the hold via `C_MidiDelay`, or a queued note under
  the spacing policy.
- `PERIODIC_IDLE_MIDI_TRIGGER` — the same on `C_PeriodicIdle` cycle
  completion.
- `MIDI_NOTE_SPACING` (UPDATE, after the triggers, before
  `MIDI_DELAY_PROCESS`) — plans and emits the frame's queued notes. Without
  it the policy cannot be enabled (`IRAudio.setNoteSpacing` refuses).
- `MIDI_DELAY_PROCESS` — decrements `C_MidiDelay`; when it hits zero,
  emits a `NOTE_OFF` transient entity and marks delay = -1 to prevent
  re-fire.

## Commands

None. MIDI control is entirely entity-driven.

## Entity builders

- `entity_midi_device.hpp` — opens an RtMidi port (IN or OUT) and
  names the device entity.
- `entity_midi_message_out.hpp` — builds a transient out-message
  (`C_MidiMessage + C_MidiOut + C_Lifetime{1}`). Dies after one frame.
- `entity_midi_sequence_animated.hpp` — stub.

## Gotchas

- **`C_MidiNote::onDestroy()` auto-sends `NOTE_OFF`.** Clean but
  surprising: destroying a note entity silently hangs up the note. If
  you want explicit control, use `C_MidiMessage` directly.
- **Ephemeral messages die every frame.** `C_Lifetime{1}` means a MIDI
  message is valid for exactly one UPDATE tick. If a system runs after
  the lifetime-decrement system, it will never see the message.
- **`C_MidiSequence` ticks are frame-dependent.** The BPM/timing fields
  are computed from `deltaTime(UPDATE)`, so slow frames slow down
  sequences. If you need sample-accurate timing, you'll need to rebuild
  the sequence on a wall-clock basis.
- **Trigger systems need *both* components.** `CONTACT_MIDI_TRIGGER`
  archetype-matches on `C_ContactEvent + C_MidiNote`. If either is
  missing, nothing fires.
- **Device creation is manual.** There is no
  "audio-device-manager system" yet — `entity_midi_device.hpp` is the
  only supported path.

## Deprecated

- `INPUT_MIDI_MESSAGE_IN` — hardware input is drained by `MidiIn::tick()`;
  synthetic input uses the port-aware `IRAudio::insertNoteOnMessage`,
  `insertNoteOffMessage`, or `insertCCMessage` functions. Tracked by #3792.
- `C_MidiSourcePort` — use the same port-aware insertion functions for
  synthetic input. Tracked by #3792.
