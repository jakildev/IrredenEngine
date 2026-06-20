# Plan: audio: C++ ECS MIDI-out has no Lua-observable hook

- **Issue:** #1869
- **Model:** opus
- **Date:** 2026-06-19

## Scope

Give Lua a single hook that observes **all** outbound MIDI, including messages
emitted by the C++ ECS audio path (`CONTACT_MIDI_TRIGGER` / `MIDI_SEQUENCE_OUT`
/ `PERIODIC_IDLE_MIDI_TRIGGER` → `OUTPUT_MIDI_MESSAGE_OUT`, plus the
`C_MidiNote::onDestroy` NOTE_OFF). A Lua MIDI traffic monitor today is blind to
anything not sent through Lua.

## Verified current state (premise correction)

- No `IRAudio.sendCC` / `sendNoteOn` / `sendNoteOff` Lua functions exist in the
  engine. `lua_audio_bindings.hpp` (`detail::bindAudioApi`) binds only
  file-playback. The public `ir_audio.hpp` has only `sendMidiMessage` (no
  `sendCC`/`sendNoteOn`/`sendNoteOff`). The `engine/audio/CLAUDE.md` line that
  lists those three is doc drift. So the "decorate the Lua send functions"
  workaround is a creation-local pattern, not an engine seam.
- `IRAudio::sendMidiMessage(...)` is the sole outbound choke point. Exhaustive
  grep: the only call sites are `system_audio_midi_message_out.hpp:30` and
  `component_midi_note.hpp:34`; nothing calls `getMidiOut().sendMessage()`
  directly. One hook at `sendMidiMessage` captures 100% of outbound traffic.
- Issue direction 1 (callback at the send choke point) is the right design;
  direction 2 is moot (no Lua send notification point exists to converge onto).

## Approach

One task, one PR, single registerable observer at the C++ send choke point.

1. `engine/audio/include/irreden/ir_audio.hpp` — declare, next to
   `AudioInputCallback`:
   `using OutboundMidiCallback = std::function<void(const IRComponents::C_MidiMessage &message, int portIndex)>;`
   plus `void setOutboundMidiObserver(OutboundMidiCallback cb);` and
   `void clearOutboundMidiObserver();`.
2. `engine/audio/include/irreden/audio/audio_manager.hpp` — store
   `OutboundMidiCallback m_outboundMidiObserver;` with get/set accessors.
   (Storage home — NOT a TU-local static, per the lifetime gotcha.)
3. `engine/audio/src/ir_audio.cpp` — in BOTH `sendMidiMessage` overloads, after
   resolving the port, fire the observer if set, passing a `C_MidiMessage`
   rebuilt from the raw bytes (`C_MidiMessage(msg[0], msg[1], msg.size() > 2 ?
   msg[2] : 0)`) and the port index (`-1`/default for the no-port overload).
   Fire UNCONDITIONALLY of whether a hardware port is open (headless-monitor
   use case). Implement `set/clearOutboundMidiObserver` as forwards to
   `AudioManager`.
4. `engine/script/include/irreden/script/lua_audio_bindings.hpp` — in
   `bindAudioApi`:
   - `IRAudio.onMidiSent(fn)` registers a `sol::protected_function` wrapped in
     an `OutboundMidiCallback` calling `fn(status, channel, data1, data2,
     portIndex)` (scalars; no `C_MidiMessage` usertype exists, so pass parsed
     fields via `getStatusBits()`/`getChannelBits()`/`data1_`/`data2_`). Catch
     the result error and `IRE_LOG_ERROR` so a Lua handler error never aborts
     the send loop.
   - `IRAudio.clearMidiObserver()` → `IRAudio::clearOutboundMidiObserver()`.
   - Add an `IRAudio.MidiStatus` integer table
     (`NOTE_ON`/`NOTE_OFF`/`CONTROL_CHANGE`/`PROGRAM_CHANGE`/`PITCH_BEND`/
     `POLYPHONIC_KEY_PRESSURE`/`CHANNEL_PRESSURE`) bound from the
     `kMidiStatus_*` constants via an `IR_BIND_MIDI_STATUS` macro
     (cpp-lua-enums convention — Lua compares the status byte against named
     values, not magic numbers).
5. `test/script/lua_audio_midi_observer_test.cpp` (register in
   `test/CMakeLists.txt`) — register an `IRAudio.onMidiSent` observer from Lua,
   drive an outbound send through the C++ path, assert the observer captured
   the right status/channel/data1/data2. Headless-safe (observer fires with no
   open port).
6. `engine/audio/CLAUDE.md` — fix the stale "MIDI send: sendNoteOn, sendNoteOff,
   sendCC" line and document the observer + its thread/lifetime contract.

## Affected files

- `engine/audio/include/irreden/ir_audio.hpp` — callback type + set/clear decls.
- `engine/audio/include/irreden/audio/audio_manager.hpp` — observer storage.
- `engine/audio/src/ir_audio.cpp` — fire observer in both `sendMidiMessage`
  overloads; implement set/clear.
- `engine/script/include/irreden/script/lua_audio_bindings.hpp` —
  `IRAudio.onMidiSent` / `clearMidiObserver` / `MidiStatus` table.
- `test/script/lua_audio_midi_observer_test.cpp` — new headless test.
- `test/CMakeLists.txt` — register the new test.
- `engine/audio/CLAUDE.md` — fix drift + document the new seam.

## Acceptance criteria

- A Lua `IRAudio.onMidiSent(fn)` receives `fn(status, channel, data1, data2,
  portIndex)` for every outbound message, including C++-emitted ones and the
  `C_MidiNote` NOTE_OFF-on-destroy.
- Observer fires in headless runs with no MIDI output device.
- `IRAudio.MidiStatus.*` resolves to the matching `kMidiStatus_*` byte; both
  `sendMidiMessage` overloads notify.
- New headless test passes; `midi_multiport_test` / `audio_playback_test`
  unaffected.

## Gotchas

- **Lifetime / dangling sol::function.** Store the observer on `AudioManager`,
  not a TU-local static. In `world.hpp`, `m_lua` (line 46) is declared before
  `m_audioManager` (line 57), so members destruct in reverse → `m_audioManager`
  (and the wrapped `sol::function`) dies while `m_lua`'s `sol::state` is still
  alive, and every `sendMidiMessage` (including teardown NOTE_OFFs) is within
  `AudioManager`'s lifetime. Add a comment noting this order dependency so a
  future member reorder doesn't introduce UB. `clearOutboundMidiObserver()` is
  an explicit escape hatch.
- **Main-thread only.** `sendMidiMessage` runs on the main thread (UPDATE
  pipeline), unlike inbound `MidiIn` callbacks (driver thread). Fire the
  observer synchronously; do not move the send to a worker thread.
- **Two-byte messages.** PROGRAM_CHANGE / CHANNEL_PRESSURE serialize to 2 bytes;
  guard `data2` reconstruction with `msg.size() > 2`.
- **Single observer, last-registration-wins.** Sufficient for v1 (matches the
  `AudioInputCallback` precedent); multi-observer fanout is out of scope — Lua
  can multiplex internally.
- **#1727 (multi-port MIDI I/O, in-flight)** may reshape `sendMidiMessage`'s
  port argument; if it merges first, rebase the observer onto the final
  signature. Orthogonal, not a hard blocker.
