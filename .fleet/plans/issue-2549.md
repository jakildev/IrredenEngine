## Plan: MIDI client init failure degrades to no-MIDI

- **Issue:** #2549
- **Model:** sonnet
- **Date:** 2026-07-24

### Verified current state (confirmed repro)

`MidiIn::MidiIn()` (`engine/audio/src/midi_in.cpp:15-27`) and `MidiOut::MidiOut()`
(`engine/audio/src/midi_out.cpp:5-17`) each hold a **by-value** RtMidi
enumeration probe (`RtMidiIn m_rtMidiIn` at `midi_in.hpp:80`, `RtMidiOut
m_rtMidiOut` at `midi_out.hpp:46`, both commented "enumeration probe only").
`AudioManager` holds `MidiIn`/`MidiOut` by value (`audio_manager.hpp:57-60`),
constructed with `World`. When the CoreMIDI client can't be created (repro: a
sandboxed shell denying the `com.apple.midiserver` Mach lookup —
`IRShapeDebug` aborts with `MidiInCore::initialize: error creating OS-X MIDI
client object (-304)` → uncaught `RtMidiError` → SIGABRT, RESULT=CRASH), the
throw escapes the member initializer list before any catch could run. The
per-port handles are already `unique_ptr` (`MidiInPort::rtMidiIn_`,
`MidiOutPort::rtMidiOut_`), constructed in `openPort` (`midi_in.cpp:75-80`,
`midi_out.cpp` equivalent) — those construction/open calls can throw the
same way on a degraded host. `AudioPlayback` already implements the module's
"no device = silent, never a crash" doctrine (engine/audio/CLAUDE.md); MIDI
is the gap.

### Affected files

- `engine/audio/include/irreden/audio/midi_in.hpp` — `m_rtMidiIn` becomes
  `std::unique_ptr<RtMidiIn>`.
- `engine/audio/src/midi_in.cpp` — ctor try/catch around probe construction
  + enumeration (degraded ⇒ `m_numberPorts = 0`, one `IRE_LOG_WARN` naming
  `RtMidiError::getMessage()`); `openPort(substring)` wraps the
  `make_unique<RtMidiIn>()` + `openPort(i)` + `setCallback` triple in
  try/catch (warn + return -1).
- `engine/audio/include/irreden/audio/midi_out.hpp` /
  `src/midi_out.cpp` — same treatment for `m_rtMidiOut` and
  `MidiOut::openPort`; `sendAllNotesOff`/`sendMessage` already no-op on
  empty `m_ports`.
- Check `patches/` for local RtMidi patches before assuming stock throw
  behavior (engine/audio/CLAUDE.md gotcha).

### Approach

Mirror the AudioPlayback failed-init pattern: construct the enumeration
probes inside try/catch in the ctor body (member becomes a null
`unique_ptr` on failure), keep `m_numberPorts`/`m_portNames` empty in the
degraded state, and guard every `m_rtMidi*` dereference (`getPortCount`,
`getPortName`) on the pointer. All query/tick paths already tolerate zero
ports (`m_ports` empty ⇒ loops no-op), so no consumer changes. No behavior
change on healthy hosts: same enumeration, same logs, same port handles.

### Acceptance criteria

As on the issue: (1) no throw escapes construction — degraded state is 0
ports / warn-once / no-op sends; (2) an engine demo reaches RESULT=CLEAN in
a MIDI-denied environment (repro above); (3) healthy-host behavior
unchanged (ports enumerate + open, MIDI demos work); (4) one WARN log names
the RtMidi error.

### Verification

`fleet-build --target IRShapeDebug`; run `IRShapeDebug --auto-screenshot 10`
in a sandboxed (MIDI-denied) shell → RESULT=CLEAN; run once normally and
confirm the MIDI enumeration log lines still appear; build + run one
MIDI-using demo (e.g. the MIDI macro creation) on a healthy host for the
no-regression check.
