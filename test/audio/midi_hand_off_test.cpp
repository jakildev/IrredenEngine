#include <irreden/audio/audio_manager.hpp>
#include <irreden/audio/midi_in.hpp>
#include <irreden/audio/midi_out.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

struct FakeMidiOutState {
    std::vector<std::string> names_;
    std::vector<std::pair<int, std::vector<unsigned char>>> sends_;
    std::vector<bool> open_;
    std::vector<std::string> *events_ = nullptr;
};

class FakeMidiOutBackend final : public IRAudio::detail::IMidiOutBackend {
  public:
    explicit FakeMidiOutBackend(std::shared_ptr<FakeMidiOutState> state)
        : m_state(std::move(state)) {}
    ~FakeMidiOutBackend() override {
        if (m_index < 0) {
            return;
        }
        m_state->open_[m_index] = false;
        if (m_state->events_ != nullptr) {
            m_state->events_->push_back("output-close");
        }
    }
    unsigned int portCount() override {
        return static_cast<unsigned int>(m_state->names_.size());
    }
    std::string portName(unsigned int index) override {
        return m_state->names_.at(index);
    }
    void openPort(unsigned int index) override {
        if (index >= m_state->names_.size()) {
            throw std::runtime_error("missing");
        }
        m_state->open_.resize(m_state->names_.size());
        if (m_state->open_[index]) {
            throw std::runtime_error("exclusive");
        }
        m_index = static_cast<int>(index);
        m_state->open_[index] = true;
    }
    void sendMessage(const std::vector<unsigned char> &message) override {
        m_state->sends_.emplace_back(m_index, message);
        if (m_state->events_ == nullptr || message.empty()) {
            return;
        }
        if (message.front() == IRAudio::kMidiStatus_STOP) {
            m_state->events_->push_back("clock-stop");
        } else if (
            (message.front() & 0xF0) == IRAudio::kMidiStatus_CONTROL_CHANGE &&
            message.size() >= 2 && message[1] == IRAudio::kMidiCC_ALL_NOTES_OFF
        ) {
            m_state->events_->push_back("all-notes-off");
        }
    }

  private:
    std::shared_ptr<FakeMidiOutState> m_state;
    int m_index = -1;
};

IRAudio::detail::MidiOutBackendFactory outFactory(const std::shared_ptr<FakeMidiOutState> &state) {
    return [state]() { return std::make_unique<FakeMidiOutBackend>(state); };
}

struct FakeMidiInEndpoint {
    IRAudio::detail::MidiCallback callback_ = nullptr;
    void *userData_ = nullptr;
    bool open_ = false;
};

struct FakeMidiInState {
    std::vector<std::string> names_;
    std::vector<FakeMidiInEndpoint> endpoints_;
    std::vector<std::string> *events_ = nullptr;

    void inject(unsigned int index, std::vector<unsigned char> message) {
        ASSERT_LT(index, endpoints_.size());
        ASSERT_NE(endpoints_[index].callback_, nullptr);
        endpoints_[index].callback_(0.0, &message, endpoints_[index].userData_);
    }
};

class FakeMidiInBackend final : public IRAudio::detail::IMidiInBackend {
  public:
    explicit FakeMidiInBackend(std::shared_ptr<FakeMidiInState> state)
        : m_state(std::move(state)) {}
    ~FakeMidiInBackend() override {
        if (m_index < 0) {
            return;
        }
        FakeMidiInEndpoint &endpoint = m_state->endpoints_[m_index];
        endpoint = {};
        if (m_state->events_ != nullptr) {
            m_state->events_->push_back("input-close");
        }
    }
    unsigned int portCount() override {
        return static_cast<unsigned int>(m_state->names_.size());
    }
    std::string portName(unsigned int index) override {
        return m_state->names_.at(index);
    }
    void openPort(unsigned int index) override {
        if (index >= m_state->names_.size()) {
            throw std::runtime_error("missing");
        }
        m_state->endpoints_.resize(m_state->names_.size());
        if (m_state->endpoints_[index].open_) {
            throw std::runtime_error("exclusive");
        }
        m_index = static_cast<int>(index);
        m_state->endpoints_[index].open_ = true;
    }
    void setCallback(IRAudio::detail::MidiCallback callback, void *userData) override {
        m_state->endpoints_[m_index].callback_ = callback;
        m_state->endpoints_[m_index].userData_ = userData;
    }

  private:
    std::shared_ptr<FakeMidiInState> m_state;
    int m_index = -1;
};

IRAudio::detail::MidiInBackendFactory inFactory(const std::shared_ptr<FakeMidiInState> &state) {
    return [state]() { return std::make_unique<FakeMidiInBackend>(state); };
}

TEST(MidiPortHandOffTest, MissingSlotKeepsItsHandleAndCannotBeStolen) {
    auto state = std::make_shared<FakeMidiOutState>();
    state->names_ = {"Original", "Other"};
    IRAudio::MidiOut output{outFactory(state)};
    const int original = output.openPort("Original");
    EXPECT_EQ(original, 0);

    output.releasePorts();
    state->names_ = {"Replacement", "Other"};
    state->open_.assign(state->names_.size(), false);
    EXPECT_EQ(output.restorePorts(), (std::vector<std::string>{"Original"}));
    const int replacement = output.openPort("Replacement");
    EXPECT_NE(replacement, original);

    output.releasePorts();
    state->names_ = {"Replacement", "Original", "Other"};
    state->open_.assign(state->names_.size(), false);
    EXPECT_TRUE(output.restorePorts().empty());
    EXPECT_TRUE(output.isPortOpen(original));
    EXPECT_EQ(output.openPort("Original"), original);
}

TEST(MidiPortHandOffTest, LostDefaultDoesNotRetargetAnotherOutput) {
    auto state = std::make_shared<FakeMidiOutState>();
    state->names_ = {"Default", "Secondary"};
    IRAudio::MidiOut output{outFactory(state)};
    const int defaultHandle = output.openPort("Default");
    const int secondaryHandle = output.openPort("Secondary");

    output.releasePorts();
    state->names_ = {"Secondary"};
    state->open_.assign(1, false);
    EXPECT_EQ(output.restorePorts(), (std::vector<std::string>{"Default"}));

    output.sendMessage({0x90, 60, 100});
    EXPECT_TRUE(state->sends_.empty());
    output.sendMessage(secondaryHandle, {0x90, 60, 100});
    ASSERT_EQ(state->sends_.size(), 1u);
    EXPECT_EQ(state->sends_.front().first, 0);
    EXPECT_FALSE(output.isPortOpen(defaultHandle));
}

TEST(MidiPortHandOffTest, DuplicateNamesRestoreToDistinctConsumedIndices) {
    auto state = std::make_shared<FakeMidiOutState>();
    state->names_ = {"Twin", "Twin"};
    IRAudio::MidiOut output{outFactory(state)};
    const int first = output.openPort("Twin");
    const int second = output.openPort("Twin");
    ASSERT_NE(first, second);

    output.releasePorts();
    state->open_.assign(2, false);
    EXPECT_TRUE(output.restorePorts().empty());
    output.sendMessage(first, {0x90, 60, 100});
    output.sendMessage(second, {0x90, 61, 100});

    ASSERT_EQ(state->sends_.size(), 2u);
    EXPECT_NE(state->sends_[0].first, state->sends_[1].first);

    output.releasePorts();
    state->names_.clear();
    EXPECT_EQ(output.restorePorts(), (std::vector<std::string>{"Twin", "Twin"}));
}

TEST(MidiInputHandOffTest, ReleaseClearsInputAndRestoreReattachesTheOriginalHandle) {
    constexpr IRAudio::MidiChannel kChannel = 2;
    auto state = std::make_shared<FakeMidiInState>();
    state->names_ = {"Input"};
    IRAudio::MidiIn input{inFactory(state)};
    const int handle = input.openPort("Input");

    state->inject(0, {IRAudio::buildMidiStatus(IRAudio::kMidiStatus_NOTE_ON, kChannel), 60, 100});
    input.tick();
    ASSERT_EQ(input.getMidiNotesOnThisFrame(handle, kChannel).size(), 1u);
    state->inject(0, {IRAudio::buildMidiStatus(IRAudio::kMidiStatus_NOTE_ON, kChannel), 61, 100});

    input.releasePorts();
    EXPECT_TRUE(input.getMidiNotesOnThisFrame(handle, kChannel).empty());
    state->names_.clear();
    EXPECT_EQ(input.restorePorts(), (std::vector<std::string>{"Input"}));
    input.tick();
    EXPECT_TRUE(input.getMidiNotesOnThisFrame(handle, kChannel).empty());

    state->names_ = {"Input"};
    EXPECT_TRUE(input.restorePorts().empty());
    state->inject(0, {IRAudio::buildMidiStatus(IRAudio::kMidiStatus_NOTE_ON, kChannel), 62, 100});
    input.tick();

    const auto &notes = input.getMidiNotesOnThisFrame(handle, kChannel);
    ASSERT_EQ(notes.size(), 1u);
    EXPECT_EQ(notes.front().getMidiNoteNumber(), 62);
}

TEST(MidiInputHandOffTest, DuplicateNamesOpenAsDistinctLogicalSlots) {
    auto state = std::make_shared<FakeMidiInState>();
    state->names_ = {"Twin", "Twin"};
    IRAudio::MidiIn input{inFactory(state)};

    const int first = input.openPort("Twin");
    const int second = input.openPort("Twin");

    EXPECT_NE(first, second);
    EXPECT_EQ(input.getOpenPortIndices(), (std::vector<int>{first, second}));
}

TEST(AudioManagerMidiHandOffTest, StopsClockBeforeClosingAndKeepsItStopped) {
    std::vector<std::string> events;
    auto inputState = std::make_shared<FakeMidiInState>();
    inputState->names_ = {"Input"};
    inputState->events_ = &events;
    auto outputState = std::make_shared<FakeMidiOutState>();
    outputState->names_ = {"Output"};
    outputState->events_ = &events;
    IRAudio::AudioManager manager{inFactory(inputState), outFactory(outputState)};
    manager.getMidiIn().openPort("Input");
    const int outputHandle = manager.getMidiOut().openPort("Output");
    ASSERT_TRUE(manager.getMidiClock().start(outputHandle, 120.0));

    manager.releaseMidiPorts();

    EXPECT_FALSE(manager.getMidiClock().isRunning());
    const auto stop = std::ranges::find(events, "clock-stop");
    const auto notesOff = std::ranges::find(events, "all-notes-off");
    const auto inputClose = std::ranges::find(events, "input-close");
    const auto outputClose = std::ranges::find(events, "output-close");
    ASSERT_NE(stop, events.end());
    ASSERT_NE(notesOff, events.end());
    ASSERT_NE(inputClose, events.end());
    ASSERT_NE(outputClose, events.end());
    EXPECT_LT(stop, notesOff);
    EXPECT_LT(notesOff, inputClose);
    EXPECT_LT(inputClose, outputClose);

    const std::size_t sendsAtClose = outputState->sends_.size();
    manager.getMidiOut().sendMessage(outputHandle, {0x90, 60, 100});
    EXPECT_EQ(outputState->sends_.size(), sendsAtClose);
    EXPECT_TRUE(manager.restoreMidiPorts().empty());
    EXPECT_FALSE(manager.getMidiClock().isRunning());
}

} // namespace
