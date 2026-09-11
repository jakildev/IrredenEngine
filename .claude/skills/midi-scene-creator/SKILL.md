---
name: midi-scene-creator
description: >-
  Creates MIDI scenes, sequences, and music-driven visualisations in Irreden
  Engine — the MIDI components and systems, the outbound message pipeline,
  contact and periodic triggers, the Lua MIDI API, and the music-theory
  helpers. Use when the user wants to set up MIDI output, create musical
  sequences, build audio-reactive entities, or work with the audio/MIDI
  subsystem.
---

# MIDI Scene Creator

MIDI flows through the ECS: components carry note / sequence data, systems
route messages to hardware through ephemeral entities that live one frame.
Component catalog, entity builders, and gotchas:
[`engine/prefabs/irreden/audio/CLAUDE.md`](../../../engine/prefabs/irreden/audio/CLAUDE.md).

```
C_MidiNote / C_MidiSequence (on game entities)
        |
Trigger system (CONTACT_MIDI_TRIGGER, PERIODIC_IDLE_MIDI_TRIGGER, MIDI_SEQUENCE_OUT)
        |
Ephemeral entity: C_MidiMessage + C_MidiOut + C_Lifetime{1}
        |
OUTPUT_MIDI_MESSAGE_OUT --> IRAudio::sendMidiMessage --> hardware
```

## Systems

| System | Pipeline | Purpose |
|---|---|---|
| `INPUT_MIDI_MESSAGE_IN` | INPUT | Routes inbound MIDI from hardware into `IRAudio` buffers |
| `OUTPUT_MIDI_MESSAGE_OUT` | UPDATE | Sends `C_MidiMessage` entities tagged `C_MidiOut` to hardware |
| `MIDI_SEQUENCE_OUT` | UPDATE | Advances `C_MidiSequence` tick counters, spawns outbound message entities |
| `MIDI_DELAY_PROCESS` | UPDATE | Spawns a delayed message as an outbound entity when its countdown expires |
| `CONTACT_MIDI_TRIGGER` | UPDATE | On contact enter, fires NOTE_ON and schedules NOTE_OFF via delay |
| `PERIODIC_IDLE_MIDI_TRIGGER` | UPDATE | On periodic-idle cycle completion, fires NOTE_ON and schedules NOTE_OFF |

Minimal output pipeline — `LIFETIME` last, so ephemeral message entities are
reaped after they send (pipeline order:
[`engine/system/CLAUDE.md`](../../../engine/system/CLAUDE.md) §Pipelines):

```cpp
IRSystem::registerPipeline(
    IRTime::Events::UPDATE,
    {// ... other update systems ...
     IRSystem::createSystem<IRSystem::MIDI_SEQUENCE_OUT>(),
     IRSystem::createSystem<IRSystem::MIDI_DELAY_PROCESS>(),
     IRSystem::createSystem<IRSystem::OUTPUT_MIDI_MESSAGE_OUT>(),
     IRSystem::createSystem<IRSystem::LIFETIME>()}
);
```

## Scene setup

1. **Open a port** (substring match on the device name): C++
   `IRAudio::openPortMidiOut("OP-1");`, Lua `IRAudio.openMidiOut("OP-1")`.
2. **Entities.** Sequence-driven — `C_MidiSequence(bpm, {num, den},
   lengthMeasures, looping = true)` (or `(bpm, num, den, lengthMeasures,
   looping)`); `insertNote(startMeasures, holdSeconds, note, velocity)` with
   `0.0` = beat 1 of measure 1 and `1.0` = beat 1 of measure 2;
   `getNextMessage()` yields `std::optional<C_MidiMessage>` as ticks advance;
   `reset()` rewinds; resolution `kTicksPerWholeNote = 480 * 4`:

   ```cpp
   auto seq = C_MidiSequence(120.0f, {4, 4}, 4, true); // 120 BPM, 4/4, 4 measures, loop
   seq.insertNote(0.0, 0.1, 60, 100);   // C4 at measure start
   seq.insertNote(0.5, 0.1, 64, 100);   // E4 at half measure
   IREntity::createEntity(seq);
   ```

   Contact-driven — `C_MidiNote(note, velocity, channel = 0, holdSeconds =
   0.1f)` (default `60, 100, 0, 0.1s`) beside `C_ContactEvent{}`:

   ```cpp
   IREntity::createEntity(
       C_VoxelSetNew{...},
       C_MidiNote{60, 100, 0, 0.1f},
       C_Velocity3D{0, 0, -5.0f},
       C_ContactEvent{}
   );
   ```

3. **Delayed NOTE_OFF** (what the trigger systems do internally): NOTE_ON as
   an ephemeral `C_MidiMessage + C_MidiOut + C_Lifetime{1}` entity; NOTE_OFF
   as `C_MidiMessage(NOTE_OFF) + C_MidiDelay{frames} + C_Lifetime{frames+1}`,
   which `MIDI_DELAY_PROCESS` turns into the outbound entity when the countdown
   ends. `C_MidiNote::onDestroy()` also sends NOTE_OFF.
4. **Device entity** (prefab):

   ```cpp
   #include <irreden/audio/entities/entity_midi_device.hpp>

   auto deviceId = IREntity::createPrefab<IREntity::PrefabTypes::kMidiDevice>(
       "OP-1", IRAudio::MidiDeviceType::MIDI_DEVICE_TYPE_OUT
   );
   ```

## Lua API

| Function | Description |
|---|---|
| `IRAudio.openMidiOut(name)` / `IRAudio.openMidiIn(name)` | Open a port by device-name substring |
| `IRAudio.rootNote(noteName, octave)` | MIDI note number from name + octave |
| `IRAudio.getScaleIntervals(scaleMode)` | Interval table for a scale |
| `IRAudio.getScaleSize(scaleMode)` | Note count of a scale |

Enums `MidiNote`, `NoteName`, `ScaleMode` are registered in the creation's
`lua_bindings.cpp`. Entity creation:

```lua
local seq = C_MidiSequence.new(120, 4, 4, 2, true)
seq:insertNote(0.0, 0.1, 60, 100)
IREntity.createMidiSequence(seq)
```

Structural reference (bindings layout, component pack, script staging):
`creations/demos/default/`.

## Done when

- A MIDI output port is open.
- `OUTPUT_MIDI_MESSAGE_OUT` is in the UPDATE pipeline, `MIDI_DELAY_PROCESS`
  too when triggers or delays are used, and `LIFETIME` follows the MIDI
  systems.
- Entities carry the MIDI components they need (`C_MidiNote`,
  `C_MidiSequence`, ...).
