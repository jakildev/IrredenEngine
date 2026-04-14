# engine/audio/ — MIDI I/O and audio capture

Thin wrapper around RtMidi (MIDI in/out) and RtAudio (input capture). There
is no synth, no music playback — creations that need sound generate it
themselves, or pipe MIDI to an external hardware/software synth.

## Entry point

`engine/audio/include/irreden/ir_audio.hpp` — exposes `IRAudio::` free
functions:

- `getAudioManager()`, `getMidiIn()`, `getMidiOut()`, `getAudio()`.
- MIDI device enumeration: list ports, open by index or substring.
- MIDI query: per-channel, per-frame CC/note-on/note-off queues.
- MIDI send: `sendNoteOn`, `sendNoteOff`, `sendCC`.
- Audio input: open/start/stop a capture stream, register a float-sample
  callback.

## AudioManager owns

- **`MidiIn`** — one `RtMidiIn`, populates per-channel maps of
  `MidiMessage`s each `tick()`. Cleared and refilled every frame.
- **`MidiOut`** — one `RtMidiOut`, fire-and-forget send.
- **`Audio`** — `RtAudio` wrapper for microphone/line-in. Implements
  `IAudioCaptureSource`. Used by `VideoManager` for recording soundtracks.

## MIDI model

- **`MidiChannel`** is the map key. `MidiIn` exposes
  `ccMessagesThisFrame(channel)`, `noteOnsThisFrame(channel)`, etc.
- `MidiIn::tick()` is called by the INPUT pipeline; it drains the RtMidi
  callback queue into per-frame containers. **State is cleared every
  frame** — no history. Systems must read during their tick or they miss
  the event.
- Device names are substring-matched at `openPort()` time, with hardcoded
  fallback patterns for common devices (UMC1820, Focusrite, MPKmini2,
  OP-1). Edit `midi_in.cpp` / `midi_out.cpp` to add a new hardware match.

## Audio capture model

- `Audio::openStreamIn(sampleRate, channels, framesPerBuffer)` →
  `startStreamIn(callback)`.
- Callback signature:
  `void(const float* samples, int frameCount, double streamTime, bool overflow)`.
- Default buffer is 1024 frames.
- The callback is invoked on RtAudio's audio thread — **do not touch ECS
  or Lua state from inside it**. Copy samples into a lock-free buffer and
  consume on the main thread.

## Key components (prefabs/irreden/audio)

- `C_MidiMessage` — status + data1 + data2. Unpacks channel/status.
- `C_MidiNote`, `C_MidiSequence` — higher-level note/sequence types (with
  Lua bindings).
- `C_MidiDevice`, `C_MidiChannel`, `C_MidiDelay` — tag components.
- `C_AudioFile` — placeholder, minimal.

Plus `*_lua.hpp` variants for sequences and notes.

## Gotchas

- **No hot-swap.** If a MIDI device is unplugged mid-run, the `RtMidiIn`
  callback stops firing silently. There is no reconnection logic.
- **Per-frame queue wipe.** Query MIDI state only in systems that run
  after `MidiIn::tick()` and before the next one. Holding a reference
  across frames is UB.
- **Callback lifetime.** The input callback is a `std::function`.
  Destroying `Audio` while a stream is running can crash; always
  `stopStreamIn()` before teardown.
- **Custom RtMidi/RtAudio patches.** Don't assume stock upstream behavior
  — check `patches/` before debugging driver-level issues.
- **Single-thread `tick()` assumption.** `MidiIn::tick()` has no mutex.
  Call it only from the main loop.
