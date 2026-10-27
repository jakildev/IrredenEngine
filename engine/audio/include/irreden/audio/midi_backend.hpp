#ifndef MIDI_BACKEND_H
#define MIDI_BACKEND_H

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace IRAudio::detail {

using MidiCallback = void (*)(double, std::vector<unsigned char> *, void *);

class IMidiOutBackend {
  public:
    virtual ~IMidiOutBackend() = default;
    virtual unsigned int portCount() = 0;
    virtual std::string portName(unsigned int index) = 0;
    virtual void openPort(unsigned int index) = 0;
    virtual void sendMessage(const std::vector<unsigned char> &message) = 0;
};

class IMidiInBackend {
  public:
    virtual ~IMidiInBackend() = default;
    virtual unsigned int portCount() = 0;
    virtual std::string portName(unsigned int index) = 0;
    virtual void openPort(unsigned int index) = 0;
    virtual void setCallback(MidiCallback callback, void *userData) = 0;
};

using MidiOutBackendFactory = std::function<std::unique_ptr<IMidiOutBackend>()>;
using MidiInBackendFactory = std::function<std::unique_ptr<IMidiInBackend>()>;

MidiOutBackendFactory rtMidiOutBackendFactory();
MidiInBackendFactory rtMidiInBackendFactory();

template <typename Factory>
void enumerateMidiPorts(const Factory &factory, std::vector<std::string> &names) {
    names.clear();
    auto probe = factory();
    const unsigned int count = probe->portCount();
    names.reserve(count);
    for (unsigned int i = 0; i < count; ++i) {
        names.push_back(probe->portName(i));
    }
}

template <typename Ports> int allocateMidiLogicalHandle(int backendIndex, const Ports &ports) {
    const auto owned = [&ports](int handle) {
        for (const auto &port : ports) {
            if (port->logicalHandle_ == handle) {
                return true;
            }
        }
        return false;
    };
    if (!owned(backendIndex)) {
        return backendIndex;
    }
    int handle = 0;
    while (owned(handle)) {
        ++handle;
    }
    return handle;
}

} // namespace IRAudio::detail

#endif /* MIDI_BACKEND_H */
