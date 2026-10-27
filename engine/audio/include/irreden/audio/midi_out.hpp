#ifndef MIDI_OUT_H
#define MIDI_OUT_H

#include <irreden/audio/ir_audio_types.hpp>
#include <irreden/audio/midi_messages.hpp>
#include <irreden/audio/midi_backend.hpp>

#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace IRAudio {

// One simultaneously-open MIDI output port; each owns its own RtMidiOut so a
// send can target a specific device.
struct MidiOutPort {
    std::unique_ptr<detail::IMidiOutBackend> backend_;
    int logicalHandle_;
    int backendIndex_;
    unsigned int occurrence_;
    std::string name_;
};

class MidiOut {
  public:
    MidiOut();
    explicit MidiOut(detail::MidiOutBackendFactory backendFactory);
    ~MidiOut();

    // Opens an output port by hardcoded interface enum or device-name
    // substring. Returns a session-local logical handle or -1 if
    // no port matches. Opening an already-open port returns its handle. The
    // first port opened becomes the default target for the port-less send.
    int openPort(MidiOutInterfaces midiOutInterface);
    int openPort(std::string portNameSubstring);

    const std::vector<std::string> &getPortNames() const;
    std::vector<int> getOpenPortIndices() const;
    bool isPortOpen(int portIndex) const;

    // Sends to the default port (the first one opened) — back-compat surface.
    void sendMessage(const std::vector<unsigned char> &message);
    // Sends to a specific open port by its logical handle.
    void sendMessage(int portIndex, const std::vector<unsigned char> &message);

    void sendAllNotesOff();
    void releasePorts();
    std::vector<std::string> restorePorts();

  private:
    // Enumeration probe only — never opened for output. Null when the RtMidi
    // client failed to init (no MIDI server available); degraded state is
    // 0 ports, every query/tick path already tolerates that.
    detail::MidiOutBackendFactory m_backendFactory;
    unsigned int m_numberPorts;
    std::vector<std::string> m_portNames;
    std::vector<std::unique_ptr<MidiOutPort>> m_ports;
    int m_defaultHandle = -1;
    // One lock over the port list and every send: the MidiClock thread writes
    // its pulses to a port the main thread is sending notes and CC to.
    mutable std::mutex m_portsMutex;

    void refreshPortNames();
};

} // namespace IRAudio

#endif /* MIDI_OUT_H */
