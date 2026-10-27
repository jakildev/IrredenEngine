#include <irreden/audio/midi_out.hpp>

#include <irreden/ir_profile.hpp>

#include <RtMidi.h>

#include <unordered_set>

namespace IRAudio {

namespace {

class RtMidiOutBackend final : public detail::IMidiOutBackend {
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
    void sendMessage(const std::vector<unsigned char> &message) override {
        m_midi.sendMessage(&message);
    }

  private:
    RtMidiOut m_midi;
};

} // namespace

namespace detail {

MidiOutBackendFactory rtMidiOutBackendFactory() {
    return []() { return std::make_unique<RtMidiOutBackend>(); };
}

} // namespace detail

MidiOut::MidiOut()
    : MidiOut(detail::rtMidiOutBackendFactory()) {}

MidiOut::MidiOut(detail::MidiOutBackendFactory backendFactory)
    : m_backendFactory(std::move(backendFactory))
    , m_numberPorts(0) {
    refreshPortNames();
    IRE_LOG_INFO("Created MidiOut");
}

MidiOut::~MidiOut() {
    sendAllNotesOff();
}

void MidiOut::refreshPortNames() {
    m_numberPorts = 0;
    try {
        detail::enumerateMidiPorts(m_backendFactory, m_portNames);
        m_numberPorts = static_cast<unsigned int>(m_portNames.size());
    } catch (const RtMidiError &error) {
        m_portNames.clear();
        IRE_LOG_WARN(
            "MidiOut: RtMidi client init failed ({}); MIDI output disabled",
            error.getMessage()
        );
    }
}

int MidiOut::openPort(MidiOutInterfaces interface) {
    return openPort(kMidiOutInterfaceNames[midiOutInterfaceIndex(interface)]);
}

int MidiOut::openPort(std::string portNameSubstring) {
    std::lock_guard<std::mutex> lock(m_portsMutex);
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
                port->backendIndex_ = i;
                port->backend_ = std::move(backend);
                IRE_LOG_INFO("Reopened MIDI Out port {}: {}", port->logicalHandle_, name);
                return port->logicalHandle_;
            } catch (const RtMidiError &error) {
                IRE_LOG_WARN(
                    "MidiOut::openPort: failed to open port {} ({}): {}",
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
            auto backend = m_backendFactory();
            backend->openPort(static_cast<unsigned int>(i));
            auto port = std::make_unique<MidiOutPort>();
            port->logicalHandle_ = detail::allocateMidiLogicalHandle(i, m_ports);
            port->backendIndex_ = i;
            port->occurrence_ = occurrence;
            port->name_ = name;
            port->backend_ = std::move(backend);
            const int handle = port->logicalHandle_;
            m_ports.push_back(std::move(port));
            if (m_defaultHandle < 0) {
                m_defaultHandle = handle;
            }
            IRE_LOG_INFO("Opened MIDI Out port {}: {}", handle, name);
            return handle;
        } catch (const RtMidiError &error) {
            IRE_LOG_WARN(
                "MidiOut::openPort: failed to open port {} ({}): {}",
                i,
                name,
                error.getMessage()
            );
            return -1;
        }
    }
    IRE_LOG_WARN(
        "No MIDI output port matching '{}' — {} port(s) available",
        portNameSubstring,
        m_numberPorts
    );
    return -1;
}

const std::vector<std::string> &MidiOut::getPortNames() const {
    return m_portNames;
}

std::vector<int> MidiOut::getOpenPortIndices() const {
    std::lock_guard<std::mutex> lock(m_portsMutex);
    std::vector<int> indices;
    for (const auto &port : m_ports) {
        if (port->backend_) {
            indices.push_back(port->logicalHandle_);
        }
    }
    return indices;
}

bool MidiOut::isPortOpen(int portIndex) const {
    std::lock_guard<std::mutex> lock(m_portsMutex);
    for (const auto &port : m_ports) {
        if (port->logicalHandle_ == portIndex) {
            return port->backend_ != nullptr;
        }
    }
    return false;
}

void MidiOut::sendMessage(const std::vector<unsigned char> &message) {
    sendMessage(m_defaultHandle, message);
}

void MidiOut::sendMessage(int portIndex, const std::vector<unsigned char> &message) {
    std::lock_guard<std::mutex> lock(m_portsMutex);
    for (auto &port : m_ports) {
        if (port->logicalHandle_ == portIndex && port->backend_) {
            port->backend_->sendMessage(message);
            return;
        }
    }
    IRE_LOG_WARN("sendMessage targeted unopened MIDI output port {}", portIndex);
}

void MidiOut::sendAllNotesOff() {
    std::lock_guard<std::mutex> lock(m_portsMutex);
    for (auto &port : m_ports) {
        if (!port->backend_) {
            continue;
        }
        for (unsigned char channel = 0; channel < kNumMidiChannels; ++channel) {
            port->backend_->sendMessage(
                {buildMidiStatus(kMidiStatus_CONTROL_CHANGE, channel), kMidiCC_ALL_NOTES_OFF, 0}
            );
        }
    }
}

void MidiOut::releasePorts() {
    std::lock_guard<std::mutex> lock(m_portsMutex);
    for (auto &port : m_ports) {
        if (port->backend_) {
            IRE_LOG_INFO("Closed MIDI Out port {}: {}", port->logicalHandle_, port->name_);
            port->backend_.reset();
            port->backendIndex_ = -1;
        }
    }
}

std::vector<std::string> MidiOut::restorePorts() {
    std::lock_guard<std::mutex> lock(m_portsMutex);
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
            port->backendIndex_ = match;
            port->backend_ = std::move(backend);
            consumed.insert(match);
            IRE_LOG_INFO("Reopened MIDI Out port {}: {}", port->logicalHandle_, port->name_);
        } catch (const std::exception &error) {
            lost.push_back(port->name_);
            IRE_LOG_WARN(
                "Could not reopen MIDI Out port {} ({}): {}",
                port->logicalHandle_,
                port->name_,
                error.what()
            );
        }
    }
    return lost;
}

} // namespace IRAudio
