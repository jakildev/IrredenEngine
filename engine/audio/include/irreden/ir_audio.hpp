#ifndef IR_AUDIO_H
#define IR_AUDIO_H

#include <irreden/audio/ir_audio_types.hpp>
#include <irreden/audio/audio_capture_source.hpp>

#include <irreden/audio/components/component_midi_message.hpp>

#include <functional>
#include <vector>
#include <string>

namespace IRAudio {

class AudioManager;
/// Global pointer to the active `AudioManager`; managed by the engine runtime.
/// Prefer @ref getAudioManager() for safe access.
extern AudioManager *g_audioManager;
/// Returns a reference to the active `AudioManager`. Asserts if not initialised.
AudioManager &getAudioManager();

/// RtAudio input callback type: `void(samples, frameCount, streamTime, overflow)`.
/// Invoked on the RtAudio thread — **do not** access ECS or Lua state from inside it.
using AudioInputCallback = std::function<void(const float *, int, double, bool)>;

/// Returns the active `IAudioCaptureSource` (used by `VideoManager` for recording).
IAudioCaptureSource &getAudioCaptureSource();

/// Returns the names of all MIDI input ports discovered at startup.
std::vector<std::string> midiInPorts();
/// Returns the names of all MIDI output ports discovered at startup.
std::vector<std::string> midiOutPorts();
/// Returns the RtMidi port indices of every currently-open MIDI input port,
/// in open order — lets a per-port consumer enumerate its active lanes.
std::vector<int> midiInOpenPorts();
/// Returns the RtMidi port indices of every currently-open MIDI output port.
std::vector<int> midiOutOpenPorts();
/// Opens a MIDI input port by hardcoded interface index.
int openPortMidiIn(MidiInInterfaces midiInInterface);
/// Opens the first MIDI input port whose name contains @p deviceName (substring match).
/// Returns the port index on success, or -1 if no port matches (logs a warning).
int openPortMidiIn(const std::string &deviceName);
/// Opens a MIDI output port by hardcoded interface index.
int openPortMidiOut(MidiOutInterfaces midiOutInterface);
/// Opens the first MIDI output port whose name contains @p deviceName (substring match).
/// Returns the port index on success, or -1 if no port matches (logs a warning).
int openPortMidiOut(const std::string &deviceName);
/// Sends a raw MIDI message to the default output port (the first one opened).
void sendMidiMessage(const std::vector<unsigned char> &message);
/// Sends a raw MIDI message to a specific open output port by its RtMidi port
/// index (the handle returned by @ref openPortMidiOut). Falls back to a warning
/// if the port isn't open.
void sendMidiMessage(int portIndex, const std::vector<unsigned char> &message);

/// Returns the CC value for @p ccMessage on @p channel received this frame
/// across all open input ports, or @ref kCCFalse if no CC message arrived.
CCData checkCCMessage(int channel, CCMessage ccMessage);
/// Per-port variant: CC value for @p ccMessage on @p channel received on the
/// input port @p portIndex this frame. Use to disambiguate same-channel CC
/// traffic across simultaneously-open ports.
CCData checkCCMessage(int portIndex, MidiChannel channel, CCMessage ccMessage);
/// Returns the note-on messages received on @p channel this frame across all
/// open input ports. The list is cleared on the next `MidiIn::tick()` — read
/// during INPUT/UPDATE only.
const std::vector<IRComponents::C_MidiMessage> &getMidiNotesOnThisFrame(int channel);
/// Returns the note-off messages received on @p channel this frame (all ports).
const std::vector<IRComponents::C_MidiMessage> &getMidiNotesOffThisFrame(int channel);
/// Per-port variants: note-on / note-off messages received on @p portIndex /
/// @p channel this frame.
const std::vector<IRComponents::C_MidiMessage> &
getMidiNotesOnThisFrame(int portIndex, MidiChannel channel);
const std::vector<IRComponents::C_MidiMessage> &
getMidiNotesOffThisFrame(int portIndex, MidiChannel channel);

/// @name Low-level MIDI message insertion (used by prefab systems)
/// @{
void insertNoteOffMessage(MidiChannel channel, const IRComponents::C_MidiMessage &message);
void insertNoteOnMessage(MidiChannel channel, const IRComponents::C_MidiMessage &message);
void insertCCMessage(MidiChannel channel, const IRComponents::C_MidiMessage &message);
/// Port-aware variants — fold into both the merged and the per-port views.
void insertNoteOffMessage(
    int portIndex, MidiChannel channel, const IRComponents::C_MidiMessage &message
);
void insertNoteOnMessage(
    int portIndex, MidiChannel channel, const IRComponents::C_MidiMessage &message
);
void insertCCMessage(
    int portIndex, MidiChannel channel, const IRComponents::C_MidiMessage &message
);
/// @}

/// Opens and starts an RtAudio input capture stream.
/// @p callback is invoked on the RtAudio thread — copy data out before returning.
/// Returns `false` if the device could not be opened.
bool startAudioInputCapture(
    const std::string &deviceName, int sampleRate, int channels, AudioInputCallback callback
);
/// Stops the active RtAudio input stream.  Always call before `AudioManager` teardown.
void stopAudioInputCapture();

} // namespace IRAudio

#endif /* IR_AUDIO_H */
