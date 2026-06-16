#ifndef MIDI_OUT_H
#define MIDI_OUT_H

#include <irreden/audio/ir_audio_types.hpp>
#include <irreden/audio/midi_messages.hpp>

#include <RtMidi.h>

#include <memory>
#include <string>
#include <vector>

namespace IRAudio {

// One simultaneously-open MIDI output port; each owns its own RtMidiOut so a
// send can target a specific device.
struct MidiOutPort {
    std::unique_ptr<RtMidiOut> rtMidiOut_;
    int portIndex_;
    std::string name_;
};

class MidiOut {
  public:
    MidiOut();
    ~MidiOut();

    // Opens an output port by hardcoded interface enum or device-name
    // substring. Returns the RtMidi port index (stable port handle) or -1 if
    // no port matches. Opening an already-open port returns its handle. The
    // first port opened becomes the default target for the port-less send.
    int openPort(MidiOutInterfaces midiOutInterface);
    int openPort(std::string portNameSubstring);

    const std::vector<std::string> &getPortNames() const;
    std::vector<int> getOpenPortIndices() const;

    // Sends to the default port (the first one opened) — back-compat surface.
    void sendMessage(const std::vector<unsigned char> &message);
    // Sends to a specific open port by its RtMidi port index.
    void sendMessage(int portIndex, const std::vector<unsigned char> &message);

    void sendAllNotesOff();

  private:
    RtMidiOut m_rtMidiOut; // enumeration probe only — never opened for output
    unsigned int m_numberPorts;
    std::vector<std::string> m_portNames;
    std::vector<std::unique_ptr<MidiOutPort>> m_ports;
};

} // namespace IRAudio

#endif /* MIDI_OUT_H */
