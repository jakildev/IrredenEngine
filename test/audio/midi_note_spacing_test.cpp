#include <gtest/gtest.h>

#include <irreden/audio/components/component_midi_note.hpp>
#include <irreden/audio/components/component_midi_note_spacing.hpp>
#include <irreden/audio/midi_note_spacing.hpp>

#include <cstdint>
#include <vector>

// The planner over `C_MidiNoteSpacing` is pure, so these drive it without a
// world: the frame's notes land low to high, a stagger apart, behind the
// queue, within the voice count.
namespace {

using IRComponents::C_MidiNote;
using IRComponents::C_MidiNoteSpacing;
using IRPrefab::MidiNotes::ScheduledMessage;

constexpr std::uint8_t kNoteOnCh0 = 0x90;
constexpr std::uint8_t kNoteOffCh0 = 0x80;

C_MidiNoteSpacing::Pending pending(std::uint8_t note, int holdFrames = 6) {
    return {note, 100, 0, holdFrames};
}

std::vector<ScheduledMessage> ons(const std::vector<ScheduledMessage> &plan) {
    std::vector<ScheduledMessage> out;
    for (const ScheduledMessage &m : plan) {
        if (m.status_ == kNoteOnCh0) {
            out.push_back(m);
        }
    }
    return out;
}

} // namespace

TEST(MidiNoteSpacingPlan, ImmediateIsNoteOnNowAndOffAfterTheHold) {
    std::vector<ScheduledMessage> plan;
    IRPrefab::MidiNotes::planImmediate(C_MidiNote{60, 90, 0, 0.1f}, plan);
    ASSERT_EQ(plan.size(), 2u);
    EXPECT_EQ(plan[0].delayFrames_, 0);
    EXPECT_EQ(plan[0].status_, kNoteOnCh0);
    EXPECT_EQ(plan[0].data1_, 60);
    EXPECT_EQ(plan[0].data2_, 90);
    EXPECT_EQ(plan[1].status_, kNoteOffCh0);
    EXPECT_EQ(plan[1].delayFrames_, 6); // 0.1 s at 60 frames a second
}

TEST(MidiNoteSpacingPlan, AFramesNotesLandLowToHighAStaggerApart) {
    C_MidiNoteSpacing s;
    s.enabled_ = true;
    s.staggerFrames_ = 2;
    s.pending_ = {pending(64), pending(60), pending(67)};
    std::vector<ScheduledMessage> plan;
    IRPrefab::MidiNotes::planFrame(s, plan);
    const std::vector<ScheduledMessage> on = ons(plan);
    ASSERT_EQ(on.size(), 3u);
    EXPECT_EQ(on[0].data1_, 60);
    EXPECT_EQ(on[0].delayFrames_, 0);
    EXPECT_EQ(on[1].data1_, 64);
    EXPECT_EQ(on[1].delayFrames_, 2);
    EXPECT_EQ(on[2].data1_, 67);
    EXPECT_EQ(on[2].delayFrames_, 4);
    // Each NOTE_OFF follows its NOTE_ON by the hold.
    EXPECT_EQ(plan[1].status_, kNoteOffCh0);
    EXPECT_EQ(plan[1].delayFrames_, 6);
    EXPECT_EQ(plan[5].delayFrames_, 10);
    EXPECT_TRUE(s.pending_.empty());
    EXPECT_EQ(s.frame_, 1);
    EXPECT_EQ(s.nextFreeFrame_, 6);
    EXPECT_EQ(s.sounding_.size(), 3u);
}

TEST(MidiNoteSpacingPlan, ALaterFrameWaitsBehindTheQueue) {
    C_MidiNoteSpacing s;
    s.enabled_ = true;
    s.staggerFrames_ = 3;
    s.pending_ = {pending(60), pending(62)};
    std::vector<ScheduledMessage> plan;
    IRPrefab::MidiNotes::planFrame(s, plan); // 60 at 0, 62 at 3, next free 6
    plan.clear();
    s.pending_ = {pending(65)};
    IRPrefab::MidiNotes::planFrame(s, plan); // frame 1: the queue is free at 6
    ASSERT_EQ(ons(plan).size(), 1u);
    EXPECT_EQ(ons(plan)[0].delayFrames_, 5);
    plan.clear();
    IRPrefab::MidiNotes::planFrame(s, plan); // an empty frame still counts
    EXPECT_TRUE(plan.empty());
    EXPECT_EQ(s.frame_, 3);
    // Long after the queue drains, a note goes out at once.
    s.frame_ = 100;
    s.pending_ = {pending(70)};
    IRPrefab::MidiNotes::planFrame(s, plan);
    EXPECT_EQ(ons(plan)[0].delayFrames_, 0);
}

TEST(MidiNoteSpacingPlan, APastTheVoiceCountCutsTheOldestOff) {
    C_MidiNoteSpacing s;
    s.enabled_ = true;
    s.staggerFrames_ = 1;
    s.maxVoices_ = 2;
    s.pending_ = {pending(60, 30), pending(64, 30), pending(67, 30)};
    std::vector<ScheduledMessage> plan;
    IRPrefab::MidiNotes::planFrame(s, plan);
    // 60 at 0, 64 at 1; 67 at 2 needs a voice: 60's NOTE_OFF lands at 2,
    // before 67's NOTE_ON.
    ASSERT_EQ(plan.size(), 7u);
    EXPECT_EQ(plan[4].status_, kNoteOffCh0);
    EXPECT_EQ(plan[4].data1_, 60);
    EXPECT_EQ(plan[4].delayFrames_, 2);
    EXPECT_EQ(plan[5].status_, kNoteOnCh0);
    EXPECT_EQ(plan[5].data1_, 67);
    EXPECT_EQ(plan[5].delayFrames_, 2);
    EXPECT_EQ(s.sounding_.size(), 2u);
    // A note whose hold has ended frees its voice without a cut.
    plan.clear();
    s.frame_ = 40;
    s.nextFreeFrame_ = 0;
    s.pending_ = {pending(72, 30)};
    IRPrefab::MidiNotes::planFrame(s, plan);
    ASSERT_EQ(plan.size(), 2u);
    EXPECT_EQ(plan[0].status_, kNoteOnCh0);
    EXPECT_EQ(s.sounding_.size(), 1u);
}

TEST(MidiNoteSpacingPlan, TheDelayCapBunchesANoteRatherThanLosingIt) {
    C_MidiNoteSpacing s;
    s.enabled_ = true;
    s.staggerFrames_ = 4;
    s.maxDelayFrames_ = 5;
    s.pending_ = {pending(60), pending(62), pending(64)};
    std::vector<ScheduledMessage> plan;
    IRPrefab::MidiNotes::planFrame(s, plan);
    const std::vector<ScheduledMessage> on = ons(plan);
    ASSERT_EQ(on.size(), 3u);
    EXPECT_EQ(on[0].delayFrames_, 0);
    EXPECT_EQ(on[1].delayFrames_, 4);
    EXPECT_EQ(on[2].delayFrames_, 5); // 8 capped to 5
}

TEST(MidiNoteSpacingPlan, ChannelsAreNormalizedIntoTheStatus) {
    C_MidiNoteSpacing s;
    s.enabled_ = true;
    s.pending_ = {{61, 80, 3, 2}};
    std::vector<ScheduledMessage> plan;
    IRPrefab::MidiNotes::planFrame(s, plan);
    ASSERT_EQ(plan.size(), 2u);
    EXPECT_EQ(plan[0].status_, static_cast<std::uint8_t>(0x90 | 3));
    EXPECT_EQ(plan[1].status_, static_cast<std::uint8_t>(0x80 | 3));
}
