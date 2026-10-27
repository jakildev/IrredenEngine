#include <irreden/audio/midi_in.hpp>
#include <irreden/audio/midi_out.hpp>

#include <gtest/gtest.h>

#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

struct FakeMidiState {
    std::vector<std::string> names_;
    std::vector<std::pair<int, std::vector<unsigned char>>> sends_;
    std::vector<bool> open_;
};

class FakeMidiOutBackend final : public IRAudio::detail::IMidiOutBackend {
  public:
    explicit FakeMidiOutBackend(std::shared_ptr<FakeMidiState> state)
        : m_state(std::move(state)) {}
    ~FakeMidiOutBackend() override {
        if (m_index >= 0) {
            m_state->open_[m_index] = false;
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
    }

  private:
    std::shared_ptr<FakeMidiState> m_state;
    int m_index = -1;
};

IRAudio::detail::MidiOutBackendFactory outFactory(const std::shared_ptr<FakeMidiState> &state) {
    return [state]() { return std::make_unique<FakeMidiOutBackend>(state); };
}

TEST(MidiPortHandOffTest, MissingSlotKeepsItsHandleAndCannotBeStolen) {
    auto state = std::make_shared<FakeMidiState>();
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
    auto state = std::make_shared<FakeMidiState>();
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
    auto state = std::make_shared<FakeMidiState>();
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
}

} // namespace
