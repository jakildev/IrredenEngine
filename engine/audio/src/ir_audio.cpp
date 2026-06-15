#include <irreden/ir_audio.hpp>

#include <irreden/audio/audio_manager.hpp>

#include <utility>

namespace IRAudio {

AudioManager *g_audioManager = nullptr;
AudioManager &getAudioManager() {
    IR_ASSERT(g_audioManager != nullptr, "AudioManager not initialized");
    return *g_audioManager;
}

IAudioCaptureSource &getAudioCaptureSource() {
    return getAudioManager().getAudio();
}

std::vector<std::string> midiInPorts() {
    return getAudioManager().getMidiIn().getPortNames();
}

std::vector<std::string> midiOutPorts() {
    return getAudioManager().getMidiOut().getPortNames();
}

std::vector<int> midiInOpenPorts() {
    return getAudioManager().getMidiIn().getOpenPortIndices();
}

std::vector<int> midiOutOpenPorts() {
    return getAudioManager().getMidiOut().getOpenPortIndices();
}

int openPortMidiIn(MidiInInterfaces port) {
    return getAudioManager().getMidiIn().openPort(port);
}

int openPortMidiIn(const std::string &deviceName) {
    return getAudioManager().getMidiIn().openPort(deviceName);
}

int openPortMidiOut(MidiOutInterfaces port) {
    return getAudioManager().getMidiOut().openPort(port);
}

int openPortMidiOut(const std::string &deviceName) {
    return getAudioManager().getMidiOut().openPort(deviceName);
}

void sendMidiMessage(const std::vector<unsigned char> &message) {
    getAudioManager().getMidiOut().sendMessage(message);
}

void sendMidiMessage(int portIndex, const std::vector<unsigned char> &message) {
    getAudioManager().getMidiOut().sendMessage(portIndex, message);
}

CCData checkCCMessage(int channel, CCMessage ccMessage) {
    return getAudioManager().getMidiIn().checkCCMessageThisFrame(channel, ccMessage);
}

CCData checkCCMessage(int portIndex, MidiChannel channel, CCMessage ccMessage) {
    return getAudioManager().getMidiIn().checkCCMessageThisFrame(portIndex, channel, ccMessage);
}

// Move some of this to IRInput
const std::vector<IRComponents::C_MidiMessage> &getMidiNotesOnThisFrame(int channel) {
    return getAudioManager().getMidiIn().getMidiNotesOnThisFrame(channel);
}
const std::vector<IRComponents::C_MidiMessage> &getMidiNotesOffThisFrame(int channel) {
    return getAudioManager().getMidiIn().getMidiNotesOffThisFrame(channel);
}
const std::vector<IRComponents::C_MidiMessage> &
getMidiNotesOnThisFrame(int portIndex, MidiChannel channel) {
    return getAudioManager().getMidiIn().getMidiNotesOnThisFrame(portIndex, channel);
}
const std::vector<IRComponents::C_MidiMessage> &
getMidiNotesOffThisFrame(int portIndex, MidiChannel channel) {
    return getAudioManager().getMidiIn().getMidiNotesOffThisFrame(portIndex, channel);
}

void insertNoteOffMessage(MidiChannel channel, const IRComponents::C_MidiMessage &message) {
    getAudioManager().getMidiIn().insertNoteOffMessage(channel, message);
}
void insertNoteOnMessage(MidiChannel channel, const IRComponents::C_MidiMessage &message) {
    getAudioManager().getMidiIn().insertNoteOnMessage(channel, message);
}
void insertCCMessage(MidiChannel channel, const IRComponents::C_MidiMessage &message) {
    getAudioManager().getMidiIn().insertCCMessage(channel, message);
}

void insertNoteOffMessage(
    int portIndex, MidiChannel channel, const IRComponents::C_MidiMessage &message
) {
    getAudioManager().getMidiIn().insertNoteOffMessage(portIndex, channel, message);
}
void insertNoteOnMessage(
    int portIndex, MidiChannel channel, const IRComponents::C_MidiMessage &message
) {
    getAudioManager().getMidiIn().insertNoteOnMessage(portIndex, channel, message);
}
void insertCCMessage(
    int portIndex, MidiChannel channel, const IRComponents::C_MidiMessage &message
) {
    getAudioManager().getMidiIn().insertCCMessage(portIndex, channel, message);
}

bool startAudioInputCapture(
    const std::string &deviceName, int sampleRate, int channels, AudioInputCallback callback
) {
    Audio &audio = getAudioManager().getAudio();
    if (!audio.openStreamIn(deviceName, sampleRate, channels, std::move(callback))) {
        return false;
    }
    if (!audio.startStreamIn()) {
        audio.closeStreamIn();
        return false;
    }
    return true;
}

void stopAudioInputCapture() {
    getAudioManager().getAudio().closeStreamIn();
}

SoundHandle playSound(const std::string &path, AudioBus bus, float volume, bool loop) {
    return getAudioManager().getAudioPlayback().playSound(path, bus, volume, loop);
}

SoundHandle playMusic(const std::string &path, float volume, bool loop) {
    return getAudioManager().getAudioPlayback().playMusic(path, volume, loop);
}

SoundHandle playSoundAt(
    const std::string &path, AudioBus bus, const IRMath::vec3 &position, float volume, bool loop
) {
    return getAudioManager().getAudioPlayback().playSoundAt(path, bus, position, volume, loop);
}

void stopSound(SoundHandle handle) {
    getAudioManager().getAudioPlayback().stop(handle);
}

void setSoundVolume(SoundHandle handle, float volume) {
    getAudioManager().getAudioPlayback().setSoundVolume(handle, volume);
}

void fadeInSound(SoundHandle handle, unsigned int milliseconds) {
    getAudioManager().getAudioPlayback().fadeIn(handle, milliseconds);
}

void fadeOutSound(SoundHandle handle, unsigned int milliseconds) {
    getAudioManager().getAudioPlayback().fadeOut(handle, milliseconds);
}

void setBusVolume(AudioBus bus, float volume) {
    getAudioManager().getAudioPlayback().setBusVolume(bus, volume);
}

void setMasterVolume(float volume) {
    getAudioManager().getAudioPlayback().setMasterVolume(volume);
}

void setListenerPosition(const IRMath::vec3 &position) {
    getAudioManager().getAudioPlayback().setListenerPosition(position);
}

void tickAudioPlayback() {
    getAudioManager().getAudioPlayback().tickPlayback();
}

} // namespace IRAudio