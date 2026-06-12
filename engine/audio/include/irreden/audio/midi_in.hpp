#ifndef MIDI_IN_H
#define MIDI_IN_H

#include <irreden/ir_profile.hpp>

#include <irreden/audio/ir_audio_types.hpp>
#include <irreden/audio/midi_input_frame_buffer.hpp>
#include <irreden/audio/components/component_midi_message.hpp>

#include <RtMidi.h>

#include <memory>
#include <queue>
#include <string>
#include <vector>

using IRComponents::C_MidiMessage;

namespace IRAudio {

// One simultaneously-open MIDI input port. Each port owns its own RtMidiIn and
// a single-producer (its RtMidi callback thread) / single-consumer (the main
// thread, in processMidiMessageQueue) message queue, so two devices can feed
// the engine at once without one openPort replacing another.
//
// rtMidiIn_ is declared last so it destructs first: closing the port stops the
// callback thread before queue_ (its write target) is torn down.
struct MidiInPort {
    std::queue<C_MidiMessage> queue_;
    int portIndex_;
    std::string name_;
    std::unique_ptr<RtMidiIn> rtMidiIn_;
};

class MidiIn {
  public:
    MidiIn();
    ~MidiIn();

    void tick();

    // Opens an input port by hardcoded interface enum or by device-name
    // substring. Returns the RtMidi port index (used as the stable port
    // handle / source-port id) or -1 if no port matches. Opening an
    // already-open port is a no-op that returns the existing handle.
    int openPort(MidiInInterfaces midiInInterface);
    int openPort(const std::string &deviceName);

    const std::vector<std::string> &getPortNames() const;
    // RtMidi indices of every currently-open input port (one per device),
    // in open order — lets a per-port consumer enumerate its lanes.
    std::vector<int> getOpenPortIndices() const;

    void processMidiMessageQueue();

    // Merged queries (all open ports folded together) — the back-compat
    // surface for consumers that don't disambiguate by port.
    CCData checkCCMessageThisFrame(MidiChannel channel, CCMessage ccNumber) const;
    const std::vector<C_MidiMessage> &getMidiNotesOnThisFrame(MidiChannel channel) const;
    const std::vector<C_MidiMessage> &getMidiNotesOffThisFrame(MidiChannel channel) const;

    // Per-port queries — one device's traffic in isolation.
    CCData checkCCMessageThisFrame(int portIndex, MidiChannel channel, CCMessage ccNumber) const;
    const std::vector<C_MidiMessage> &
    getMidiNotesOnThisFrame(int portIndex, MidiChannel channel) const;
    const std::vector<C_MidiMessage> &
    getMidiNotesOffThisFrame(int portIndex, MidiChannel channel) const;

    // Merged-view inserts (no port identity) — retained for the legacy API.
    void insertNoteOffMessage(MidiChannel channel, const C_MidiMessage &midiMessage);
    void insertNoteOnMessage(MidiChannel channel, const C_MidiMessage &midiMessage);
    void insertCCMessage(MidiChannel channel, const C_MidiMessage &midiMessage);

    // Port-aware inserts — fold into both the merged view and the port view.
    void insertNoteOffMessage(int portIndex, MidiChannel channel, const C_MidiMessage &midiMessage);
    void insertNoteOnMessage(int portIndex, MidiChannel channel, const C_MidiMessage &midiMessage);
    void insertCCMessage(int portIndex, MidiChannel channel, const C_MidiMessage &midiMessage);

  private:
    RtMidiIn m_rtMidiIn; // enumeration probe only — never opened for input
    unsigned int m_numberPorts;
    std::vector<std::string> m_portNames;
    std::vector<std::unique_ptr<MidiInPort>> m_ports;
    MidiInputFrameBuffer m_frameBuffer;

    void clearPreviousMessages();
};

void readMessageTestCallbackNew(
    double deltaTime, std::vector<unsigned char> *message, void *userData
);

} // namespace IRAudio

#endif /* MIDI_IN_H */
