#include <gtest/gtest.h>

#include <irreden/ir_audio.hpp>
#include <irreden/ir_entity.hpp>
#include <irreden/ir_system.hpp>
#include <irreden/audio/audio_manager.hpp>
#include <irreden/audio/midi_in.hpp>

#include "common/allocation_counter.hpp"

#include <atomic>
#include <chrono>
#include <memory>
#include <string>
#include <thread>
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
        onRtMidiMessage(0.0, &bytes, &m_port.ring_);
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

TEST_F(MidiInTickTest, PreservesConcurrentCallbackMessagesInOrder) {
    constexpr std::size_t kConcurrentMessageCount = 1000;
    std::atomic<bool> producerFinished{false};
    std::vector<C_MidiMessage> received;
    received.reserve(kConcurrentMessageCount);

    std::thread producer([this, &producerFinished]() {
        std::vector<unsigned char> bytes{buildMidiStatus(kMidiStatus_NOTE_ON, kChannel), 0, 0};
        for (std::size_t sequence = 0; sequence < kConcurrentMessageCount; ++sequence) {
            bytes[1] = static_cast<unsigned char>(sequence & 0x7F);
            bytes[2] = static_cast<unsigned char>(sequence >> 7);
            onRtMidiMessage(0.0, &bytes, &m_port.ring_);
        }
        producerFinished.store(true, std::memory_order_release);
    });

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (received.size() < kConcurrentMessageCount &&
           std::chrono::steady_clock::now() < deadline) {
        m_audioManager.getMidiIn().tick();
        const auto &notes = getMidiNotesOnThisFrame(kPortIndex, kChannel);
        received.insert(received.end(), notes.begin(), notes.end());
        if (!producerFinished.load(std::memory_order_acquire) ||
            received.size() < kConcurrentMessageCount) {
            std::this_thread::yield();
        }
    }
    producer.join();

    ASSERT_EQ(received.size(), kConcurrentMessageCount);
    for (std::size_t sequence = 0; sequence < received.size(); ++sequence) {
        EXPECT_EQ(received[sequence].data1_, static_cast<unsigned char>(sequence & 0x7F));
        EXPECT_EQ(received[sequence].data2_, static_cast<unsigned char>(sequence >> 7));
    }
}

TEST_F(MidiInTickTest, ReportsAndClearsOverflowEpisodes) {
    constexpr std::size_t kExtraMessages = 37;
    for (std::size_t sequence = 0; sequence < kMidiMessageRingCapacity + kExtraMessages;
         ++sequence) {
        queueMessage(
            kMidiStatus_NOTE_ON,
            static_cast<unsigned char>(sequence & 0x7F),
            static_cast<unsigned char>(sequence >> 7)
        );
    }

    m_audioManager.getMidiIn().tick();

    const auto &notes = getMidiNotesOnThisFrame(kPortIndex, kChannel);
    ASSERT_EQ(notes.size(), kMidiMessageRingCapacity);
    for (std::size_t sequence = 0; sequence < notes.size(); ++sequence) {
        EXPECT_EQ(notes[sequence].data1_, static_cast<unsigned char>(sequence & 0x7F));
        EXPECT_EQ(notes[sequence].data2_, static_cast<unsigned char>(sequence >> 7));
    }
    EXPECT_TRUE(m_port.overflowing_);

    m_audioManager.getMidiIn().tick();

    EXPECT_FALSE(m_port.overflowing_);
}

TEST_F(MidiInTickTest, CallbackDoesNotAllocate) {
    std::vector<unsigned char> bytes{buildMidiStatus(kMidiStatus_NOTE_ON, kChannel), 60, 100};
    IRTest::AllocationCounter allocationCounter;

    for (std::size_t index = 0; index < 1000; ++index) {
        onRtMidiMessage(0.0, &bytes, &m_port.ring_);
    }

    EXPECT_EQ(allocationCounter.allocations(), 0u);
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
