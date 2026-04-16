# engine/prefabs/irreden/audio/ — MIDI messages and sequences

MIDI composition, sequencing, contact/idle triggers, and device wiring.
Everything here is ECS-driven — there is no standalone audio "engine". A
creation composes MIDI behavior by adding components to entities.

## Key components

- `C_MidiMessage` — status + channel + data1/data2. Helpers for note
  number, velocity, CC value.
- `C_MidiSequence` — BPM, time signature, measures, tick-based playback,
  loop flag, message buffer.
- `C_MidiNote` — note number, velocity, channel, hold duration.
  **`onDestroy()` sends `NOTE_OFF`** — a free cleanup when an entity dies.
- `C_MidiDevice` — wraps an RtMidi port index (-1 = uninitialized). Pair
  with `C_MidiIn` / `C_MidiOut` tags.
- `C_MidiChannel` — 0–15 wrapper. `normalizeMidiChannel()` converts to
  MIDI wire format.
- `C_MidiDelay` — countdown timer for scheduling a delayed `NOTE_OFF`.
- `C_ContactEvent` — tag for entities whose collision events should trigger
  MIDI.

Many of these have `_lua.hpp` siblings that bind them into Lua.

## Key systems

- `MIDI_SEQUENCE_OUT` (UPDATE pipeline) — ticks a `C_MidiSequence` each
  frame, emits queued messages as ephemeral entities
  (`C_MidiMessage + C_MidiOut + C_Lifetime{1}`). Handles looping.
- `CONTACT_MIDI_TRIGGER` (UPDATE pipeline) — on `C_ContactEvent`
  enter, fires `NOTE_ON` and schedules `NOTE_OFF` via `C_MidiDelay`.
- `PERIODIC_IDLE_MIDI_TRIGGER` — fires `NOTE_ON` on `C_PeriodicIdle`
  cycle completion.
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
