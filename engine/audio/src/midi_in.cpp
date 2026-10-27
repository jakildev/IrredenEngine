#include <irreden/ir_audio.hpp>

#include <irreden/audio/midi_in.hpp>

#include <RtMidi.h>

#include <unordered_set>

using namespace IRComponents;

namespace IRAudio {

namespace {

class RtMidiInBackend final : public detail::IMidiInBackend {
  public:
    unsigned int portCount() override {
        return m_midi.getPortCount();
    }
    std::string portName(unsigned int index) override {
        return m_midi.getPortName(index);
    }
    void openPort(unsigned int index) override {
        m_midi.openPort(index);
    }
    void setCallback(detail::MidiCallback callback, void *userData) override {
        m_midi.setCallback(callback, userData);
    }

  private:
    RtMidiIn m_midi;
};

} // namespace

namespace detail {

MidiInBackendFactory rtMidiInBackendFactory() {
    return []() { return std::make_unique<RtMidiInBackend>(); };
}

} // namespace detail

MidiIn::MidiIn()
    : MidiIn(detail::rtMidiInBackendFactory()) {}

MidiIn::MidiIn(detail::MidiInBackendFactory backendFactory)
    : m_backendFactory(std::move(backendFactory))
    , m_numberPorts(0) {
    refreshPortNames();
    IRE_LOG_INFO("Created MidiIn");
}

MidiIn::~MidiIn() = default;

void MidiIn::refreshPortNames() {
    m_numberPorts = 0;
    try {
        detail::enumerateMidiPorts(m_backendFactory, m_portNames);
        m_numberPorts = static_cast<unsigned int>(m_portNames.size());
    } catch (const RtMidiError &error) {
        m_portNames.clear();
        IRE_LOG_WARN(
            "MidiIn: RtMidi client init failed ({}); MIDI input disabled",
            error.getMessage()
        );
    }
}

void MidiIn::tick() {
    clearPreviousMessages();
    processMidiMessageQueue();
}

void MidiIn::clearPreviousMessages() {
    m_frameBuffer.clear();
}

int MidiIn::openPort(const std::string &portNameSubstring) {
    refreshPortNames();
    for (int i = 0; i < static_cast<int>(m_portNames.size()); ++i) {
        const std::string &name = m_portNames[i];
        if (name.find(portNameSubstring) == std::string::npos) {
            continue;
        }
        unsigned int occurrence = 0;
        for (int before = 0; before < i; ++before) {
            occurrence += m_portNames[before] == name ? 1U : 0U;
        }
        bool tryLaterDuplicate = false;
        for (auto &port : m_ports) {
            if (port->name_ != name || port->occurrence_ != occurrence) {
                continue;
            }
            if (port->backend_) {
                bool hasLaterDuplicate = false;
                for (int later = i + 1; later < static_cast<int>(m_portNames.size()); ++later) {
                    hasLaterDuplicate = hasLaterDuplicate || m_portNames[later] == name;
                }
                if (portNameSubstring == name && hasLaterDuplicate) {
                    tryLaterDuplicate = true;
                    break;
                }
                return port->logicalHandle_;
            }
            try {
                auto backend = m_backendFactory();
                backend->openPort(static_cast<unsigned int>(i));
                backend->setCallback(onRtMidiMessage, &port->ring_);
                port->backendIndex_ = i;
                port->backend_ = std::move(backend);
                return port->logicalHandle_;
            } catch (const RtMidiError &error) {
                IRE_LOG_WARN(
                    "MidiIn::openPort: failed to open port {} ({}): {}",
                    i,
                    name,
                    error.getMessage()
                );
                return -1;
            }
        }
        if (tryLaterDuplicate) {
            continue;
        }
        try {
            auto port = std::make_unique<MidiInPort>();
            auto backend = m_backendFactory();
            backend->openPort(static_cast<unsigned int>(i));
            backend->setCallback(onRtMidiMessage, &port->ring_);
            port->logicalHandle_ = detail::allocateMidiLogicalHandle(i, m_ports);
            port->backendIndex_ = i;
            port->occurrence_ = occurrence;
            port->name_ = name;
            port->backend_ = std::move(backend);
            const int handle = port->logicalHandle_;
            m_ports.push_back(std::move(port));
            IRE_LOG_INFO("Opened MIDI In port {}: {}", handle, name);
            return handle;
        } catch (const RtMidiError &error) {
            IRE_LOG_WARN(
                "MidiIn::openPort: failed to open port {} ({}): {}",
                i,
                name,
                error.getMessage()
            );
            return -1;
        }
    }
    IRE_LOG_WARN(
        "No MIDI input port matching '{}' — {} port(s) available",
        portNameSubstring,
        m_numberPorts
    );
    return -1;
}

int MidiIn::openPort(MidiInInterfaces interface) {
    return openPort(kMidiInInterfaceNames[midiInInterfaceIndex(interface)]);
}

const std::vector<std::string> &MidiIn::getPortNames() const {
    return m_portNames;
}

std::vector<int> MidiIn::getOpenPortIndices() const {
    std::vector<int> indices;
    for (const auto &port : m_ports) {
        if (port->backend_) {
            indices.push_back(port->logicalHandle_);
        }
    }
    return indices;
}

void MidiIn::releasePorts() {
    for (auto &port : m_ports) {
        if (port->backend_) {
            IRE_LOG_INFO("Closed MIDI In port {}: {}", port->logicalHandle_, port->name_);
            port->backend_.reset();
            port->backendIndex_ = -1;
        }
        port->ring_.drain([](const C_MidiMessage &) {});
        port->ring_.takeDroppedCount();
        port->overflowing_ = false;
    }
    m_frameBuffer.clear();
}

std::vector<std::string> MidiIn::restorePorts() {
    refreshPortNames();
    std::vector<std::string> lost;
    std::unordered_set<int> consumed;
    for (auto &port : m_ports) {
        if (port->backend_) {
            consumed.insert(port->backendIndex_);
            continue;
        }
        unsigned int occurrence = 0;
        int match = -1;
        for (int i = 0; i < static_cast<int>(m_portNames.size()); ++i) {
            if (m_portNames[i] != port->name_) {
                continue;
            }
            if (occurrence++ == port->occurrence_ && !consumed.contains(i)) {
                match = i;
                break;
            }
        }
        try {
            if (match < 0) {
                throw std::runtime_error("endpoint is absent");
            }
            auto backend = m_backendFactory();
            backend->openPort(static_cast<unsigned int>(match));
            backend->setCallback(onRtMidiMessage, &port->ring_);
            port->backendIndex_ = match;
            port->backend_ = std::move(backend);
            consumed.insert(match);
            IRE_LOG_INFO("Reopened MIDI In port {}: {}", port->logicalHandle_, port->name_);
        } catch (const std::exception &error) {
            lost.push_back(port->name_);
            IRE_LOG_WARN(
                "Could not reopen MIDI In port {} ({}): {}",
                port->logicalHandle_,
                port->name_,
                error.what()
            );
        }
    }
    return lost;
}

void MidiIn::processMidiMessageQueue() {
    for (auto &port : m_ports) {
        if (!port->backend_) {
            continue;
        }
        port->ring_.drain([this, &port](const C_MidiMessage &message) {
            m_frameBuffer.insertMessage(port->logicalHandle_, message);
        });
        const std::uint32_t droppedCount = port->ring_.takeDroppedCount();
        if (droppedCount == 0) {
            port->overflowing_ = false;
        } else if (!port->overflowing_) {
            IRE_LOG_WARN(
                "MIDI In port {} ({}) overflowed its {}-message ring; dropped {} message(s)",
                port->logicalHandle_,
                port->name_,
                kMidiMessageRingCapacity,
                droppedCount
            );
            port->overflowing_ = true;
        }
    }
}

CCData MidiIn::checkCCMessageThisFrame(MidiChannel channel, CCMessage ccNumber) const {
    return m_frameBuffer.checkCC(channel, ccNumber);
}
CCData
MidiIn::checkCCMessageThisFrame(int portIndex, MidiChannel channel, CCMessage ccNumber) const {
    return m_frameBuffer.checkCC(portIndex, channel, ccNumber);
}
const std::vector<C_MidiMessage> &MidiIn::getMidiNotesOnThisFrame(MidiChannel channel) const {
    return m_frameBuffer.notesOn(channel);
}
const std::vector<C_MidiMessage> &MidiIn::getMidiNotesOffThisFrame(MidiChannel channel) const {
    return m_frameBuffer.notesOff(channel);
}
const std::vector<C_MidiMessage> &
MidiIn::getMidiNotesOnThisFrame(int portIndex, MidiChannel channel) const {
    return m_frameBuffer.notesOn(portIndex, channel);
}
const std::vector<C_MidiMessage> &
MidiIn::getMidiNotesOffThisFrame(int portIndex, MidiChannel channel) const {
    return m_frameBuffer.notesOff(portIndex, channel);
}
void MidiIn::insertNoteOffMessage(MidiChannel channel, const C_MidiMessage &message) {
    m_frameBuffer.insertNoteOff(channel, message);
}
void MidiIn::insertNoteOnMessage(MidiChannel channel, const C_MidiMessage &message) {
    m_frameBuffer.insertNoteOn(channel, message);
}
void MidiIn::insertCCMessage(MidiChannel channel, const C_MidiMessage &message) {
    m_frameBuffer.insertCC(channel, message);
}
void MidiIn::insertNoteOffMessage(
    int portIndex, MidiChannel channel, const C_MidiMessage &message
) {
    m_frameBuffer.insertNoteOff(portIndex, channel, message);
}
void MidiIn::insertNoteOnMessage(int portIndex, MidiChannel channel, const C_MidiMessage &message) {
    m_frameBuffer.insertNoteOn(portIndex, channel, message);
}
void MidiIn::insertCCMessage(int portIndex, MidiChannel channel, const C_MidiMessage &message) {
    m_frameBuffer.insertCC(portIndex, channel, message);
}

void onRtMidiMessage(double, std::vector<unsigned char> *message, void *userData) noexcept {
    if (message == nullptr || message->empty() || userData == nullptr) {
        return;
    }
    static_cast<MidiMessageRing *>(userData)->push(
        C_MidiMessage{
            (*message)[0],
            message->size() > 1 ? (*message)[1] : static_cast<unsigned char>(0),
            message->size() > 2 ? (*message)[2] : static_cast<unsigned char>(0)
        }
    );
}

} // namespace IRAudio
