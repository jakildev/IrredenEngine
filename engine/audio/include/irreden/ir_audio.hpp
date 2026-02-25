#ifndef IR_AUDIO_H
#define IR_AUDIO_H

#include <irreden/audio/ir_audio_types.hpp>

#include <irreden/audio/components/component_midi_message.hpp>

#include <functional>
#include <vector>
#include <string>

namespace IRAudio {

class AudioManager;
extern AudioManager *g_audioManager;
AudioManager &getAudioManager();
using AudioInputCallback = std::function<void(const float *, int, double, bool)>;

int openPortMidiIn(MidiInInterfaces midiInInterface);
int openPortMidiIn(const std::string &deviceName);
int openPortMidiOut(MidiOutInterfaces midiOutInterface);
int openPortMidiOut(const std::string &midiOutInterface);
void sendMidiMessage(const std::vector<unsigned char> &message);

CCData checkCCMessage(int device, CCMessage ccMessage);
const std::vector<IRComponents::C_MidiMessage> &getMidiNotesOnThisFrame(int device);
const std::vector<IRComponents::C_MidiMessage> &getMidiNotesOffThisFrame(int device);

void insertNoteOffMessage(MidiChannel channel, const IRComponents::C_MidiMessage &message);
void insertNoteOnMessage(MidiChannel channel, const IRComponents::C_MidiMessage &message);
void insertCCMessage(MidiChannel channel, const IRComponents::C_MidiMessage &message);

bool startAudioInputCapture(
    const std::string &deviceName,
    int sampleRate,
    int channels,
    AudioInputCallback callback
);
void stopAudioInputCapture();

} // namespace IRAudio

#endif /* IR_AUDIO_H */
