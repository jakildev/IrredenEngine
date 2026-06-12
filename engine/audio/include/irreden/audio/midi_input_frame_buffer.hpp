#ifndef MIDI_INPUT_FRAME_BUFFER_H
#define MIDI_INPUT_FRAME_BUFFER_H

#include <irreden/audio/ir_audio_types.hpp>
#include <irreden/audio/components/component_midi_message.hpp>

#include <unordered_map>
#include <vector>

namespace IRAudio {

// Per-frame snapshot of inbound MIDI state, keyed for both a merged
// (all-ports) view and a per-port view. MidiIn fills this once per frame from
// the C_MidiMessage entity stream; consumers poll it during the same frame.
//
// Two read scopes:
//   * Merged (channel only)        — every open input port folded together.
//     Back-compat surface for consumers that don't care which port a message
//     arrived on (the historical single-port behaviour).
//   * Per-port (portIndex+channel) — one port's traffic in isolation, so two
//     devices sending on the same channel can be told apart.
//
// Both scopes are populated by the same insert call; clear() wipes the whole
// frame. No RtMidi/hardware dependency lives here, which keeps the routing
// logic unit-testable in isolation from device I/O.
class MidiInputFrameBuffer {
  public:
    // Insert into the merged view only (no port identity). Retained for the
    // legacy port-less insert API; the entity drainer uses the port-aware
    // overloads below.
    void insertCC(MidiChannel channel, const IRComponents::C_MidiMessage &message) {
        m_merged.insertCC(channel, message);
    }
    void insertNoteOn(MidiChannel channel, const IRComponents::C_MidiMessage &message) {
        m_merged.insertNoteOn(channel, message);
    }
    void insertNoteOff(MidiChannel channel, const IRComponents::C_MidiMessage &message) {
        m_merged.insertNoteOff(channel, message);
    }

    // Insert into both the merged view and the originating port's view.
    void insertCC(int portIndex, MidiChannel channel, const IRComponents::C_MidiMessage &message) {
        m_merged.insertCC(channel, message);
        m_perPort[portIndex].insertCC(channel, message);
    }
    void
    insertNoteOn(int portIndex, MidiChannel channel, const IRComponents::C_MidiMessage &message) {
        m_merged.insertNoteOn(channel, message);
        m_perPort[portIndex].insertNoteOn(channel, message);
    }
    void
    insertNoteOff(int portIndex, MidiChannel channel, const IRComponents::C_MidiMessage &message) {
        m_merged.insertNoteOff(channel, message);
        m_perPort[portIndex].insertNoteOff(channel, message);
    }

    // Merged reads (all ports folded together).
    CCData checkCC(MidiChannel channel, CCMessage ccNumber) const {
        return m_merged.checkCC(channel, ccNumber);
    }
    const std::vector<IRComponents::C_MidiMessage> &notesOn(MidiChannel channel) const {
        return m_merged.notesOn(channel);
    }
    const std::vector<IRComponents::C_MidiMessage> &notesOff(MidiChannel channel) const {
        return m_merged.notesOff(channel);
    }

    // Per-port reads. An unopened/silent port returns kCCFalse / an empty list.
    CCData checkCC(int portIndex, MidiChannel channel, CCMessage ccNumber) const {
        const auto it = m_perPort.find(portIndex);
        if (it == m_perPort.end()) {
            return kCCFalse;
        }
        return it->second.checkCC(channel, ccNumber);
    }
    const std::vector<IRComponents::C_MidiMessage> &
    notesOn(int portIndex, MidiChannel channel) const {
        const auto it = m_perPort.find(portIndex);
        if (it == m_perPort.end()) {
            return kEmptyMessages;
        }
        return it->second.notesOn(channel);
    }
    const std::vector<IRComponents::C_MidiMessage> &
    notesOff(int portIndex, MidiChannel channel) const {
        const auto it = m_perPort.find(portIndex);
        if (it == m_perPort.end()) {
            return kEmptyMessages;
        }
        return it->second.notesOff(channel);
    }

    // Drop the whole frame. The per-port outer map keys persist (port set is
    // stable across a session) — only the per-channel contents are wiped.
    void clear() {
        m_merged.clear();
        for (auto &entry : m_perPort) {
            entry.second.clear();
        }
    }

  private:
    inline static const std::vector<IRComponents::C_MidiMessage> kEmptyMessages{};

    // One port's (or the merged) per-channel state for a single frame.
    struct ChannelState {
        std::unordered_map<MidiChannel, std::unordered_map<CCMessage, CCData>> cc_;
        std::unordered_map<MidiChannel, std::vector<IRComponents::C_MidiMessage>> noteOn_;
        std::unordered_map<MidiChannel, std::vector<IRComponents::C_MidiMessage>> noteOff_;

        void insertCC(MidiChannel channel, const IRComponents::C_MidiMessage &message) {
            cc_[channel][message.getCCNumber()] = message.getCCValue();
        }
        void insertNoteOn(MidiChannel channel, const IRComponents::C_MidiMessage &message) {
            noteOn_[channel].push_back(message);
        }
        void insertNoteOff(MidiChannel channel, const IRComponents::C_MidiMessage &message) {
            noteOff_[channel].push_back(message);
        }

        CCData checkCC(MidiChannel channel, CCMessage ccNumber) const {
            const auto channelIt = cc_.find(channel);
            if (channelIt == cc_.end()) {
                return kCCFalse;
            }
            const auto ccIt = channelIt->second.find(ccNumber);
            if (ccIt == channelIt->second.end()) {
                return kCCFalse;
            }
            return ccIt->second;
        }
        const std::vector<IRComponents::C_MidiMessage> &notesOn(MidiChannel channel) const {
            const auto it = noteOn_.find(channel);
            return it == noteOn_.end() ? kEmptyMessages : it->second;
        }
        const std::vector<IRComponents::C_MidiMessage> &notesOff(MidiChannel channel) const {
            const auto it = noteOff_.find(channel);
            return it == noteOff_.end() ? kEmptyMessages : it->second;
        }

        void clear() {
            for (auto &entry : cc_) {
                entry.second.clear();
            }
            for (auto &entry : noteOn_) {
                entry.second.clear();
            }
            for (auto &entry : noteOff_) {
                entry.second.clear();
            }
        }
    };

    ChannelState m_merged;
    std::unordered_map<int, ChannelState> m_perPort;
};

} // namespace IRAudio

#endif /* MIDI_INPUT_FRAME_BUFFER_H */
