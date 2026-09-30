#include <gtest/gtest.h>

#include <irreden/ir_audio.hpp>
#include <irreden/ir_entity.hpp>
#include <irreden/ir_system.hpp>
#include <irreden/audio/audio_manager.hpp>
#include <irreden/audio/midi_in.hpp>

#include <memory>
#include <string>
#include <vector>

namespace IRAudio {

struct MidiInTestAccess {
    static MidiInPort &attachPort(MidiIn &midiIn, int portIndex) {
        auto port = std::make_unique<MidiInPort>();
        port->portIndex_ = portIndex;
        port->name_ = "detached test port";
        midiIn.m_ports.push_back(std::move(port));
        return *midiIn.m_ports.back();
    }
};

} // namespace IRAudio

namespace {

using namespace IRAudio;

constexpr int kPortIndex = 3;
constexpr MidiChannel kChannel = 4;
constexpr unsigned char kCCNumber = 7;
constexpr unsigned char kCCValue = 99;
constexpr unsigned char kNoteOn = 60;
constexpr unsigned char kNoteOff = 64;

class MidiInTickTest : public ::testing::Test {
  protected:
    MidiInTickTest()
        : m_entityManager{}
        , m_systemManager{}
        , m_audioManager{}
        , m_port{MidiInTestAccess::attachPort(m_audioManager.getMidiIn(), kPortIndex)} {}

    void queueMessage(MidiStatus status, unsigned char data1, unsigned char data2) {
        std::vector<unsigned char> bytes{buildMidiStatus(status, kChannel), data1, data2};
        onRtMidiMessage(0.0, &bytes, &m_port.queue_);
    }

    IREntity::EntityManager m_entityManager;
    IRSystem::SystemManager m_systemManager;
    AudioManager m_audioManager;
    MidiInPort &m_port;
};

TEST_F(MidiInTickTest, DrainsCallbackQueueIntoQueriesExactlyOnce) {
    const auto entityCount = m_entityManager.getLiveEntityCount();
    queueMessage(kMidiStatus_CONTROL_CHANGE, kCCNumber, kCCValue);
    queueMessage(kMidiStatus_NOTE_ON, kNoteOn, 100);
    queueMessage(kMidiStatus_NOTE_OFF, kNoteOff, 0);

    m_audioManager.getMidiIn().tick();

    EXPECT_EQ(checkCCMessage(kChannel, kCCNumber), kCCValue);
    EXPECT_EQ(checkCCMessage(kPortIndex, kChannel, kCCNumber), kCCValue);

    const auto &mergedNotesOn = getMidiNotesOnThisFrame(kChannel);
    ASSERT_EQ(mergedNotesOn.size(), 1u);
    EXPECT_EQ(mergedNotesOn.front().getMidiNoteNumber(), kNoteOn);
    const auto &portNotesOn = getMidiNotesOnThisFrame(kPortIndex, kChannel);
    ASSERT_EQ(portNotesOn.size(), 1u);
    EXPECT_EQ(portNotesOn.front().getMidiNoteNumber(), kNoteOn);

    const auto &mergedNotesOff = getMidiNotesOffThisFrame(kChannel);
    ASSERT_EQ(mergedNotesOff.size(), 1u);
    EXPECT_EQ(mergedNotesOff.front().getMidiNoteNumber(), kNoteOff);
    const auto &portNotesOff = getMidiNotesOffThisFrame(kPortIndex, kChannel);
    ASSERT_EQ(portNotesOff.size(), 1u);
    EXPECT_EQ(portNotesOff.front().getMidiNoteNumber(), kNoteOff);
    EXPECT_EQ(m_entityManager.getLiveEntityCount(), entityCount);

    m_audioManager.getMidiIn().tick();

    EXPECT_EQ(checkCCMessage(kChannel, kCCNumber), kCCFalse);
    EXPECT_EQ(checkCCMessage(kPortIndex, kChannel, kCCNumber), kCCFalse);
    EXPECT_TRUE(getMidiNotesOnThisFrame(kChannel).empty());
    EXPECT_TRUE(getMidiNotesOnThisFrame(kPortIndex, kChannel).empty());
    EXPECT_TRUE(getMidiNotesOffThisFrame(kChannel).empty());
    EXPECT_TRUE(getMidiNotesOffThisFrame(kPortIndex, kChannel).empty());
    EXPECT_EQ(m_entityManager.getLiveEntityCount(), entityCount);
}

TEST_F(MidiInTickTest, DropsUnsupportedStatus) {
    queueMessage(kMidiStatus_PROGRAM_CHANGE, 42, 0);

    m_audioManager.getMidiIn().tick();

    EXPECT_EQ(checkCCMessage(kChannel, kCCNumber), kCCFalse);
    EXPECT_EQ(checkCCMessage(kChannel, 42), kCCFalse);
    EXPECT_TRUE(getMidiNotesOnThisFrame(kChannel).empty());
    EXPECT_TRUE(getMidiNotesOffThisFrame(kChannel).empty());
}

TEST(MidiInTickRoutingTest, RoutesSupportedStatuses) {
    MidiInputFrameBuffer buffer;
    const IRComponents::C_MidiMessage cc{
        buildMidiStatus(kMidiStatus_CONTROL_CHANGE, kChannel),
        kCCNumber,
        kCCValue
    };
    const IRComponents::C_MidiMessage noteOn{
        buildMidiStatus(kMidiStatus_NOTE_ON, kChannel),
        kNoteOn,
        100
    };
    const IRComponents::C_MidiMessage noteOff{
        buildMidiStatus(kMidiStatus_NOTE_OFF, kChannel),
        kNoteOff,
        0
    };

    buffer.insertMessage(kPortIndex, cc);
    buffer.insertMessage(kPortIndex, noteOn);
    buffer.insertMessage(kPortIndex, noteOff);

    EXPECT_EQ(buffer.checkCC(kPortIndex, kChannel, kCCNumber), kCCValue);
    EXPECT_EQ(buffer.notesOn(kPortIndex, kChannel).size(), 1u);
    EXPECT_EQ(buffer.notesOff(kPortIndex, kChannel).size(), 1u);
}

} // namespace
