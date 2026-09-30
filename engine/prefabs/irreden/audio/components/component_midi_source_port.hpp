#ifndef COMPONENT_MIDI_SOURCE_PORT_H
#define COMPONENT_MIDI_SOURCE_PORT_H

namespace IRComponents {

// DEPRECATED — use the port-aware IRAudio insertion functions instead.
// Source-port identity for a synthetic inbound MIDI message entity.
struct C_MidiSourcePort {
    int portIndex_;

    C_MidiSourcePort(int portIndex)
        : portIndex_(portIndex) {}

    C_MidiSourcePort()
        : portIndex_(-1) {}
};

} // namespace IRComponents

#endif /* COMPONENT_MIDI_SOURCE_PORT_H */
