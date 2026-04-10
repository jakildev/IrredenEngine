---
name: midi-scene-creator
description: >-
  Create MIDI scenes, sequences, and music-driven visualizations in Irreden
  Engine. Covers MIDI components, systems, outbound pipeline, contact/periodic
  triggers, Lua MIDI API, and music theory helpers. Use when the user wants to
  set up MIDI output, create musical sequences, build audio-reactive entities,
  or work with the audio/MIDI subsystem.
---

# MIDI Scene Creator

## Architecture

MIDI in Irreden flows through the ECS: components carry note/sequence data, systems route messages to hardware. The outbound path uses ephemeral entities destroyed after one frame.

```
C_MidiNote / C_MidiSequence (on game entities)
        |
        v
Trigger system (CONTACT_MIDI_TRIGGER, PERIODIC_IDLE_MIDI_TRIGGER, MIDI_SEQUENCE_OUT)
        |
        v
Ephemeral entity: C_MidiMessage + C_MidiOut + C_Lifetime{1}
        |
        v
OUTPUT_MIDI_MESSAGE_OUT system --> IRAudio::sendMidiMessage --> hardware
```

## MIDI Components

| Component | Location | Purpose |
|-----------|----------|---------|
| `C_MidiNote` | `audio/components/component_midi_note.hpp` | Note, velocity, channel, hold time. Sends NOTE_OFF on entity destroy. |
| `C_MidiSequence` | `audio/components/component_midi_sequence.hpp` | Tick-based timeline with BPM, time signature, loop. `insertNote()` adds events. |
| `C_MidiMessage` | `audio/components/component_midi_message.hpp` | Raw MIDI bytes (status, data1, data2). |
| `C_MidiDevice` | `audio/components/component_midi_device.hpp` | RtMidi port index. |
| `C_MidiChannel` | `audio/components/component_midi_channel.hpp` | Channel wrapper. |
| `C_MidiDelay` | `audio/components/component_midi_delay.hpp` | Frame countdown before forwarding a message. |

### C_MidiNote

```cpp
C_MidiNote(unsigned char note, unsigned char velocity,
           unsigned char channel = 0, float holdSeconds = 0.1f);
C_MidiNote(); // defaults: note=60, velocity=100, channel=0, hold=0.1s
```

Automatically sends NOTE_OFF via `onDestroy()` when the entity is removed.

### C_MidiSequence

```cpp
C_MidiSequence(float bpm, std::pair<int,int> timeSignature,
               int lengthMeasures, bool looping = true);
C_MidiSequence(float bpm, int tsNum, int tsDen, int lengthMeasures,
               bool looping = true);
```

Key API:
- `insertNote(double start, double holdDurationSeconds, unsigned char note, unsigned char velocity)` -- `start` is in measures (0.0 = beat 1 of measure 1, 1.0 = beat 1 of measure 2).
- `getNextMessage()` returns `std::optional<C_MidiMessage>` as the tick counter advances.
- `reset()` rewinds for looping.
- `kTicksPerWholeNote = 480 * 4` (resolution constant).

## MIDI Systems

Register these in your creation's `initSystems()` under the appropriate pipeline:

| System | Pipeline | Purpose |
|--------|----------|---------|
| `INPUT_MIDI_MESSAGE_IN` | INPUT | Routes inbound MIDI from hardware into `IRAudio` buffers |
| `OUTPUT_MIDI_MESSAGE_OUT` | UPDATE | Sends `C_MidiMessage` entities tagged `C_MidiOut` to hardware |
| `MIDI_SEQUENCE_OUT` | UPDATE | Advances `C_MidiSequence` tick counters, spawns outbound message entities |
| `MIDI_DELAY_PROCESS` | UPDATE | After countdown expires, spawns the delayed message as an outbound entity |
| `CONTACT_MIDI_TRIGGER` | UPDATE | On collision contact enter, fires NOTE_ON + schedules NOTE_OFF via delay |
| `PERIODIC_IDLE_MIDI_TRIGGER` | UPDATE | On periodic idle cycle completion, fires NOTE_ON + schedules NOTE_OFF |

**Minimal MIDI output pipeline:**

```cpp
IRSystem::registerPipeline(
    IRTime::Events::UPDATE,
    {// ... other update systems ...
     IRSystem::createSystem<IRSystem::MIDI_SEQUENCE_OUT>(),
     IRSystem::createSystem<IRSystem::MIDI_DELAY_PROCESS>(),
     IRSystem::createSystem<IRSystem::OUTPUT_MIDI_MESSAGE_OUT>(),
     // ... lifetime should come after so ephemeral entities get cleaned up ...
     IRSystem::createSystem<IRSystem::LIFETIME>()}
);
```

## Setting Up a MIDI Scene

### 1. Open a MIDI output port

**C++:**
```cpp
IRAudio::openPortMidiOut("OP-1");  // substring match on device name
```

**Lua:**
```lua
IRAudio.openMidiOut("OP-1")
```

### 2. Create entities with MIDI data

**Sequence-driven (timeline):**
```cpp
auto seq = C_MidiSequence(120.0f, {4, 4}, 4, true); // 120 BPM, 4/4, 4 measures, loop
seq.insertNote(0.0, 0.1, 60, 100);   // C4 at measure start
seq.insertNote(0.5, 0.1, 64, 100);   // E4 at half measure
seq.insertNote(1.0, 0.1, 67, 100);   // G4 at measure 2
IREntity::createEntity(seq);
```

**Contact-driven (physics interaction):**
```cpp
IREntity::createEntity(
    C_VoxelSetNew{...},
    C_MidiNote{60, 100, 0, 0.1f},  // plays when contact triggers
    C_Velocity3D{0, 0, -5.0f},
    C_ContactEvent{}
);
```

### 3. Delayed NOTE_OFF pattern

Trigger systems (CONTACT and PERIODIC_IDLE) use this pattern internally:
1. Immediately send NOTE_ON via an ephemeral `C_MidiMessage + C_MidiOut + C_Lifetime{1}` entity.
2. Create a second entity with `C_MidiMessage(NOTE_OFF) + C_MidiDelay{frames} + C_Lifetime{frames+1}`.
3. `MIDI_DELAY_PROCESS` counts down, then spawns the final outbound entity.

### 4. MIDI device entities (prefab)

```cpp
#include <irreden/audio/entities/entity_midi_device.hpp>

auto deviceId = IREntity::createPrefab<IREntity::PrefabTypes::kMidiDevice>(
    "OP-1", IRAudio::MidiDeviceType::MIDI_DEVICE_TYPE_OUT
);
```

## Lua API Reference

| Function | Description |
|----------|-------------|
| `IRAudio.openMidiOut(name)` | Open output port by device name substring |
| `IRAudio.openMidiIn(name)` | Open input port by device name substring |
| `IRAudio.rootNote(noteName, octave)` | Get MIDI note number from name + octave |
| `IRAudio.getScaleIntervals(scaleMode)` | Get interval table for a scale |
| `IRAudio.getScaleSize(scaleMode)` | Get number of notes in a scale |

**Lua enums:** `MidiNote`, `NoteName`, `ScaleMode` (registered in `lua_bindings.cpp`).

**Lua entity creation:**
```lua
local seq = C_MidiSequence.new(120, 4, 4, 2, true)
seq:insertNote(0.0, 0.1, 60, 100)
IREntity.createMidiSequence(seq)
```

## Reference Creation

`creations/demos/midi_polyrhythm/` is the canonical MIDI scene example:
- `settings.lua` configures device, voice MIDI params
- `voices.lua` maps scale notes to per-voice MIDI (note, velocity, hold, channel)
- `entities.lua` batch-creates voxel note blocks with `C_MidiNote`
- `CONTACT_MIDI_TRIGGER` fires on platform collisions
- Full pipeline: contact -> NOTE_ON -> delayed NOTE_OFF -> hardware

## Checklist

- [ ] MIDI output port opened (`IRAudio.openMidiOut` or `openPortMidiOut`)
- [ ] `OUTPUT_MIDI_MESSAGE_OUT` registered in UPDATE pipeline
- [ ] `MIDI_DELAY_PROCESS` registered if using contact/periodic triggers or delays
- [ ] `LIFETIME` registered after MIDI systems for ephemeral entity cleanup
- [ ] Entities have appropriate MIDI components (`C_MidiNote`, `C_MidiSequence`, etc.)
