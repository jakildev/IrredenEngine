#include <gtest/gtest.h>

#include <irreden/ir_entity.hpp>
#include <irreden/ir_system.hpp>
#include <irreden/ir_time.hpp>
#include <irreden/time/time_manager.hpp>

#include <irreden/audio/components/component_midi_delay.hpp>
#include <irreden/audio/components/component_midi_message.hpp>
#include <irreden/audio/components/component_midi_note.hpp>
#include <irreden/audio/components/component_midi_note_spacing.hpp>
#include <irreden/audio/midi_note_spacing.hpp>
#include <irreden/audio/systems/system_contact_midi_trigger.hpp>
#include <irreden/audio/systems/system_midi_note_spacing.hpp>
#include <irreden/audio/systems/system_periodic_idle_midi_trigger.hpp>
#include <irreden/common/components/component_tags_all.hpp>
#include <irreden/update/components/component_contact_event.hpp>

#include <cstdint>
#include <memory>
#include <set>
#include <stdexcept>
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

TEST(MidiNoteSpacingPlan, ALoweredVoiceCountCutsDownToItWhereTheNextNoteLands) {
    auto s = makeSpacing(1);
    IRPrefab::MidiNotes::queue(*s, pending(60, 30));
    IRPrefab::MidiNotes::queue(*s, pending(64, 30));
    IRPrefab::MidiNotes::queue(*s, pending(67, 30));
    std::vector<Sent> sent;
    run(*s, 3, sent); // 60 at 0, 64 at 1, 67 at 2, all sounding
    s->maxVoices_ = 1;
    IRPrefab::MidiNotes::queue(*s, pending(72, 30));
    run(*s, 1, sent); // 72 at 3 needs the one voice: all three end there
    for (const std::uint8_t note : {60, 64, 67}) {
        EXPECT_FALSE(soundingAfter(sent, note, 3)) << int(note);
    }
    EXPECT_TRUE(soundingAfter(sent, 72, 3));
    // The NOTE_OFFs go out before the NOTE_ON, and no old hold end follows.
    ASSERT_EQ(sent.size(), 7u);
    EXPECT_EQ(sent[6].status_, kNoteOnCh0);
    EXPECT_EQ(sent[6].note_, 72);
    run(*s, 40, sent);
    EXPECT_EQ(withStatus(sent, kNoteOffCh0).size(), 4u);
}

TEST(MidiNoteSpacingPlan, OffTheImmediateNoteEndsTheSpacedOneOfItsPitch) {
    // Spaced 60 holds to frame 10; the policy goes off at frame 3 and an
    // immediate 60 holds 20 frames. The spaced 60 ends before the immediate
    // NOTE_ON, and its hold end at 10 never goes out to silence the new 60.
    auto s = makeSpacing(1);
    IRPrefab::MidiNotes::TriggerPlan plan{};
    EXPECT_EQ(
        IRPrefab::MidiNotes::planTrigger(*s, C_MidiNote{60, 100, 0, 10.0f / 60.0f}, false, plan),
        0
    );
    std::vector<Sent> sent;
    run(*s, 3, sent);
    ASSERT_TRUE(soundingAfter(sent, 60, 2));
    s->enabled_ = false;
    const int count =
        IRPrefab::MidiNotes::planTrigger(*s, C_MidiNote{60, 90, 0, 20.0f / 60.0f}, false, plan);
    ASSERT_EQ(count, 3);
    EXPECT_EQ(plan[0].delayFrames_, 0);
    EXPECT_EQ(plan[0].status_, kNoteOffCh0);
    EXPECT_EQ(plan[1].delayFrames_, 0);
    EXPECT_EQ(plan[1].status_, kNoteOnCh0);
    EXPECT_EQ(plan[2].status_, kNoteOffCh0);
    EXPECT_EQ(plan[2].delayFrames_, 20);
    EXPECT_EQ(s->scheduledCount_, 0);
    sent.clear();
    run(*s, 20, sent); // through the spaced note's old hold end
    EXPECT_TRUE(sent.empty());
}

TEST(MidiNoteSpacingPlan, OffTheImmediateNoteDropsAQueuedOneOfItsPitch) {
    // Queued under the policy, not yet planned: the immediate 60 supersedes
    // it, so it never sounds after the immediate one; other notes keep going.
    auto s = makeSpacing(1);
    IRPrefab::MidiNotes::queue(*s, pending(60, 10));
    IRPrefab::MidiNotes::queue(*s, pending(64, 10));
    s->enabled_ = false;
    IRPrefab::MidiNotes::TriggerPlan plan{};
    const int count =
        IRPrefab::MidiNotes::planTrigger(*s, C_MidiNote{60, 90, 0, 20.0f / 60.0f}, false, plan);
    ASSERT_EQ(count, 2);
    EXPECT_EQ(plan[0].status_, kNoteOnCh0);
    std::vector<Sent> sent;
    run(*s, 20, sent);
    ASSERT_EQ(sent.size(), 2u);
    EXPECT_EQ(sent[0].note_, 64);
    EXPECT_EQ(sent[1].note_, 64);
}

TEST(MidiNoteSpacingPlan, ALoweredVoiceCountHoldsWhereAnAlreadyPlannedNoteLands) {
    // 60, 64, 67 planned at 0, 10, 20 with no cap; the cap drops to one after
    // frame 0 and nothing new is queued. Each planned note still takes the
    // one voice where it lands.
    auto s = makeSpacing(10);
    IRPrefab::MidiNotes::queue(*s, pending(60, 100));
    IRPrefab::MidiNotes::queue(*s, pending(64, 100));
    IRPrefab::MidiNotes::queue(*s, pending(67, 100));
    std::vector<Sent> sent;
    run(*s, 1, sent);
    s->maxVoices_ = 1;
    run(*s, 30, sent);
    for (long frame = 0; frame < 30; ++frame) {
        int sounding = 0;
        for (const std::uint8_t note : {60, 64, 67}) {
            sounding += soundingAfter(sent, note, frame) ? 1 : 0;
        }
        EXPECT_EQ(sounding, 1) << frame;
    }
    ASSERT_EQ(sent.size(), 5u);
    EXPECT_EQ(sent[1].frame_, 10);
    EXPECT_EQ(sent[1].status_, kNoteOffCh0);
    EXPECT_EQ(sent[1].note_, 60);
    EXPECT_EQ(sent[2].note_, 64);
    EXPECT_EQ(sent[3].frame_, 20);
    EXPECT_EQ(sent[3].status_, kNoteOffCh0);
    EXPECT_EQ(sent[3].note_, 64);
    EXPECT_EQ(sent[4].status_, kNoteOnCh0);
    EXPECT_EQ(sent[4].note_, 67);
    // The cut notes' old hold ends never go out.
    run(*s, 150, sent);
    EXPECT_EQ(withStatus(sent, kNoteOffCh0).size(), 3u);
}

TEST(MidiNoteSpacingPlan, ANoteCutTheFrameItLandsNeverSounds) {
    // The delay cap bunches 62 and 64 onto frame 3 under one voice: 62 lands
    // and 64 takes its voice there, so 62 sends nothing at all.
    auto s = makeSpacing(4, 1, 3);
    IRPrefab::MidiNotes::queue(*s, pending(60, 30));
    IRPrefab::MidiNotes::queue(*s, pending(62, 30));
    IRPrefab::MidiNotes::queue(*s, pending(64, 30));
    std::vector<Sent> sent;
    run(*s, 40, sent);
    ASSERT_EQ(sent.size(), 4u);
    EXPECT_EQ(sent[1].frame_, 3);
    EXPECT_EQ(sent[1].status_, kNoteOffCh0);
    EXPECT_EQ(sent[1].note_, 60);
    EXPECT_EQ(sent[2].frame_, 3);
    EXPECT_EQ(sent[2].status_, kNoteOnCh0);
    EXPECT_EQ(sent[2].note_, 64);
    EXPECT_EQ(sent[3].note_, 64);
    for (const Sent &m : sent) {
        EXPECT_NE(m.note_, 62);
    }
}

TEST(MidiNoteSpacingPlan, OwnedImmediateHoldEndsOnItsFrame) {
    auto s = makeSpacing(1);
    s->enabled_ = false;
    IRPrefab::MidiNotes::TriggerPlan plan{};
    ASSERT_EQ(IRPrefab::MidiNotes::planTrigger(*s, C_MidiNote{60, 90, 0, 0.1f}, true, plan), 1);
    EXPECT_EQ(plan[0].status_, kNoteOnCh0);
    EXPECT_EQ(plan[0].delayFrames_, 0);
    EXPECT_EQ(s->scheduledCount_, 1);
    std::vector<Sent> sent;
    run(*s, 20, sent);
    ASSERT_EQ(sent.size(), 1u);
    EXPECT_EQ(sent[0].frame_, 6);
    EXPECT_EQ(sent[0].status_, kNoteOffCh0);
    EXPECT_EQ(s->scheduledCount_, 0);
}

TEST(MidiNoteSpacingPlan, OnASpacedNoteCutsTheImmediateHoldOfItsPitch) {
    // Off: an immediate 60 holds to frame 20, its hold end owned by the
    // schedule. On at frame 2: a spaced 60 lands there, ends the immediate one
    // first, and the old hold end at 20 never silences it.
    auto s = makeSpacing(1);
    s->enabled_ = false;
    IRPrefab::MidiNotes::TriggerPlan plan{};
    ASSERT_EQ(
        IRPrefab::MidiNotes::planTrigger(*s, C_MidiNote{60, 90, 0, 20.0f / 60.0f}, true, plan),
        1
    );
    std::vector<Sent> sent;
    run(*s, 2, sent);
    EXPECT_TRUE(sent.empty());
    s->enabled_ = true;
    ASSERT_EQ(
        IRPrefab::MidiNotes::planTrigger(*s, C_MidiNote{60, 100, 0, 30.0f / 60.0f}, true, plan),
        0
    );
    run(*s, 40, sent);
    ASSERT_EQ(sent.size(), 3u);
    EXPECT_EQ(sent[0].frame_, 2);
    EXPECT_EQ(sent[0].status_, kNoteOffCh0);
    EXPECT_EQ(sent[1].frame_, 2);
    EXPECT_EQ(sent[1].status_, kNoteOnCh0);
    EXPECT_EQ(sent[2].frame_, 32);
    EXPECT_TRUE(soundingAfter(sent, 60, 20));
}

TEST(MidiNoteSpacingPlan, OffAnImmediateRetriggerEndsTheOwnedHoldFirst) {
    auto s = makeSpacing(1);
    s->enabled_ = false;
    IRPrefab::MidiNotes::TriggerPlan plan{};
    ASSERT_EQ(
        IRPrefab::MidiNotes::planTrigger(*s, C_MidiNote{60, 90, 0, 20.0f / 60.0f}, true, plan),
        1
    );
    std::vector<Sent> sent;
    run(*s, 3, sent);
    ASSERT_EQ(
        IRPrefab::MidiNotes::planTrigger(*s, C_MidiNote{60, 90, 0, 10.0f / 60.0f}, true, plan),
        2
    );
    EXPECT_EQ(plan[0].status_, kNoteOffCh0);
    EXPECT_EQ(plan[1].status_, kNoteOnCh0);
    run(*s, 30, sent);
    ASSERT_EQ(sent.size(), 1u);
    EXPECT_EQ(sent[0].frame_, 13);
    EXPECT_EQ(sent[0].status_, kNoteOffCh0);
}

// The trigger, the spacing system and an output probe in UPDATE order: the
// messages staged inside the ticks reach the later stage the frame they are
// due, and turning the policy on mid-hold never lets the immediate note's
// hold end silence the spaced retrigger.
class MidiNoteSpacingPipelineTest : public testing::Test {
  protected:
    MidiNoteSpacingPipelineTest() {
        const IRSystem::SystemId probe = IRSystem::createSystem<IRComponents::C_MidiMessage>(
            "MidiOutProbe",
            [this](IREntity::EntityId id, const IRComponents::C_MidiMessage &m) {
                if (m_seen.insert(id).second) {
                    m_sent.push_back({m_frame, m.status_, m.data1_});
                }
            }
        );
        IRSystem::addSystemTag<IRComponents::C_MidiOut>(probe);
        m_systemManager.registerPipeline(
            IRTime::Events::UPDATE,
            {IRSystem::createSystem<IRSystem::CONTACT_MIDI_TRIGGER>(),
             IRSystem::createSystem<IRSystem::MIDI_NOTE_SPACING>(),
             probe}
        );
    }

    void tick() {
        m_systemManager.executePipeline(IRTime::Events::UPDATE);
        ++m_frame;
    }

    IRTime::TimeManager m_timeManager;
    IREntity::EntityManager m_entityManager;
    IRSystem::SystemManager m_systemManager;
    long m_frame = 0;
    std::set<IREntity::EntityId> m_seen;
    std::vector<Sent> m_sent;
};

TEST_F(MidiNoteSpacingPipelineTest, EnablingMidHoldNeverSilencesTheSpacedRetrigger) {
    const IREntity::EntityId source = IREntity::createEntity(
        IRComponents::C_ContactEvent{},
        C_MidiNote{60, 90, 0, 20.0f / 60.0f}
    );
    IREntity::getComponent<IRComponents::C_ContactEvent>(source).entered_ = true;
    tick(); // frame 0: the immediate 60, its hold end owned by the schedule
    IREntity::getComponent<IRComponents::C_ContactEvent>(source).entered_ = false;
    tick();
    IRPrefab::MidiNotes::spacing().enabled_ = true;
    IREntity::getComponent<C_MidiNote>(source).holdSeconds_ = 30.0f / 60.0f;
    IREntity::getComponent<IRComponents::C_ContactEvent>(source).entered_ = true;
    tick(); // frame 2: the spaced 60 lands
    IREntity::getComponent<IRComponents::C_ContactEvent>(source).entered_ = false;
    for (int i = 0; i < 40; ++i) {
        tick();
    }
    ASSERT_EQ(m_sent.size(), 4u);
    EXPECT_EQ(m_sent[0].frame_, 0);
    EXPECT_EQ(m_sent[0].status_, kNoteOnCh0);
    EXPECT_EQ(m_sent[1].frame_, 2);
    EXPECT_EQ(m_sent[1].status_, kNoteOffCh0);
    EXPECT_EQ(m_sent[2].frame_, 2);
    EXPECT_EQ(m_sent[2].status_, kNoteOnCh0);
    EXPECT_EQ(m_sent[3].frame_, 32);
    EXPECT_EQ(m_sent[3].status_, kNoteOffCh0);
    EXPECT_TRUE(soundingAfter(m_sent, 60, 20));
    int delayed = 0;
    IRSystem::executeQuery<IRComponents::C_MidiDelay>(
        [&delayed](const IRComponents::C_MidiDelay &) { ++delayed; }
    );
    EXPECT_EQ(delayed, 0);
}

// Both triggers write the `C_MidiNoteSpacing` singleton the spacing system
// plans from, so the group validator keeps each of them out of the spacing
// system's group.
class MidiNoteSpacingGroupTest : public testing::Test {
  protected:
    IRTime::TimeManager m_timeManager;
    IREntity::EntityManager m_entityManager;
    IRSystem::SystemManager m_systemManager;
};

TEST_F(MidiNoteSpacingGroupTest, TheTriggersAndTheSpacingSystemPassInSeparateGroups) {
    m_systemManager.registerPipelineGroups(
        IRTime::Events::UPDATE,
        {{IRSystem::createSystem<IRSystem::CONTACT_MIDI_TRIGGER>()},
         {IRSystem::createSystem<IRSystem::PERIODIC_IDLE_MIDI_TRIGGER>()},
         {IRSystem::createSystem<IRSystem::MIDI_NOTE_SPACING>()}}
    );
    EXPECT_NO_THROW(m_systemManager.validateAllPipelineGroups());
}

TEST_F(MidiNoteSpacingGroupTest, TheContactTriggerCannotShareTheSpacingSystemsGroup) {
    m_systemManager.registerPipelineGroups(
        IRTime::Events::UPDATE,
        {{IRSystem::createSystem<IRSystem::CONTACT_MIDI_TRIGGER>(),
          IRSystem::createSystem<IRSystem::MIDI_NOTE_SPACING>()}}
    );
    EXPECT_THROW(m_systemManager.validateAllPipelineGroups(), std::runtime_error);
}

TEST_F(MidiNoteSpacingGroupTest, ThePeriodicTriggerCannotShareTheSpacingSystemsGroup) {
    m_systemManager.registerPipelineGroups(
        IRTime::Events::UPDATE,
        {{IRSystem::createSystem<IRSystem::PERIODIC_IDLE_MIDI_TRIGGER>(),
          IRSystem::createSystem<IRSystem::MIDI_NOTE_SPACING>()}}
    );
    EXPECT_THROW(m_systemManager.validateAllPipelineGroups(), std::runtime_error);
}
