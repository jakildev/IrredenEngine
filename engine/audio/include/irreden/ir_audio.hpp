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

/// Opens a MIDI input port by hardcoded interface index.
int openPortMidiIn(MidiInInterfaces midiInInterface);
/// Opens the first MIDI input port whose name contains @p deviceName (substring match).
int openPortMidiIn(const std::string &deviceName);
/// Opens a MIDI output port by hardcoded interface index.
int openPortMidiOut(MidiOutInterfaces midiOutInterface);
/// Opens the first MIDI output port whose name contains @p midiOutInterface (substring match).
int openPortMidiOut(const std::string &midiOutInterface);
/// Sends a raw MIDI message (fire-and-forget via `RtMidiOut`).
void sendMidiMessage(const std::vector<unsigned char> &message);

/// Returns the CC value for @p ccMessage on @p device received this frame,
/// or @ref kCCFalse if no CC message arrived.
CCData checkCCMessage(int device, CCMessage ccMessage);
/// Returns the list of note-on messages received on @p device this frame.
/// The list is cleared on the next `MidiIn::tick()` — read during INPUT/UPDATE only.
const std::vector<IRComponents::C_MidiMessage> &getMidiNotesOnThisFrame(int device);
/// Returns the list of note-off messages received on @p device this frame.
const std::vector<IRComponents::C_MidiMessage> &getMidiNotesOffThisFrame(int device);

/// @name Low-level MIDI message insertion (used by prefab systems)
/// @{
void insertNoteOffMessage(MidiChannel channel, const IRComponents::C_MidiMessage &message);
void insertNoteOnMessage(MidiChannel channel, const IRComponents::C_MidiMessage &message);
void insertCCMessage(MidiChannel channel, const IRComponents::C_MidiMessage &message);
/// @}

/// Opens and starts an RtAudio input capture stream.
/// @p callback is invoked on the RtAudio thread — copy data out before returning.
/// Returns `false` if the device could not be opened.
bool startAudioInputCapture(
    const std::string &deviceName,
    int sampleRate,
    int channels,
    AudioInputCallback callback
);
/// Stops the active RtAudio input stream.  Always call before `AudioManager` teardown.
void stopAudioInputCapture();

} // namespace IRAudio

#endif /* IR_AUDIO_H */
