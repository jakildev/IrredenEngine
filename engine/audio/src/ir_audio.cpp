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

namespace {

// Rebuilds a C_MidiMessage from the raw RtMidi wire bytes and fires the
// outbound observer if one is registered. Shared by both sendMidiMessage
// overloads so the byte unpack lives in one place. Each index is guarded:
// 1-byte system real-time bytes (e.g. clock 0xF8) and 2-byte messages
// (PROGRAM_CHANGE / CHANNEL_PRESSURE) must not read past the vector.
// portIndex is -1 for the default-port send.
void notifyOutboundObserver(
    AudioManager &manager, const std::vector<unsigned char> &message, int portIndex
) {
    const unsigned char status = message.empty() ? 0 : message[0];
    const unsigned char data1 = message.size() > 1 ? message[1] : 0;
    const unsigned char data2 = message.size() > 2 ? message[2] : 0;
    manager.fireOutboundMidiObserver(IRComponents::C_MidiMessage{status, data1, data2}, portIndex);
}

} // namespace

void sendMidiMessage(const std::vector<unsigned char> &message) {
    AudioManager &manager = getAudioManager();
    // Fire before the hardware send so a headless monitor observes the message
    // even with no output port open (the send below then no-ops).
    notifyOutboundObserver(manager, message, -1);
    manager.getMidiOut().sendMessage(message);
}

void sendMidiMessage(int portIndex, const std::vector<unsigned char> &message) {
    AudioManager &manager = getAudioManager();
    notifyOutboundObserver(manager, message, portIndex);
    manager.getMidiOut().sendMessage(portIndex, message);
}

void setOutboundMidiObserver(OutboundMidiCallback callback) {
    getAudioManager().setOutboundMidiObserver(std::move(callback));
}

void clearOutboundMidiObserver() {
    getAudioManager().clearOutboundMidiObserver();
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