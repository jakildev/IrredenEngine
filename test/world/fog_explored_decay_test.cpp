#include <gtest/gtest.h>

#include <irreden/asset/binary_io.hpp>
#include <irreden/ir_job.hpp>
#include <irreden/job/job_manager.hpp>
#include <irreden/asset/chunk_header.hpp>
#include <irreden/profile/logger_spd.hpp>
#include <irreden/render/components/component_canvas_fog_of_war.hpp>
#include <irreden/render/fog_of_war.hpp>
#include <irreden/render/fog_world_field.hpp>
#include <irreden/spatial/chunked_field.hpp>
#include <irreden/world/field_chunk_persistence.hpp>

#include <spdlog/sinks/ostream_sink.h>

#include "common/allocation_counter.hpp"
#include "common/fog_save_root.hpp"
#include "common/fog_window_image.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace {

using IRComponents::C_CanvasFogOfWar;
using IRComponents::FrameDataFogObservers;
using IRComponents::kFogChannelDefault;
using IRComponents::kFogStateExplored;
using IRComponents::kFogStateUnexplored;
using IRComponents::kFogStateVisible;
using IRPrefab::Fog::ExploredPolicy;
using IRPrefab::Fog::FogWindowTexel;
using IRPrefab::Fog::WorldField;
using IRPrefab::Fog::detail::expandWindowChunks;
using IRPrefab::Fog::detail::planWindowGather;
using IRPrefab::Fog::detail::WindowGatherPlan;
using IRPrefab::Fog::detail::WindowUploadRect;
using IRPrefab::Spatial::FieldChunkKey;
using IRPrefab::Spatial::fieldChunkOf;
using IRPrefab::Spatial::kFieldChunkCells;
using IRPrefab::Spatial::kFieldChunkEdge;
using IRPrefab::Spatial::packFieldChunkKey;
using IRWorld::FieldChunkDiskPersistence;

constexpr std::uint64_t kDuration = 1000;
constexpr std::uint32_t kOtherChannel = 1u << 1u;
constexpr std::uint32_t kHighChannel = 1u << 31u;
const IRMath::ivec2 kWindowOrigin{-128, -128};
constexpr int kWindowEdge = 256;

class EngineLogCapture {
  public:
    EngineLogCapture()
        : m_logger{LoggerSpd::instance()->getEngineLogger()}
        , m_sink{std::make_shared<spdlog::sinks::ostream_sink_st>(m_text)} {
        m_logger->sinks().push_back(m_sink);
    }

    ~EngineLogCapture() {
        auto &sinks = m_logger->sinks();
        sinks.erase(std::remove(sinks.begin(), sinks.end(), m_sink), sinks.end());
    }

    std::string text() {
        m_logger->flush();
        return m_text.str();
    }

  private:
    std::ostringstream m_text;
    spdlog::logger *m_logger;
    std::shared_ptr<spdlog::sinks::sink> m_sink;
};

void expandWholeWindow(
    WorldField &field, IRMath::ivec2 origin, int edge, std::vector<FogWindowTexel> &image
) {
    IRTest::expandWholeFogWindow(field, origin, edge, image);
}

FogWindowTexel texelOf(const std::vector<FogWindowTexel> &image, IRMath::ivec2 column, int edge) {
    return IRTest::fogWindowTexelOf(image, column, edge);
}

std::int64_t discCellCount(std::int64_t radius) {
    std::int64_t count = 0;
    for (std::int64_t dy = -radius; dy <= radius; ++dy) {
        for (std::int64_t dx = -radius; dx <= radius; ++dx) {
            count += dx * dx + dy * dy <= radius * radius ? 1 : 0;
        }
    }
    return count;
}

class FogWorldFieldDecayTest : public ::testing::Test {
  protected:
    static WorldField decaying(std::uint32_t channels = kFogChannelDefault) {
        WorldField field;
        EXPECT_TRUE(field.setExploredPolicy(ExploredPolicy::DECAY, kDuration, channels));
        return field;
    }

    void persist(WorldField &field, const std::string &subdirectory = "") const {
        ASSERT_TRUE(field.setPersistence(m_root.store(subdirectory)));
    }

    IRTest::ScopedFogSaveRoot m_root;
};

// The deterministic policy fire: eligible EXPLORED memory goes 128 -> 0 at
// exactly the duration, and the persistent and disjoint-mask controls keep
// it.
TEST_F(FogWorldFieldDecayTest, ExploredMemoryExpiresAtExactlyTheDuration) {
    WorldField field = decaying();
    const IRMath::ivec2 single{7, -3};
    const IRMath::ivec2 disjoint{40, 40};
    ASSERT_TRUE(field.setCell(single, kFogStateExplored));
    field.setCellChannels(disjoint, kOtherChannel);
    ASSERT_TRUE(field.setCell(disjoint, kFogStateExplored));
    EXPECT_GT(field.exploreRadius({-200, 300}, 5), 0);
    field.stats();

    EXPECT_TRUE(field.setExploredTimeMs(kDuration - 1));
    EXPECT_EQ(field.getCell(single), kFogStateExplored);
    EXPECT_EQ(field.getCell({-200, 300}), kFogStateExplored);
    EXPECT_EQ(field.stats().expired_, 0);

    EXPECT_TRUE(field.setExploredTimeMs(kDuration));
    EXPECT_EQ(field.getCell(single), kFogStateUnexplored);
    EXPECT_EQ(field.getCell({-200, 300}), kFogStateUnexplored);
    EXPECT_EQ(field.getCell({-200 + 5, 300}), kFogStateUnexplored);
    EXPECT_EQ(field.getCell(disjoint), kFogStateExplored) << "a disjoint cell mask never decays";
    EXPECT_EQ(field.stats().expired_, 1 + discCellCount(5));

    WorldField persistent;
    ASSERT_TRUE(persistent.setExploredPolicy(ExploredPolicy::PERSISTENT, 0));
    persistent.setCell(single, kFogStateExplored);
    EXPECT_TRUE(persistent.setExploredTimeMs(10 * kDuration));
    EXPECT_EQ(persistent.getCell(single), kFogStateExplored);
    EXPECT_EQ(persistent.stats().expired_, 0);

    WorldField defaulted;
    defaulted.setCell(single, kFogStateExplored);
    EXPECT_TRUE(defaulted.setExploredTimeMs(10 * kDuration));
    EXPECT_EQ(defaulted.getCell(single), kFogStateExplored) << "PERSISTENT is the default";
}

// Re-exploring refreshes the deadline even when the byte does not change;
// VISIBLE and UNEXPLORED writes drop the recorded time.
TEST_F(FogWorldFieldDecayTest, ExploringAgainRefreshesTheDeadline) {
    WorldField field = decaying();
    const IRMath::ivec2 cell{3, 3};
    field.setCell(cell, kFogStateExplored);
    EXPECT_TRUE(field.setExploredTimeMs(500));
    EXPECT_FALSE(field.setCell(cell, kFogStateExplored)) << "the byte is unchanged";
    EXPECT_TRUE(field.setExploredTimeMs(kDuration));
    EXPECT_EQ(field.getCell(cell), kFogStateExplored) << "refreshed at 500";
    EXPECT_TRUE(field.setExploredTimeMs(500 + kDuration - 1));
    EXPECT_EQ(field.getCell(cell), kFogStateExplored);
    EXPECT_TRUE(field.setExploredTimeMs(500 + kDuration));
    EXPECT_EQ(field.getCell(cell), kFogStateUnexplored);

    // exploreRadius over an already-explored disc changes nothing but refreshes.
    const IRMath::ivec2 centre{100, 100};
    EXPECT_GT(field.exploreRadius(centre, 3), 0);
    EXPECT_TRUE(field.setExploredTimeMs(2000));
    EXPECT_EQ(field.exploreRadius(centre, 3), 0);
    EXPECT_TRUE(field.setExploredTimeMs(2000 + kDuration - 1));
    EXPECT_EQ(field.getCell(centre), kFogStateExplored);
    EXPECT_TRUE(field.setExploredTimeMs(2000 + kDuration));
    EXPECT_EQ(field.getCell(centre), kFogStateUnexplored);

    // A VISIBLE write is not explored memory and never decays.
    field.setCell(cell, kFogStateVisible);
    EXPECT_TRUE(field.setExploredTimeMs(10 * kDuration));
    EXPECT_EQ(field.getCell(cell), kFogStateVisible);
}

TEST_F(FogWorldFieldDecayTest, ClockAndPolicyRejectInvalidTransitionsWithoutMutation) {
    WorldField field;
    EXPECT_FALSE(field.setExploredPolicy(ExploredPolicy::DECAY, 0));
    EXPECT_FALSE(field.setExploredPolicy(ExploredPolicy::DECAY, IRPrefab::Fog::kFogTimeMsMax + 1));
    EXPECT_FALSE(field.setExploredPolicy(static_cast<ExploredPolicy>(7), 10));
    EXPECT_EQ(field.exploredPolicy().policy_, ExploredPolicy::PERSISTENT);
    EXPECT_TRUE(field.setExploredPolicy(ExploredPolicy::DECAY, kDuration, kOtherChannel));
    EXPECT_EQ(field.exploredPolicy().durationMs_, kDuration);
    EXPECT_EQ(field.exploredPolicy().channels_, kOtherChannel);

    EXPECT_TRUE(field.setExploredTimeMs(5));
    EXPECT_TRUE(field.setExploredTimeMs(5)) << "equal time is a no-op";
    EXPECT_FALSE(field.setExploredTimeMs(4));
    EXPECT_FALSE(field.setExploredTimeMs(IRPrefab::Fog::kFogTimeMsMax + 1));
    EXPECT_EQ(field.exploredTimeMs(), 5u);
    EXPECT_TRUE(field.setExploredTimeMs(IRPrefab::Fog::kFogTimeMsMax));

    // Once a cell exists only a repeat of the settings is accepted.
    WorldField used = decaying();
    used.setCell({1, 1}, kFogStateExplored);
    EXPECT_FALSE(used.setExploredPolicy(ExploredPolicy::PERSISTENT, 0));
    EXPECT_FALSE(used.setExploredPolicy(ExploredPolicy::DECAY, kDuration + 1));
    EXPECT_TRUE(used.setExploredPolicy(ExploredPolicy::DECAY, kDuration));
    EXPECT_EQ(used.exploredPolicy().durationMs_, kDuration);
    used.clear();
    EXPECT_TRUE(used.setExploredPolicy(ExploredPolicy::PERSISTENT, 0));
    EXPECT_EQ(used.exploredPolicy().policy_, ExploredPolicy::PERSISTENT);

    // A region record also counts as use.
    WorldField probed;
    persist(probed);
    EXPECT_EQ(probed.getCell({0, 0}), kFogStateUnexplored);
    EXPECT_FALSE(probed.setExploredPolicy(ExploredPolicy::DECAY, kDuration));
}

TEST_F(FogWorldFieldDecayTest, CellChannelsCarryEveryBitAndDefaultWhenAbsent) {
    WorldField field;
    const IRMath::ivec2 cell{10, 20};
    EXPECT_EQ(field.getCellChannels(cell), kFogChannelDefault);
    EXPECT_FALSE(field.setCellChannels(cell, kFogChannelDefault))
        << "the default on an absent chunk stores nothing";
    EXPECT_EQ(field.metadataChunkCount(), 0u);
    EXPECT_TRUE(field.setCellChannels(cell, kHighChannel));
    EXPECT_EQ(field.getCellChannels(cell), kHighChannel);
    EXPECT_TRUE(field.setCellChannels(cell, 0xFFFFFFFFu));
    EXPECT_EQ(field.getCellChannels(cell), 0xFFFFFFFFu);
    EXPECT_TRUE(field.setCellChannels(cell, 0u));
    EXPECT_EQ(field.getCellChannels(cell), 0u);
    EXPECT_FALSE(field.setCellChannels(cell, 0u));
    EXPECT_EQ(field.getCellChannels(cell + IRMath::ivec2{1, 0}), kFogChannelDefault)
        << "an untouched cell of a present mask chunk reads the default";
    EXPECT_TRUE(field.setCellChannels(cell, kFogChannelDefault));
    EXPECT_EQ(field.getCellChannels(cell), kFogChannelDefault);

    constexpr int kMax = std::numeric_limits<int>::max();
    constexpr int kMin = std::numeric_limits<int>::min();
    EXPECT_TRUE(field.setCellChannels({kMax, kMin}, kOtherChannel));
    EXPECT_EQ(field.getCellChannels({kMax, kMin}), kOtherChannel);
    EXPECT_EQ(field.peekCellChannels({kMax, kMin}), kOtherChannel);
    EXPECT_GT(field.exploreRadius({kMax - 1, kMin + 1}, 4, kOtherChannel), 0);
    EXPECT_EQ(field.getCell({kMax, kMin}), kFogStateExplored);
    EXPECT_EQ(field.getCell({kMax - 1, kMin + 1}), kFogStateUnexplored)
        << "a default cell is not admitted by a disjoint source";
}

TEST_F(FogWorldFieldDecayTest, RevealAndExploreRadiusAdmitOnlyIntersectingCells) {
    WorldField field;
    const IRMath::ivec2 centre{0, 0};
    const IRMath::ivec2 other{2, 1};
    const IRMath::ivec2 closed{-2, -1};
    field.setCellChannels(other, kOtherChannel);
    field.setCellChannels(closed, 0u);

    EXPECT_EQ(field.revealRadius(centre, 4), discCellCount(4) - 2);
    EXPECT_EQ(field.getCell(centre), kFogStateVisible);
    EXPECT_EQ(field.getCell(other), kFogStateUnexplored);
    EXPECT_EQ(field.getCell(closed), kFogStateUnexplored);
    EXPECT_EQ(field.revealRadius(centre, 4, kOtherChannel), 1);
    EXPECT_EQ(field.getCell(other), kFogStateVisible);
    EXPECT_EQ(field.revealRadius(centre, 4, 0u), 0) << "a zero source mask admits nothing";

    // exploreRadius keeps VISIBLE cells and marks the rest of the admitted disc.
    const IRMath::ivec2 far{300, 300};
    field.revealRadius(far, 2);
    field.setCellChannels(far + IRMath::ivec2{5, 0}, 0u);
    EXPECT_EQ(field.exploreRadius(far, 6), discCellCount(6) - discCellCount(2) - 1);
    EXPECT_EQ(field.getCell(far), kFogStateVisible);
    EXPECT_EQ(field.getCell(far + IRMath::ivec2{3, 0}), kFogStateExplored);
    EXPECT_EQ(field.getCell(far + IRMath::ivec2{5, 0}), kFogStateUnexplored);
    EXPECT_EQ(field.getCell(far + IRMath::ivec2{7, 0}), kFogStateUnexplored);
    EXPECT_EQ(field.exploreRadius(far, -1), 0);
    EXPECT_EQ(field.exploreRadius(far, 6), 0) << "an explored disc is unchanged";
    field.setCell(far + IRMath::ivec2{3, 0}, kFogStateExplored);
    EXPECT_EQ(field.getCell(far + IRMath::ivec2{3, 0}), kFogStateExplored)
        << "setCell is the explicit demotion route and stays";
}

// A mask edit resolves expiry under the old mask first, then under the new
// one: a cell that becomes eligible expires at once if due, and removing a bit
// cannot resurrect memory that already expired.
TEST_F(FogWorldFieldDecayTest, MaskEditsResolveExpiryUnderTheOldMaskFirst) {
    WorldField field = decaying();
    const IRMath::ivec2 shielded{5, 5};
    const IRMath::ivec2 plain{6, 6};
    field.setCellChannels(shielded, kOtherChannel);
    field.setCell(shielded, kFogStateExplored);
    field.setCell(plain, kFogStateExplored);
    EXPECT_TRUE(field.setExploredTimeMs(2 * kDuration));
    EXPECT_EQ(field.getCell(shielded), kFogStateExplored);
    EXPECT_EQ(field.getCell(plain), kFogStateUnexplored);

    field.setCellChannels(shielded, kOtherChannel | kFogChannelDefault);
    EXPECT_EQ(field.getCell(shielded), kFogStateUnexplored)
        << "a newly eligible old cell expires immediately";

    field.setCellChannels(plain, kOtherChannel);
    EXPECT_EQ(field.getCell(plain), kFogStateUnexplored) << "no resurrection";

    // Shielding a live cell holds it; unshielding it later expires it on its
    // original time, not the edit's.
    const IRMath::ivec2 held{8, 8};
    field.setCell(held, kFogStateExplored);
    EXPECT_TRUE(field.setExploredTimeMs(2 * kDuration + 500));
    field.setCellChannels(held, kOtherChannel);
    EXPECT_TRUE(field.setExploredTimeMs(4 * kDuration));
    EXPECT_EQ(field.getCell(held), kFogStateExplored);
    field.setCellChannels(held, kFogChannelDefault);
    EXPECT_EQ(field.getCell(held), kFogStateUnexplored);
}

TEST_F(FogWorldFieldDecayTest, TransientUnionsAreAdmittedPerCellMask) {
    WorldField field;
    const IRMath::ivec2 a{0, 0};
    const IRMath::ivec2 b{6, 0};
    field.setCellChannels({3, 0}, kOtherChannel);
    field.setCellChannels({1, 0}, kOtherChannel);
    field.setCellChannels({0, 1}, 0u);
    EXPECT_EQ(field.stampTransientDisc(IRMath::vec2(a), 4.0f), discCellCount(4));
    EXPECT_EQ(field.stampTransientDisc(IRMath::vec2(b), 4.0f, kOtherChannel), discCellCount(4));
    EXPECT_EQ(field.stampTransientDisc(IRMath::vec2(a), 4.0f, 0u), 0)
        << "a zero mask stamps nothing";
    EXPECT_EQ(field.stampTransientDisc(IRMath::vec2(a), 4.0f), 0) << "the bits are already present";

    EXPECT_EQ(field.getCell(a), kFogStateVisible);
    EXPECT_EQ(field.getCell({1, 0}), kFogStateUnexplored) << "under A only, mask 0b10";
    EXPECT_EQ(field.getCell({3, 0}), kFogStateVisible) << "in both discs: the union admits it";
    EXPECT_EQ(field.getCell({0, 1}), kFogStateUnexplored) << "mask 0 admits no source";
    EXPECT_EQ(field.getCell(b), kFogStateUnexplored) << "under B only, default mask";
    EXPECT_EQ(field.getCell({4, 0}), kFogStateVisible) << "default cell under both";

    std::vector<FogWindowTexel> image;
    expandWholeWindow(field, kWindowOrigin, kWindowEdge, image);
    EXPECT_EQ(
        texelOf(image, a, kWindowEdge),
        (FogWindowTexel{kFogStateVisible, kFogChannelDefault})
    );
    EXPECT_EQ(
        texelOf(image, {1, 0}, kWindowEdge),
        (FogWindowTexel{kFogStateUnexplored, kOtherChannel})
    );
    EXPECT_EQ(
        texelOf(image, {3, 0}, kWindowEdge),
        (FogWindowTexel{kFogStateVisible, kOtherChannel})
    );
    EXPECT_EQ(texelOf(image, {0, 1}, kWindowEdge), (FogWindowTexel{kFogStateUnexplored, 0u}));
    EXPECT_EQ(
        texelOf(image, b, kWindowEdge),
        (FogWindowTexel{kFogStateUnexplored, kFogChannelDefault})
    );

    // A mask edit under a live disc re-admits without restamping, and the
    // mask field feeds the pending set so the gather re-expands the chunk.
    std::vector<FieldChunkKey> pending;
    field.consumePending(pending);
    field.setCellChannels(b, kOtherChannel);
    EXPECT_EQ(field.getCell(b), kFogStateVisible);
    field.consumePending(pending);
    EXPECT_TRUE(
        std::binary_search(pending.begin(), pending.end(), packFieldChunkKey(fieldChunkOf(b)))
    );

    field.clearTransient();
    EXPECT_EQ(field.getCell(a), kFogStateUnexplored);
    EXPECT_EQ(field.getCell({3, 0}), kFogStateUnexplored);
}

// Expired memory under a still-visible admitted tier source reads visible
// through the tier, and clearing the source leaves UNEXPLORED, never the
// expired 128.
TEST_F(FogWorldFieldDecayTest, ExpiredMemoryUnderALiveSourceDoesNotResurrect) {
    WorldField field = decaying();
    const IRMath::ivec2 cell{12, 12};
    field.setCell(cell, kFogStateExplored);
    field.stampTransientDisc(IRMath::vec2(cell), 2.0f);
    EXPECT_EQ(field.getCell(cell), kFogStateVisible);
    EXPECT_TRUE(field.setExploredTimeMs(kDuration));
    EXPECT_EQ(field.peekCell(cell), std::optional<std::uint8_t>{kFogStateVisible});
    EXPECT_EQ(field.stats().expired_, 1);
    field.clearTransient();
    EXPECT_EQ(field.getCell(cell), kFogStateUnexplored);
}

// The eighth (analytic) and ninth (field-tier) sources both obey cell-mask
// admission: the named positive control reveals on both sides of the tier
// boundary for matching masks, and disjoint or zero masks reveal nothing.
TEST_F(FogWorldFieldDecayTest, MatchingSourceRevealsOnBothSidesOfTierBoundary) {
    constexpr std::uint32_t kSourceMask = kOtherChannel;
    WorldField field;
    FrameDataFogObservers observers;
    const auto admit = [&](int index, std::uint32_t channels) {
        return C_CanvasFogOfWar::addVisionCircle(
            observers,
            field,
            static_cast<float>(index * 100),
            0.0f,
            4.0f,
            0.0f,
            0.0f,
            0.0f,
            IRComponents::kFogVisionZCostMirrorUp,
            0.0f,
            channels
        );
    };
    for (int i = 0; i < IRComponents::kMaxFogVisionCircles - 1; ++i) {
        ASSERT_EQ(admit(i, kSourceMask), i);
    }
    const int eighth = IRComponents::kMaxFogVisionCircles - 1;
    const int ninth = IRComponents::kMaxFogVisionCircles;
    const IRMath::ivec2 ninthCentre{ninth * 100, 0};
    field.setCellChannels(ninthCentre + IRMath::ivec2{1, 0}, 0u);
    field.setCellChannels(ninthCentre + IRMath::ivec2{2, 0}, kFogChannelDefault);
    field.setCellChannels(ninthCentre, kSourceMask | kHighChannel);
    ASSERT_EQ(admit(eighth, kSourceMask), eighth);
    ASSERT_EQ(admit(ninth, kSourceMask), -1);

    // Eighth source, analytic tier: the sampled cell mask gates the reveal.
    const IRMath::vec3 eighthPoint(static_cast<float>(eighth * 100), 0.0f, 0.0f);
    EXPECT_FLOAT_EQ(
        IRPrefab::Fog::evalReveal(observers, {}, kFogStateUnexplored, eighthPoint, kSourceMask),
        1.0f
    );
    EXPECT_FLOAT_EQ(
        IRPrefab::Fog::evalReveal(
            observers,
            {},
            kFogStateUnexplored,
            eighthPoint,
            kSourceMask | kHighChannel
        ),
        1.0f
    );
    EXPECT_FLOAT_EQ(
        IRPrefab::Fog::evalReveal(
            observers,
            {},
            kFogStateUnexplored,
            eighthPoint,
            kFogChannelDefault
        ),
        0.0f
    ) << "the default cell mask does not intersect the source";
    EXPECT_FLOAT_EQ(
        IRPrefab::Fog::evalReveal(observers, {}, kFogStateUnexplored, eighthPoint, 0u),
        0.0f
    );

    // Ninth source, field tier: only cells whose mask intersects read visible.
    int revealed = 0;
    for (int dy = -5; dy <= 5; ++dy) {
        for (int dx = -5; dx <= 5; ++dx) {
            revealed += field.getCell(ninthCentre + IRMath::ivec2{dx, dy}) == kFogStateVisible;
        }
    }
    EXPECT_EQ(revealed, 1) << "only the one cell carrying the source's bit";
    EXPECT_EQ(field.getCell(ninthCentre), kFogStateVisible);
    EXPECT_EQ(field.getCell(ninthCentre + IRMath::ivec2{1, 0}), kFogStateUnexplored) << "mask 0";
    EXPECT_EQ(field.getCell(ninthCentre + IRMath::ivec2{2, 0}), kFogStateUnexplored)
        << "default mask";

    // The transition with matching default masks on both tiers.
    WorldField plain;
    FrameDataFogObservers plainObservers;
    int plainRevealed = 0;
    for (int i = 0; i <= ninth; ++i) {
        C_CanvasFogOfWar::addVisionCircle(
            plainObservers,
            plain,
            static_cast<float>(i * 100),
            0.0f,
            4.0f,
            0.0f,
            0.0f,
            0.0f,
            IRComponents::kFogVisionZCostMirrorUp,
            0.0f
        );
    }
    for (int dy = -5; dy <= 5; ++dy) {
        for (int dx = -5; dx <= 5; ++dx) {
            plainRevealed += plain.getCell(ninthCentre + IRMath::ivec2{dx, dy}) == kFogStateVisible;
        }
    }
    EXPECT_EQ(plainRevealed, discCellCount(4));
    EXPECT_FLOAT_EQ(
        IRPrefab::Fog::evalReveal(plainObservers, {}, kFogStateUnexplored, eighthPoint),
        1.0f
    );
    EXPECT_FLOAT_EQ(
        IRPrefab::Fog::evalReveal(plainObservers, {}, kFogStateVisible, eighthPoint, 0u),
        1.0f
    ) << "an admitted VISIBLE grid cell reveals a BODY regardless of its mask";
}

// An evicted cell ages off-camera: it is expired during its staged reload,
// before any reader sees it, in both the CPU read and the gathered texel.
TEST_F(FogWorldFieldDecayTest, EvictedCellExpiresOnReload) {
    WorldField field = decaying();
    persist(field);
    const IRMath::ivec2 far{5000, 5000};
    const IRMath::ivec2 nearCell{0, 0};
    field.setCell(far, kFogStateExplored);
    field.setCell(nearCell, kFogStateExplored);
    field.stats();
    EXPECT_EQ(field.evict({-4, -4}, {3, 3}), 0);
    EXPECT_EQ(field.evict({-4, -4}, {3, 3}), 1);
    EXPECT_FALSE(field.peekCell(far).has_value());

    EXPECT_TRUE(field.setExploredTimeMs(kDuration));
    EXPECT_EQ(field.stats().expired_, 1) << "only the resident cell expired on the tick";
    EXPECT_EQ(field.peekCell(nearCell), std::optional<std::uint8_t>{kFogStateUnexplored});

    std::vector<FogWindowTexel> image;
    const IRMath::ivec2 farOrigin = fieldChunkOf(far) * kFieldChunkEdge - kWindowEdge / 2;
    expandWholeWindow(field, farOrigin, kWindowEdge, image);
    EXPECT_EQ(texelOf(image, far, kWindowEdge).state_, kFogStateUnexplored)
        << "the reload expired it before the gather read it";
    EXPECT_EQ(field.getCell(far), kFogStateUnexplored);
    const IRPrefab::Fog::WorldFieldStats stats = field.stats();
    EXPECT_EQ(stats.loads_, 1);
    EXPECT_EQ(stats.expired_, 1);
}

// A stationary camera over a decaying field uploads nothing until the
// deadline, then exactly the expired chunk.
TEST_F(FogWorldFieldDecayTest, StationaryExpiryUploadsOnlyAtTheDeadline) {
    WorldField field = decaying();
    IRPrefab::Fog::detail::WindowGatherScratch scratch;
    std::optional<IRMath::ivec2> windowOrigin;
    int uploads = 0;
    std::vector<FogWindowTexel> lastUpload;
    const auto upload = [&](const WindowUploadRect &, std::span<const FogWindowTexel> texels) {
        ++uploads;
        lastUpload.assign(texels.begin(), texels.end());
    };
    const IRMath::ivec2 cell{40, 40};
    field.setCell(cell, kFogStateExplored);
    IRPrefab::Fog::detail::gatherWindow(
        field,
        windowOrigin,
        kWindowOrigin,
        kWindowEdge,
        scratch,
        upload
    );
    EXPECT_EQ(uploads, kWindowEdge / kFieldChunkEdge);

    uploads = 0;
    EXPECT_TRUE(field.setExploredTimeMs(kDuration - 1));
    IRPrefab::Fog::detail::gatherWindow(
        field,
        windowOrigin,
        kWindowOrigin,
        kWindowEdge,
        scratch,
        upload
    );
    EXPECT_EQ(uploads, 0) << "no change before the deadline";

    EXPECT_TRUE(field.setExploredTimeMs(kDuration));
    IRPrefab::Fog::detail::gatherWindow(
        field,
        windowOrigin,
        kWindowOrigin,
        kWindowEdge,
        scratch,
        upload
    );
    EXPECT_EQ(uploads, 1) << "the expired chunk is re-expanded";
    ASSERT_EQ(lastUpload.size(), static_cast<std::size_t>(kFieldChunkCells));
    const IRMath::ivec2 local = IRPrefab::Spatial::fieldChunkLocal(cell);
    EXPECT_EQ(
        lastUpload[static_cast<std::size_t>(local.y * kFieldChunkEdge + local.x)],
        (FogWindowTexel{kFogStateUnexplored, kFogChannelDefault})
    );
}

// Masks, exploration times and metadata-only cells survive a flush and a
// fresh field under the same policy and restored clock; the decay deadline is
// the original one.
TEST_F(FogWorldFieldDecayTest, SaveRoundTripsMasksAndExplorationTimes) {
    const IRMath::ivec2 explored{700, -900};
    const IRMath::ivec2 metadataOnly{701, -900};
    const IRMath::ivec2 masked{702, -900};
    {
        WorldField field = decaying();
        persist(field);
        EXPECT_TRUE(field.setExploredTimeMs(300));
        field.setCell(explored, kFogStateExplored);
        field.setCellChannels(metadataOnly, kHighChannel);
        field.setCellChannels(masked, kOtherChannel);
        field.setCell(masked, kFogStateExplored);
        EXPECT_EQ(field.flush(), 1);
    }
    {
        WorldField fresh = decaying();
        EXPECT_TRUE(fresh.setExploredTimeMs(300));
        persist(fresh);
        EXPECT_EQ(fresh.getCell(explored), kFogStateExplored);
        EXPECT_EQ(fresh.getCellChannels(metadataOnly), kHighChannel);
        EXPECT_EQ(fresh.getCell(metadataOnly), kFogStateUnexplored);
        EXPECT_EQ(fresh.getCellChannels(masked), kOtherChannel);
        EXPECT_EQ(fresh.getCell(masked), kFogStateExplored);
        EXPECT_TRUE(fresh.setExploredTimeMs(300 + kDuration - 1));
        EXPECT_EQ(fresh.getCell(explored), kFogStateExplored);
        EXPECT_TRUE(fresh.setExploredTimeMs(300 + kDuration));
        EXPECT_EQ(fresh.getCell(explored), kFogStateUnexplored);
        EXPECT_EQ(fresh.getCell(masked), kFogStateExplored) << "disjoint from the policy mask";
        EXPECT_EQ(fresh.flush(), 1) << "the expiry dirtied the region";
    }
    // A later reopen at the deadline finds the expiry already on disk and a
    // metadata-only cell still present.
    WorldField later = decaying();
    EXPECT_TRUE(later.setExploredTimeMs(300 + kDuration));
    persist(later);
    EXPECT_EQ(later.getCell(explored), kFogStateUnexplored);
    EXPECT_EQ(later.getCellChannels(metadataOnly), kHighChannel);
    EXPECT_EQ(later.stats().expired_, 0);

    // Eviction saves and reloads metadata-only chunks too.
    WorldField evicting;
    persist(evicting, "evict");
    const IRMath::ivec2 lone{9000, 9000};
    evicting.setCellChannels(lone, kOtherChannel);
    evicting.stats();
    evicting.evict({-4, -4}, {3, 3});
    EXPECT_EQ(evicting.evict({-4, -4}, {3, 3}), 1);
    EXPECT_EQ(evicting.stats().saves_, 1);
    EXPECT_EQ(evicting.getCellChannels(lone), kOtherChannel);
    EXPECT_EQ(evicting.getCell(lone), kFogStateUnexplored);
}

// A pre-metadata save imports under DECAY with the restored clock as the
// first exploration time and is rewritten so a later reload does not renew it.
TEST_F(FogWorldFieldDecayTest, LegacySaveImportsWithTheRestoredClock) {
    const IRMath::ivec2 cell{-40, 70};
    const IRMath::ivec2 region = WorldField::regionOfCell(cell);
    const FieldChunkDiskPersistence legacy = *FieldChunkDiskPersistence::create(
        m_root.path().string(),
        IRPrefab::Fog::kFogFieldLayer,
        1
    );
    ASSERT_EQ(legacy.version(), IRWorld::kFieldRegionVersionBase);
    {
        IRWorld::FieldRegion data;
        const int bit = FieldChunkDiskPersistence::regionLocalBit(fieldChunkOf(cell));
        data.setChunk(bit);
        data.cells_.assign(kFieldChunkCells, kFogStateUnexplored);
        const IRMath::ivec2 local = IRPrefab::Spatial::fieldChunkLocal(cell);
        data.cells_[static_cast<std::size_t>(local.y * kFieldChunkEdge + local.x)] =
            kFogStateExplored;
        ASSERT_TRUE(legacy.saveRegion(region, data));
    }
    {
        WorldField field = decaying();
        EXPECT_TRUE(field.setExploredTimeMs(500));
        persist(field);
        EXPECT_EQ(field.getCell(cell), kFogStateExplored);
        EXPECT_TRUE(field.setExploredTimeMs(500 + kDuration - 1));
        EXPECT_EQ(field.getCell(cell), kFogStateExplored);
        EXPECT_EQ(field.flush(), 1) << "the migrated region is saved as v2";
        const std::optional<IRWorld::FieldRegion> rewritten = m_root.store().loadRegion(region);
        ASSERT_TRUE(rewritten.has_value());
        EXPECT_EQ(rewritten->version_, IRWorld::kFieldRegionVersionAuxiliary);
        ASSERT_EQ(rewritten->aux_.size(), 2u);
        EXPECT_EQ(rewritten->aux_[1].chunkCount(), 1) << "the import recorded the restored clock";
    }
    WorldField reopened = decaying();
    EXPECT_TRUE(reopened.setExploredTimeMs(500 + kDuration - 1));
    persist(reopened);
    EXPECT_EQ(reopened.getCell(cell), kFogStateExplored) << "not renewed at the reopen clock";
    EXPECT_TRUE(reopened.setExploredTimeMs(500 + kDuration));
    EXPECT_EQ(reopened.getCell(cell), kFogStateUnexplored);

    // The same legacy save under PERSISTENT keeps the memory forever.
    const IRMath::ivec2 otherCell{-40, 1070};
    {
        IRWorld::FieldRegion data;
        data.setChunk(FieldChunkDiskPersistence::regionLocalBit(fieldChunkOf(otherCell)));
        data.cells_.assign(kFieldChunkCells, kFogStateExplored);
        ASSERT_TRUE(legacy.saveRegion(WorldField::regionOfCell(otherCell), data));
    }
    WorldField persistent;
    EXPECT_TRUE(persistent.setExploredTimeMs(10 * kDuration));
    persist(persistent);
    EXPECT_EQ(persistent.getCell(otherCell), kFogStateExplored);
}

// A recorded exploration time beyond the restored clock is a recoverable
// diagnostic: the region reads empty, with nothing of it installed.
TEST_F(FogWorldFieldDecayTest, FutureTimestampReadsTheRegionAsEmpty) {
    const IRMath::ivec2 cell{33, 33};
    {
        WorldField field = decaying();
        persist(field);
        EXPECT_TRUE(field.setExploredTimeMs(5000));
        field.setCellChannels(cell, kOtherChannel);
        field.setCell(cell, kFogStateExplored);
        EXPECT_EQ(field.flush(), 1);
    }
    WorldField earlier = decaying();
    EXPECT_TRUE(earlier.setExploredTimeMs(100));
    persist(earlier);
    EngineLogCapture log;
    EXPECT_EQ(earlier.getCell(cell), kFogStateUnexplored);
    EXPECT_EQ(earlier.getCellChannels(cell), kFogChannelDefault) << "no partial install";
    EXPECT_NE(log.text().find("beyond the restored clock"), std::string::npos);
    const IRPrefab::Fog::WorldFieldStats stats = earlier.stats();
    EXPECT_EQ(stats.probes_, 1);
    EXPECT_EQ(stats.loads_, 0);
    EXPECT_EQ(stats.residentChunks_, 0);

    // Under PERSISTENT the ages are not consulted, so the same save loads.
    WorldField persistent;
    persist(persistent);
    EXPECT_EQ(persistent.getCell(cell), kFogStateExplored);
    EXPECT_EQ(persistent.getCellChannels(cell), kOtherChannel);
}

TEST_F(FogWorldFieldDecayTest, PersistentPolicyWritesNoAgePayloadAndDefaultMasksNoMaskPayload) {
    const IRMath::ivec2 cell{64, 64};
    const IRMath::ivec2 region = WorldField::regionOfCell(cell);
    {
        WorldField field;
        persist(field);
        EXPECT_TRUE(field.setExploredTimeMs(900));
        field.setCell(cell, kFogStateExplored);
        EXPECT_EQ(field.flush(), 1);
        const std::optional<IRWorld::FieldRegion> saved = m_root.store().loadRegion(region);
        ASSERT_TRUE(saved.has_value());
        ASSERT_EQ(saved->aux_.size(), 2u);
        EXPECT_EQ(saved->aux_[0].chunkCount(), 0) << "default masks need no FMAS";
        EXPECT_EQ(saved->aux_[1].chunkCount(), 0) << "a persistent field needs no FAGE";
        EXPECT_EQ(saved->version_, IRWorld::kFieldRegionVersionAuxiliary);
    }
    {
        WorldField field = decaying();
        persist(field, "decay");
        EXPECT_TRUE(field.setExploredTimeMs(900));
        field.setCell(cell, kFogStateExplored);
        field.setCellChannels(cell + IRMath::ivec2{1, 0}, kOtherChannel);
        EXPECT_EQ(field.flush(), 1);
        const std::optional<IRWorld::FieldRegion> saved = m_root.store("decay").loadRegion(region);
        ASSERT_TRUE(saved.has_value());
        ASSERT_EQ(saved->aux_.size(), 2u);
        EXPECT_EQ(saved->aux_[0].chunkCount(), 1);
        EXPECT_EQ(saved->aux_[1].chunkCount(), 1);
        EXPECT_EQ(saved->aux_[1].cells_.size(), kFieldChunkCells * 8u);
    }
}

// Expiry honours the clock through a window crossing and a teleport: cells the
// window left behind expire when their regions reload, cells it kept expire
// on the tick, and a return finds both expired.
TEST_F(FogWorldFieldDecayTest, CrossingsAndTeleportsExpireConsistently) {
    constexpr int kEdge = 1152;
    WorldField field = decaying();
    persist(field);
    IRPrefab::Fog::detail::WindowGatherScratch scratch;
    std::optional<IRMath::ivec2> windowOrigin;
    int uploads = 0;
    const auto upload = [&](const WindowUploadRect &, std::span<const FogWindowTexel>) {
        ++uploads;
    };

    IRMath::ivec2 origin{0, 0};
    const IRMath::ivec2 kept{kEdge / 2, kEdge / 2};
    const IRMath::ivec2 leftBehind{8, kEdge / 2};
    const IRMath::ivec2 farAway{kEdge * 4, 0};
    field.setCell(kept, kFogStateExplored);
    field.setCell(leftBehind, kFogStateExplored);
    field.setCell(farAway, kFogStateExplored);
    IRPrefab::Fog::detail::gatherWindow(field, windowOrigin, origin, kEdge, scratch, upload);
    field.stats();

    // Smooth +X crossings until leftBehind's region is outside the keep
    // rectangle; a negative strip brings the window back one chunk.
    for (int step = 0; step < 24; ++step) {
        origin.x += kFieldChunkEdge;
        IRPrefab::Fog::detail::gatherWindow(field, windowOrigin, origin, kEdge, scratch, upload);
    }
    origin.x -= kFieldChunkEdge;
    IRPrefab::Fog::detail::gatherWindow(field, windowOrigin, origin, kEdge, scratch, upload);
    EXPECT_GT(field.stats().evictions_, 0);
    EXPECT_FALSE(field.peekCell(leftBehind).has_value()) << "evicted with its region";
    EXPECT_FALSE(field.peekCell(farAway).has_value());

    EXPECT_TRUE(field.setExploredTimeMs(kDuration));
    EXPECT_EQ(field.stats().expired_, 1) << "only the kept cell expired on the tick";
    EXPECT_EQ(field.peekCell(kept), std::optional<std::uint8_t>{kFogStateUnexplored});

    // Teleport to the far cell and back: both reloads expire during load.
    origin = fieldChunkOf(farAway) * kFieldChunkEdge - kEdge / 2;
    uploads = 0;
    IRPrefab::Fog::detail::gatherWindow(field, windowOrigin, origin, kEdge, scratch, upload);
    EXPECT_EQ(uploads, kEdge / kFieldChunkEdge) << "a teleport re-expands the whole window";
    EXPECT_EQ(field.peekCell(farAway), std::optional<std::uint8_t>{kFogStateUnexplored});
    origin = IRMath::ivec2{0, 0};
    IRPrefab::Fog::detail::gatherWindow(field, windowOrigin, origin, kEdge, scratch, upload);
    EXPECT_EQ(field.peekCell(leftBehind), std::optional<std::uint8_t>{kFogStateUnexplored});
    EXPECT_EQ(field.peekCell(kept), std::optional<std::uint8_t>{kFogStateUnexplored});
    EXPECT_EQ(field.stats().expired_, 2);
}

#ifndef IR_RELEASE
// Expiry is serial field work: a clock advance from a worker thread asserts
// before any cell changes.
TEST_F(FogWorldFieldDecayTest, ClockAdvanceOffTheMainThreadAssertsBeforeExpiring) {
    IRJob::JobManager jobs{1};
    WorldField field = decaying();
    field.setCell({2, 2}, kFogStateExplored);
    field.stats();

    bool threw = false;
    std::thread worker([&] {
        try {
            field.setExploredTimeMs(kDuration);
        } catch (const std::runtime_error &) {
            threw = true;
        }
    });
    worker.join();

    EXPECT_TRUE(threw);
    EXPECT_EQ(field.exploredTimeMs(), 0u) << "the assert fires before the clock moves";
    EXPECT_EQ(field.peekCell({2, 2}), std::optional<std::uint8_t>{kFogStateExplored});
    EXPECT_EQ(field.stats().expired_, 0);
    EXPECT_TRUE(field.setExploredTimeMs(kDuration));
    EXPECT_EQ(field.stats().expired_, 1) << "the same advance on the main thread expires";
}
#endif

// The phase-0 cost gate for the widened texel and the conditional metadata:
// dense whole-window gathers over default/persistent and over
// DECAY + non-default-mask fields at the demo and cap edges. Counts are the
// oracle; bytes, memory and elapsed time are printed for the budget check.
TEST_F(FogWorldFieldDecayTest, ColdWholeWindowGatherWithMetadata) {
    struct Row {
        int edge_;
        bool metadata_;
        int regions_;
    };
    constexpr std::array rows{
        Row{1152, false, 16},
        Row{4096, false, 81},
        Row{1152, true, 16},
        Row{4096, true, 81},
    };
    const IRMath::ivec2 originChunk{
        2 * IRWorld::kFieldRegionEdgeChunks + 15,
        -IRWorld::kFieldRegionEdgeChunks + 15
    };
    const IRMath::ivec2 origin = originChunk * kFieldChunkEdge;
    constexpr std::uint64_t kSaveClock = 5000;

    for (const Row &row : rows) {
        const std::string subdirectory =
            std::to_string(row.edge_) + (row.metadata_ ? "-decay-masked" : "-default");
        const int edgeChunks = row.edge_ / kFieldChunkEdge;
        const auto configure = [&](WorldField &field) {
            if (row.metadata_) {
                ASSERT_TRUE(field.setExploredPolicy(ExploredPolicy::DECAY, 60000, 0b11u));
            }
            ASSERT_TRUE(field.setExploredTimeMs(kSaveClock));
        };
        {
            WorldField writer;
            configure(writer);
            persist(writer, subdirectory);
            for (int cy = 0; cy < edgeChunks; ++cy) {
                for (int cx = 0; cx < edgeChunks; ++cx) {
                    const IRMath::ivec2 chunk = originChunk + IRMath::ivec2{cx, cy};
                    for (int y = 0; y < kFieldChunkEdge; ++y) {
                        const IRMath::ivec2 first = chunk * kFieldChunkEdge + IRMath::ivec2{0, y};
                        writer.fillRow(first, kFieldChunkEdge, kFogStateExplored);
                        if (row.metadata_) {
                            for (int x = 0; x < kFieldChunkEdge; x += 2) {
                                writer.setCellChannels(first + IRMath::ivec2{x, 0}, kOtherChannel);
                            }
                        }
                    }
                }
            }
            ASSERT_EQ(writer.flush(), row.regions_);
        }

        WorldField reader;
        configure(reader);
        persist(reader, subdirectory);
        WindowGatherPlan plan;
        std::vector<FogWindowTexel> strip(static_cast<std::size_t>(row.edge_) * kFieldChunkEdge);
        std::size_t allocations = 0;
        const auto start = std::chrono::steady_clock::now();
        {
            const IRTest::AllocationCounter counter;
            planWindowGather(std::nullopt, origin, row.edge_, {}, plan);
            for (const WindowUploadRect &rect : plan.rects_) {
                expandWindowChunks(reader, origin, row.edge_, rect, strip);
            }
            allocations = counter.allocations();
        }
        const auto elapsed = std::chrono::steady_clock::now() - start;
        const IRPrefab::Fog::WorldFieldStats stats = reader.stats();
        const std::size_t texelBytes = static_cast<std::size_t>(row.edge_) *
                                       static_cast<std::size_t>(row.edge_) * sizeof(FogWindowTexel);
        const std::size_t metadataBytes =
            reader.metadataChunkCount() * kFieldChunkCells * (row.metadata_ ? 8u : 0u);
        std::printf(
            "FOG-COLD-GATHER-METADATA edge=%d fill=%s regions=%d probes=%d loads=%d chunks=%d "
            "metadataChunks=%zu windowBytes=%zu metadataBytes~%zu allocations=%zu ms=%.2f\n",
            row.edge_,
            row.metadata_ ? "decay-masked" : "default",
            row.regions_,
            stats.probes_,
            stats.loads_,
            stats.residentChunks_,
            reader.metadataChunkCount(),
            texelBytes,
            metadataBytes,
            allocations,
            std::chrono::duration<double, std::milli>(elapsed).count()
        );
        EXPECT_EQ(stats.probes_, row.regions_) << subdirectory;
        EXPECT_EQ(stats.loads_, row.regions_) << subdirectory;
        EXPECT_EQ(stats.residentChunks_, edgeChunks * edgeChunks) << subdirectory;
        EXPECT_EQ(stats.expired_, 0) << subdirectory;
        const FogWindowTexel sample = strip[0];
        EXPECT_EQ(sample.state_, kFogStateExplored) << subdirectory;
        if (row.metadata_) {
            EXPECT_EQ(reader.metadataChunkCount(), 2u * edgeChunks * edgeChunks) << subdirectory;
        } else {
            EXPECT_EQ(reader.metadataChunkCount(), 0u) << subdirectory;
        }

        // The deadline passing off-camera: the next tick expires every
        // eligible resident cell; a persistent field expires none.
        const auto tickStart = std::chrono::steady_clock::now();
        ASSERT_TRUE(reader.setExploredTimeMs(kSaveClock + 60000));
        const auto tickElapsed = std::chrono::steady_clock::now() - tickStart;
        const int expired = reader.stats().expired_;
        std::printf(
            "FOG-DECAY-TICK edge=%d fill=%s expired=%d ms=%.2f\n",
            row.edge_,
            row.metadata_ ? "decay-masked" : "default",
            expired,
            std::chrono::duration<double, std::milli>(tickElapsed).count()
        );
        EXPECT_EQ(expired, row.metadata_ ? edgeChunks * edgeChunks * kFieldChunkCells : 0)
            << subdirectory;
    }
}

// A moving set past the cap carrying non-default channels over a decaying
// field with an advancing clock, cleared, re-stamped and drained every frame,
// allocates nothing once warm.
TEST_F(FogWorldFieldDecayTest, MovingChannelledFieldTierAllocatesNothingOnceWarm) {
    constexpr int kSources = IRComponents::kMaxFogVisionCircles + 64;
    constexpr int kPeriod = 16;
    constexpr float kRadius = 24.0f;
    WorldField field;
    ASSERT_TRUE(field.setExploredPolicy(ExploredPolicy::DECAY, 60000, 0b11u));
    FrameDataFogObservers observers;
    std::vector<FieldChunkKey> pending;
    for (int x = -100; x < 1300; x += 3) {
        field.setCellChannels({x, 0}, kOtherChannel);
    }
    field.exploreRadius({600, 0}, 60, 0b11u);
    std::uint64_t clock = 0;
    const auto centreOf = [](int source, int index) {
        const float step = static_cast<float>(index % kPeriod) * 9.5f;
        return IRMath::vec2{
            static_cast<float>((source % 8) * 150) + step,
            static_cast<float>((source / 8) * 150) - step
        };
    };
    const auto frame = [&](int index) {
        C_CanvasFogOfWar::clearVisionCircles(observers, field);
        for (int i = 0; i < kSources; ++i) {
            const IRMath::vec2 centre = centreOf(i, index);
            C_CanvasFogOfWar::addVisionCircle(
                observers,
                field,
                centre.x,
                centre.y,
                kRadius,
                0.0f,
                0.0f,
                0.0f,
                IRComponents::kFogVisionZCostMirrorUp,
                0.0f,
                i % 2 == 0 ? kFogChannelDefault : kOtherChannel
            );
        }
        clock += 16;
        ASSERT_TRUE(field.setExploredTimeMs(clock));
        field.consumePending(pending);
    };
    for (int index = 0; index < 2 * kPeriod; ++index) {
        frame(index);
        if (index == kPeriod - 1) {
            pending.shrink_to_fit();
        }
    }
    const IRTest::AllocationCounter counter;
    for (int index = 2 * kPeriod; index < 4 * kPeriod; ++index) {
        frame(index);
    }
    EXPECT_EQ(counter.allocations(), 0u);
    EXPECT_EQ(field.getCell({600, 0}), kFogStateExplored) << "within the duration";
}

} // namespace
