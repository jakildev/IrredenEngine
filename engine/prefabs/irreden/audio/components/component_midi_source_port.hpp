#ifndef COMPONENT_MIDI_SOURCE_PORT_H
#define COMPONENT_MIDI_SOURCE_PORT_H

namespace IRComponents {

// Source-port identity for an inbound MIDI message entity. Attached alongside
// C_MidiMessage + C_MidiIn when a message enters the ECS from an open input
// port, so consumers (per-port monitor lanes, port-scoped handlers) can
// disambiguate same-channel traffic arriving on different ports. portIndex_
// is the RtMidi port index returned by IRAudio::openPortMidiIn (-1 if unknown).
struct C_MidiSourcePort {
    int portIndex_;

    C_MidiSourcePort(int portIndex)
        : portIndex_(portIndex) {}

    C_MidiSourcePort()
        : portIndex_(-1) {}
};

} // namespace IRComponents

#endif /* COMPONENT_MIDI_SOURCE_PORT_H */
