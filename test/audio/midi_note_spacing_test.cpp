#include <gtest/gtest.h>

#include <irreden/audio/components/component_midi_note.hpp>
#include <irreden/audio/components/component_midi_note_spacing.hpp>
#include <irreden/audio/midi_note_spacing.hpp>

#include <cstdint>
#include <memory>
#include <vector>

// The planner over `C_MidiNoteSpacing` is pure, so these drive it without a
// world, a frame at a time: the frame's notes land low to high, a stagger
// apart, behind the queue, within the voice count, and every message goes
// out on the frame it is due.
namespace {

using IRComponents::C_MidiNote;
using IRComponents::C_MidiNoteSpacing;
using IRPrefab::MidiNotes::ScheduledMessage;

constexpr std::uint8_t kNoteOnCh0 = 0x90;
constexpr std::uint8_t kNoteOffCh0 = 0x80;

struct Sent {
    long frame_;
    std::uint8_t status_;
    std::uint8_t note_;
};

C_MidiNoteSpacing::Pending pending(std::uint8_t note, int holdFrames = 6) {
    return {note, 100, 0, holdFrames};
}

// The component is a few kilobytes of fixed arrays; keep it off the stack.
std::unique_ptr<C_MidiNoteSpacing>
makeSpacing(int staggerFrames, int maxVoices = 0, int maxDelayFrames = 0) {
    auto s = std::make_unique<C_MidiNoteSpacing>();
    s->enabled_ = true;
    s->staggerFrames_ = staggerFrames;
    s->maxVoices_ = maxVoices;
    s->maxDelayFrames_ = maxDelayFrames;
    return s;
}

// Runs `frames` planner frames, recording each message with the frame it
// went out on. Every message a spaced plan holds is due now.
void run(C_MidiNoteSpacing &s, int frames, std::vector<Sent> &sent) {
    IRPrefab::MidiNotes::FramePlan plan{};
    for (int f = 0; f < frames; ++f) {
        const long frame = s.frame_;
        const int count = IRPrefab::MidiNotes::planFrame(s, plan);
        for (int i = 0; i < count; ++i) {
            EXPECT_EQ(plan[i].delayFrames_, 0);
            sent.push_back({frame, plan[i].status_, plan[i].data1_});
        }
    }
}

std::vector<Sent> withStatus(const std::vector<Sent> &sent, std::uint8_t status) {
    std::vector<Sent> out;
    for (const Sent &m : sent) {
        if (m.status_ == status) {
            out.push_back(m);
        }
    }
    return out;
}

// Whether `note` is sounding after every message through `frame` went out.
bool soundingAfter(const std::vector<Sent> &sent, std::uint8_t note, long frame) {
    bool on = false;
    for (const Sent &m : sent) {
        if (m.frame_ <= frame && m.note_ == note) {
            on = m.status_ == kNoteOnCh0;
        }
    }
    return on;
}

} // namespace

TEST(MidiNoteSpacingPlan, ImmediateIsNoteOnNowAndOffAfterTheHold) {
    const auto plan = IRPrefab::MidiNotes::planImmediate(C_MidiNote{60, 90, 0, 0.1f});
    EXPECT_EQ(plan[0].delayFrames_, 0);
    EXPECT_EQ(plan[0].status_, kNoteOnCh0);
    EXPECT_EQ(plan[0].data1_, 60);
    EXPECT_EQ(plan[0].data2_, 90);
    EXPECT_EQ(plan[1].status_, kNoteOffCh0);
    EXPECT_EQ(plan[1].delayFrames_, 6); // 0.1 s at 60 frames a second
}

TEST(MidiNoteSpacingPlan, AFramesNotesLandLowToHighAStaggerApart) {
    auto s = makeSpacing(2);
    IRPrefab::MidiNotes::queue(*s, pending(64));
    IRPrefab::MidiNotes::queue(*s, pending(60));
    IRPrefab::MidiNotes::queue(*s, pending(67));
    std::vector<Sent> sent;
    run(*s, 1, sent);
    EXPECT_EQ(s->pendingCount_, 0);
    EXPECT_EQ(s->nextFreeFrame_, 6);
    EXPECT_EQ(s->scheduledCount_, 3);
    run(*s, 15, sent);
    const std::vector<Sent> on = withStatus(sent, kNoteOnCh0);
    ASSERT_EQ(on.size(), 3u);
    EXPECT_EQ(on[0].note_, 60);
    EXPECT_EQ(on[0].frame_, 0);
    EXPECT_EQ(on[1].note_, 64);
    EXPECT_EQ(on[1].frame_, 2);
    EXPECT_EQ(on[2].note_, 67);
    EXPECT_EQ(on[2].frame_, 4);
    // Each NOTE_OFF follows its NOTE_ON by the hold.
    const std::vector<Sent> off = withStatus(sent, kNoteOffCh0);
    ASSERT_EQ(off.size(), 3u);
    EXPECT_EQ(off[0].frame_, 6);
    EXPECT_EQ(off[2].frame_, 10);
    EXPECT_EQ(s->scheduledCount_, 0);
}

TEST(MidiNoteSpacingPlan, ALaterFrameWaitsBehindTheQueue) {
    auto s = makeSpacing(3);
    IRPrefab::MidiNotes::queue(*s, pending(60));
    IRPrefab::MidiNotes::queue(*s, pending(62));
    std::vector<Sent> sent;
    run(*s, 1, sent); // 60 at 0, 62 at 3, next free 6
    IRPrefab::MidiNotes::queue(*s, pending(65));
    run(*s, 20, sent); // frame 1: the queue is free at 6
    const std::vector<Sent> on = withStatus(sent, kNoteOnCh0);
    ASSERT_EQ(on.size(), 3u);
    EXPECT_EQ(on[2].note_, 65);
    EXPECT_EQ(on[2].frame_, 6);
    // Long after the queue drains, a note goes out at once.
    IRPrefab::MidiNotes::queue(*s, pending(70));
    const long now = s->frame_;
    sent.clear();
    run(*s, 1, sent);
    ASSERT_EQ(sent.size(), 1u);
    EXPECT_EQ(sent[0].frame_, now);
    EXPECT_EQ(sent[0].status_, kNoteOnCh0);
}

TEST(MidiNoteSpacingPlan, APastTheVoiceCountCutsTheOldestOff) {
    auto s = makeSpacing(1, 2);
    IRPrefab::MidiNotes::queue(*s, pending(60, 30));
    IRPrefab::MidiNotes::queue(*s, pending(64, 30));
    IRPrefab::MidiNotes::queue(*s, pending(67, 30));
    std::vector<Sent> sent;
    run(*s, 3, sent);
    // 60 at 0, 64 at 1; 67 at 2 needs a voice: 60's NOTE_OFF goes out at 2,
    // before 67's NOTE_ON.
    ASSERT_EQ(sent.size(), 4u);
    EXPECT_EQ(sent[2].frame_, 2);
    EXPECT_EQ(sent[2].status_, kNoteOffCh0);
    EXPECT_EQ(sent[2].note_, 60);
    EXPECT_EQ(sent[3].frame_, 2);
    EXPECT_EQ(sent[3].status_, kNoteOnCh0);
    EXPECT_EQ(sent[3].note_, 67);
    // The cut note's own hold end (frame 30) never goes out.
    run(*s, 40, sent);
    int offs60 = 0;
    for (const Sent &m : withStatus(sent, kNoteOffCh0)) {
        offs60 += m.note_ == 60 ? 1 : 0;
    }
    EXPECT_EQ(offs60, 1);
    // A note whose hold has ended frees its voice without a cut.
    IRPrefab::MidiNotes::queue(*s, pending(72, 30));
    sent.clear();
    run(*s, 1, sent);
    ASSERT_EQ(sent.size(), 1u);
    EXPECT_EQ(sent[0].status_, kNoteOnCh0);
    EXPECT_EQ(s->scheduledCount_, 1);
}

TEST(MidiNoteSpacingPlan, AStolenRepeatedPitchIsNotSilencedByItsOldHoldEnd) {
    // One voice, 60 -> 62 -> 60: the second 60 must sound through its own
    // hold, past the first 60's original hold end at frame 10.
    auto s = makeSpacing(2, 1);
    IRPrefab::MidiNotes::queue(*s, pending(60, 10));
    std::vector<Sent> sent;
    run(*s, 1, sent);
    IRPrefab::MidiNotes::queue(*s, pending(62, 10));
    run(*s, 1, sent); // 62 at 2 cuts 60
    IRPrefab::MidiNotes::queue(*s, pending(60, 10));
    run(*s, 30, sent); // the second 60 at 4 cuts 62, holds to 14
    EXPECT_TRUE(soundingAfter(sent, 60, 10));
    EXPECT_TRUE(soundingAfter(sent, 60, 13));
    EXPECT_FALSE(soundingAfter(sent, 60, 14));
    EXPECT_FALSE(soundingAfter(sent, 62, 4));
    EXPECT_EQ(withStatus(sent, kNoteOffCh0).size(), 3u);
}

TEST(MidiNoteSpacingPlan, ARetriggeredPitchEndsTheSoundingOneFirst) {
    // No voice limit: a second 60 while the first still sounds ends the first
    // where the second lands, and the first's hold end never goes out.
    auto s = makeSpacing(1);
    IRPrefab::MidiNotes::queue(*s, pending(60, 10));
    std::vector<Sent> sent;
    run(*s, 3, sent);
    IRPrefab::MidiNotes::queue(*s, pending(60, 10));
    run(*s, 20, sent); // the second 60 at 3, holds to 13
    ASSERT_EQ(sent.size(), 4u);
    EXPECT_EQ(sent[1].frame_, 3);
    EXPECT_EQ(sent[1].status_, kNoteOffCh0);
    EXPECT_EQ(sent[2].frame_, 3);
    EXPECT_EQ(sent[2].status_, kNoteOnCh0);
    EXPECT_EQ(sent[3].frame_, 13);
    EXPECT_TRUE(soundingAfter(sent, 60, 12));
}

TEST(MidiNoteSpacingPlan, TheDelayCapBunchesANoteRatherThanLosingIt) {
    auto s = makeSpacing(4, 0, 5);
    IRPrefab::MidiNotes::queue(*s, pending(60));
    IRPrefab::MidiNotes::queue(*s, pending(62));
    IRPrefab::MidiNotes::queue(*s, pending(64));
    std::vector<Sent> sent;
    run(*s, 20, sent);
    const std::vector<Sent> on = withStatus(sent, kNoteOnCh0);
    ASSERT_EQ(on.size(), 3u);
    EXPECT_EQ(on[0].frame_, 0);
    EXPECT_EQ(on[1].frame_, 4);
    EXPECT_EQ(on[2].frame_, 5); // 8 capped to 5
}

TEST(MidiNoteSpacingPlan, ChannelsAreNormalizedIntoTheStatus) {
    auto s = makeSpacing(1);
    IRPrefab::MidiNotes::queue(*s, {61, 80, 3, 2});
    std::vector<Sent> sent;
    run(*s, 5, sent);
    ASSERT_EQ(sent.size(), 2u);
    EXPECT_EQ(sent[0].status_, static_cast<std::uint8_t>(0x90 | 3));
    EXPECT_EQ(sent[1].status_, static_cast<std::uint8_t>(0x80 | 3));
}

TEST(MidiNoteSpacingPlan, ADenseFrameStaysWithinTheFixedCapacity) {
    // Each frame triggers more notes than the pending list holds, and by the
    // third frame the schedule is full: the overflow is counted, never grown
    // into.
    auto s = makeSpacing(1);
    constexpr int kPerFrame = C_MidiNoteSpacing::kMaxPendingNotes + 5;
    std::vector<Sent> sent;
    for (int frame = 0; frame < 3; ++frame) {
        for (int i = 0; i < kPerFrame; ++i) {
            IRPrefab::MidiNotes::queue(*s, pending(static_cast<std::uint8_t>(i % 128), 600));
        }
        EXPECT_EQ(s->pendingCount_, C_MidiNoteSpacing::kMaxPendingNotes);
        run(*s, 1, sent);
        EXPECT_LE(s->scheduledCount_, C_MidiNoteSpacing::kMaxScheduledNotes);
    }
    EXPECT_EQ(s->scheduledCount_, C_MidiNoteSpacing::kMaxScheduledNotes);
    EXPECT_EQ(s->droppedNotes_, 3 * 5 + C_MidiNoteSpacing::kMaxPendingNotes);
}
