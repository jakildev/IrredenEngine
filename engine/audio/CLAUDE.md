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

- **`MidiIn`** — one `RtMidiIn` per simultaneously-open input port, each
  with its own callback queue. `tick()` drains every port's queue into a
  `MidiInputFrameBuffer` (cleared and refilled each frame) that keeps both a
  merged all-ports per-channel view and a per-port view.
- **`MidiOut`** — one `RtMidiOut` per open output port; fire-and-forget send
  to the default (first-opened) port, or to a specific port by index.
- **`Audio`** — `RtAudio` wrapper for microphone/line-in. Implements
  `IAudioCaptureSource`. Used by `VideoManager` for recording soundtracks.

## MIDI model

- **`MidiChannel`** is the map key. Query `IRAudio::checkCCMessage(channel,
  cc)` / `getMidiNotesOnThisFrame(channel)` for the **merged** all-ports
  view, or the port-scoped `checkCCMessage(portIndex, channel, cc)` /
  `getMidiNotesOnThisFrame(portIndex, channel)` to disambiguate same-channel
  traffic across ports. `openPortMidiIn`/`openPortMidiOut` return the RtMidi
  port index — the stable handle used as the source-port id, the
  `sendMidiMessage(portIndex, …)` target, and the per-port query key.
  `midiInOpenPorts()` / `midiOutOpenPorts()` enumerate the open handles.
- Inbound message entities carry `C_MidiSourcePort{portIndex}`; the
  `InputMidiMessageIn` system reads it to route each message into both the
  merged and per-port views. **Merged CC collapses same-channel+cc traffic
  (last write wins)** — use the per-port query when two devices share a
  channel.
- `MidiIn::tick()` is called by the INPUT pipeline; it drains every open
  port's RtMidi callback queue into the per-frame buffer. **State is cleared
  every frame** — no history. Systems must read during their tick or they
  miss the event.
- Device names are substring-matched at `openPort()` time, with hardcoded
  fallback patterns for common devices (UMC1820, Focusrite, MPKmini2,
  OP-1). Edit `midi_in.cpp` / `midi_out.cpp` to add a new hardware match.
  Opening an already-open port is a no-op that returns the existing handle.

## Audio capture model

- `Audio::openStreamIn(sampleRate, channels, framesPerBuffer)` →
  `startStreamIn(callback)`.
- Callback signature:
  `void(const float* samples, int frameCount, double streamTime, bool overflow)`.
- Default buffer is 1024 frames.
- The callback is invoked on RtAudio's audio thread — **do not touch ECS
  or Lua state from inside it**. Copy samples into a lock-free buffer and
  consume on the main thread.

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
- **Callback log level.** Use `IRE_LOG_DEBUG`, never `IRE_LOG_INFO`, for
  per-message / per-event logging inside the RtMidi/RtAudio callbacks. They
  fire on the driver thread hundreds of times per second under a real-time
  clock (24 pps) or dense CC sweeps — `IRE_LOG_INFO` per message floods the
  log (and costs frame time) while staying invisible in sparse-traffic dev.
