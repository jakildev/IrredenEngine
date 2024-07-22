/*
 * Project: Irreden Engine
 * File: ir_audio.cpp
 * Author: Evin Killian jakildev@gmail.com
 * Created Date: October 2023
 * -----
 * Modified By: <your_name> <Month> <YYYY>
 */

#include <irreden/ir_audio.hpp>

#include <irreden/audio/audio_manager.hpp>

namespace IRAudio {

    AudioManager* g_audioManager = nullptr;
    AudioManager& getAudioManager() {
        IR_ASSERT(
            g_audioManager != nullptr,
            "AudioManager not initialized"
        );
        return *g_audioManager;
    }

    int openPortMidiIn(MidiInInterfaces port) {
        return getAudioManager().getMidiIn().openPort(port);
    }

    int openPortMidiIn(const std::string& deviceName) {
        return getAudioManager().getMidiIn().openPort(deviceName);
    }

    int openPortMidiOut(MidiOutInterfaces port) {
        return getAudioManager().getMidiOut().openPort(port);
    }

    int openPortMidiOut(const std::string& deviceName) {
        return getAudioManager().getMidiOut().openPort(deviceName);
    }

    void sendMidiMessage(const std::vector<unsigned char>& message) {
        getAudioManager().getMidiOut().sendMessage(message);
    }

    CCData checkCCMessage(int device, CCMessage ccMessage) {
        return getAudioManager().getMidiIn().
            checkCCMessageThisFrame(device, ccMessage);
    }

    // Move some of this to IRInput
    const std::vector<IRComponents::C_MidiMessage>& getMidiNotesOnThisFrame(
        int device
    )
    {
        return getAudioManager().getMidiIn().
            getMidiNotesOnThisFrame(device);
    }
    const std::vector<IRComponents::C_MidiMessage>& getMidiNotesOffThisFrame(
        int device
    )
    {
        return getAudioManager().getMidiIn().
            getMidiNotesOffThisFrame(device);
    }

    void insertNoteOffMessage(
        MidiChannel channel,
        const IRComponents::C_MidiMessage& message
    )
    {
        getAudioManager().getMidiIn().
            insertNoteOffMessage(channel, message);
    }
    void insertNoteOnMessage(
        MidiChannel channel,
        const IRComponents::C_MidiMessage& message
    )
    {
        getAudioManager().getMidiIn().
            insertNoteOnMessage(channel, message);
    }
    void insertCCMessage(
        MidiChannel channel,
        const IRComponents::C_MidiMessage& message
    )
    {
        getAudioManager().getMidiIn().
            insertCCMessage(channel, message);
    }



} // namespace IRAudio