// The zoom -> LOD tier mapping a camera selects with: the default thresholds,
// custom profiles with and without a hysteresis deadband, validation, the main
// camera's history in LOD_UPDATE, and the saved form. Secondary-viewport
// selection is covered beside the rest of the viewport sync in viewport_test.

#include <gtest/gtest.h>

#include <irreden/asset/binary_io.hpp>
#include <irreden/ir_entity.hpp>
#include <irreden/ir_math.hpp>
#include <irreden/ir_system.hpp>
#include <irreden/render/components/component_active_lod_level.hpp>
#include <irreden/render/components/component_lod_zoom_profile.hpp>
#include <irreden/render/lod_utils.hpp>
#include <irreden/render/lod_zoom_policy.hpp>
#include <irreden/render/lod_zoom_profile.hpp>
#include <irreden/render/save_serializers_render.hpp>
#include <irreden/render/systems/system_lod_update.hpp>
#include <irreden/world/save_component_inventory.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <optional>
#include <utility>
#include <vector>

namespace {

using IRComponents::C_LodZoomProfile;
using IRMath::vec2;
using IRRender::LodLevel;
using IRRender::LodTierHistory;
using IRRender::LodZoomProfile;
using Thresholds = LodZoomProfile::Thresholds;

constexpr float kInf = std::numeric_limits<float>::infinity();
constexpr float kNaN = std::numeric_limits<float>::quiet_NaN();

float below(float value) {
    return std::nextafter(value, 0.0f);
}

float above(float value) {
    return std::nextafter(value, kInf);
}

LodZoomProfile profileOf(const Thresholds &thresholds, float hysteresis) {
    const std::optional<LodZoomProfile> profile = LodZoomProfile::make(thresholds, hysteresis);
    EXPECT_TRUE(profile.has_value());
    return profile.value_or(LodZoomProfile{});
}

// The tiers a history passes through over @p zooms, one advance each.
std::vector<LodLevel>
trace(LodTierHistory &history, const LodZoomProfile &profile, const std::vector<float> &zooms) {
    std::vector<LodLevel> tiers;
    for (const float zoom : zooms) {
        tiers.push_back(history.advance(profile, zoom));
    }
    return tiers;
}

int changes(const std::vector<LodLevel> &tiers) {
    int count = 0;
    for (std::size_t i = 1; i < tiers.size(); ++i) {
        count += tiers[i] != tiers[i - 1] ? 1 : 0;
    }
    return count;
}

constexpr std::array<LodLevel, 5> kAllTiers{
    LodLevel::LOD_0,
    LodLevel::LOD_1,
    LodLevel::LOD_2,
    LodLevel::LOD_3,
    LodLevel::LOD_4,
};

// ---------------------------------------------------------------------------
// The default profile
// ---------------------------------------------------------------------------

TEST(LodZoomProfile, DefaultKeepsTheFixedBoundariesOnBothSides) {
    const LodZoomProfile profile{};
    EXPECT_EQ(profile.thresholds(), (Thresholds{2.0f, 4.0f, 8.0f, 16.0f}));
    EXPECT_EQ(profile.hysteresisOctaves(), 0.0f);

    struct Boundary {
        float zoom_;
        LodLevel coarser_;
        LodLevel finer_;
    };
    const Boundary boundaries[] = {
        {2.0f, LodLevel::LOD_4, LodLevel::LOD_3},
        {4.0f, LodLevel::LOD_3, LodLevel::LOD_2},
        {8.0f, LodLevel::LOD_2, LodLevel::LOD_1},
        {16.0f, LodLevel::LOD_1, LodLevel::LOD_0},
    };
    for (const Boundary &boundary : boundaries) {
        EXPECT_EQ(profile.tierAt(below(boundary.zoom_)), boundary.coarser_) << boundary.zoom_;
        EXPECT_EQ(profile.tierAt(boundary.zoom_), boundary.finer_) << boundary.zoom_;
        EXPECT_EQ(profile.tierAt(above(boundary.zoom_)), boundary.finer_) << boundary.zoom_;
        EXPECT_EQ(IRRender::computeLodLevel(below(boundary.zoom_)), boundary.coarser_);
        EXPECT_EQ(IRRender::computeLodLevel(boundary.zoom_), boundary.finer_);
        // Width zero: the history never matters.
        for (const LodLevel previous : kAllTiers) {
            EXPECT_EQ(profile.advance(previous, below(boundary.zoom_)), boundary.coarser_);
            EXPECT_EQ(profile.advance(previous, boundary.zoom_), boundary.finer_);
            EXPECT_EQ(profile.advance(previous, above(boundary.zoom_)), boundary.finer_);
        }
    }
    EXPECT_EQ(profile.tierAt(1.0f), LodLevel::LOD_4);
    EXPECT_EQ(profile.tierAt(64.0f), LodLevel::LOD_0);
}

TEST(LodZoomProfile, ZeroWidthAnswersTheSameFromEitherDirection) {
    const LodZoomProfile profile = profileOf({3.0f, 5.0f, 9.0f, 20.0f}, 0.0f);
    std::vector<float> rising;
    for (float zoom = 1.0f; zoom <= 32.0f; zoom += 0.125f) {
        rising.push_back(zoom);
    }
    for (const float threshold : profile.thresholds()) {
        rising.push_back(below(threshold));
        rising.push_back(threshold);
    }
    std::sort(rising.begin(), rising.end());
    const std::vector<float> falling(rising.rbegin(), rising.rend());

    for (const std::vector<float> *zooms : {&std::as_const(rising), &falling}) {
        LodTierHistory history;
        const std::vector<LodLevel> tiers = trace(history, profile, *zooms);
        for (std::size_t i = 0; i < zooms->size(); ++i) {
            EXPECT_EQ(tiers[i], profile.tierAt((*zooms)[i])) << (*zooms)[i];
        }
    }
    EXPECT_EQ(profile.tierAt(3.0f), LodLevel::LOD_3);
    EXPECT_EQ(profile.tierAt(below(3.0f)), LodLevel::LOD_4);
    EXPECT_EQ(profile.tierAt(20.0f), LodLevel::LOD_0);
}

// ---------------------------------------------------------------------------
// Hysteresis
// ---------------------------------------------------------------------------

// Thresholds 3, 6, 12, 24 with a quarter-octave half-width: the finer side of
// each boundary is entered at t * 2^0.25 and left below t / 2^0.25.
class LodZoomHysteresis : public testing::Test {
  protected:
    const LodZoomProfile m_profile = profileOf({3.0f, 6.0f, 12.0f, 24.0f}, 0.25f);
    const float m_factor = IRMath::pow(2.0f, 0.25f);

    float enter(std::size_t i) const {
        return m_profile.thresholds()[i] * m_factor;
    }
    float leave(std::size_t i) const {
        return m_profile.thresholds()[i] / m_factor;
    }
};

TEST_F(LodZoomHysteresis, ForwardSweepCrossesEachThresholdAtItsUpperEdge) {
    LodTierHistory history;
    EXPECT_EQ(history.advance(m_profile, 1.0f), LodLevel::LOD_4);
    const LodLevel finer[] = {LodLevel::LOD_3, LodLevel::LOD_2, LodLevel::LOD_1, LodLevel::LOD_0};
    LodLevel current = LodLevel::LOD_4;
    for (std::size_t i = 0; i < 4; ++i) {
        // The threshold itself and the whole upper half of the band hold.
        EXPECT_EQ(history.advance(m_profile, m_profile.thresholds()[i]), current) << i;
        EXPECT_EQ(history.advance(m_profile, below(enter(i))), current) << i;
        EXPECT_EQ(history.advance(m_profile, enter(i)), finer[i]) << i;
        current = finer[i];
    }
    EXPECT_EQ(history.advance(m_profile, 64.0f), LodLevel::LOD_0);
}

TEST_F(LodZoomHysteresis, BackwardSweepCrossesEachThresholdBelowItsLowerEdge) {
    LodTierHistory history;
    EXPECT_EQ(history.advance(m_profile, 64.0f), LodLevel::LOD_0);
    const LodLevel coarser[] = {LodLevel::LOD_1, LodLevel::LOD_2, LodLevel::LOD_3, LodLevel::LOD_4};
    LodLevel current = LodLevel::LOD_0;
    for (std::size_t step = 0; step < 4; ++step) {
        const std::size_t i = 3 - step;
        EXPECT_EQ(history.advance(m_profile, below(m_profile.thresholds()[i])), current) << i;
        EXPECT_EQ(history.advance(m_profile, leave(i)), current) << i;
        EXPECT_EQ(history.advance(m_profile, below(leave(i))), coarser[step]) << i;
        current = coarser[step];
    }
}

TEST_F(LodZoomHysteresis, OscillationInsideTheDeadbandHoldsAndLeavingCrossesOnce) {
    // 2.9 / 3.1 straddles the 3x threshold, inside [3 / 2^0.25, 3 * 2^0.25].
    std::vector<float> oscillation;
    for (int i = 0; i < 16; ++i) {
        oscillation.push_back(i % 2 == 0 ? 2.9f : 3.1f);
    }

    const LodZoomProfile sharp = profileOf(m_profile.thresholds(), 0.0f);
    LodTierHistory sharpHistory;
    EXPECT_EQ(changes(trace(sharpHistory, sharp, oscillation)), 15)
        << "width zero swaps at every crossing";

    for (const LodLevel start : {LodLevel::LOD_4, LodLevel::LOD_3}) {
        LodTierHistory history;
        history.advance(m_profile, start == LodLevel::LOD_4 ? 1.0f : 4.0f);
        const std::vector<LodLevel> held = trace(history, m_profile, oscillation);
        EXPECT_EQ(changes(held), 0);
        EXPECT_EQ(held.front(), start);
    }

    // Out of the band upward, then oscillate, then out downward: one swap each.
    std::vector<float> excursion = oscillation;
    excursion.push_back(enter(0));
    excursion.insert(excursion.end(), oscillation.begin(), oscillation.end());
    excursion.push_back(below(leave(0)));
    excursion.insert(excursion.end(), oscillation.begin(), oscillation.end());
    LodTierHistory history;
    history.advance(m_profile, 1.0f);
    const std::vector<LodLevel> tiers = trace(history, m_profile, excursion);
    EXPECT_EQ(changes(tiers), 2);
    EXPECT_EQ(tiers[oscillation.size()], LodLevel::LOD_3);
    EXPECT_EQ(tiers.back(), LodLevel::LOD_4);
}

TEST_F(LodZoomHysteresis, AJumpResolvesEveryCrossedThresholdInOneUpdate) {
    LodTierHistory history;
    history.advance(m_profile, 1.0f);
    EXPECT_EQ(history.advance(m_profile, 64.0f), LodLevel::LOD_0);
    EXPECT_EQ(history.advance(m_profile, 1.0f), LodLevel::LOD_4);
    // Into the middle of a band: every threshold below it is fully crossed,
    // the one it sits in is not.
    EXPECT_EQ(history.advance(m_profile, 12.0f), LodLevel::LOD_2);
    EXPECT_EQ(history.advance(m_profile, 3.0f), LodLevel::LOD_3);
    for (const LodLevel previous : kAllTiers) {
        EXPECT_EQ(m_profile.advance(previous, 100.0f), LodLevel::LOD_0);
        EXPECT_EQ(m_profile.advance(previous, 0.5f), LodLevel::LOD_4);
    }
}

TEST_F(LodZoomHysteresis, CreationResetAndReplacementSeedFromTheCurrentZoom) {
    // Inside the first band: the seed takes the finer side of the threshold,
    // whichever way the zoom will move next.
    LodTierHistory created;
    EXPECT_EQ(created.advance(m_profile, 3.2f), LodLevel::LOD_3);
    LodTierHistory below3;
    EXPECT_EQ(below3.advance(m_profile, 2.9f), LodLevel::LOD_4);

    LodTierHistory history;
    history.advance(m_profile, 1.0f);
    EXPECT_EQ(history.advance(m_profile, 3.2f), LodLevel::LOD_4) << "held by the deadband";
    history.reset();
    EXPECT_FALSE(history.seeded_);
    EXPECT_EQ(history.advance(m_profile, 3.2f), LodLevel::LOD_3) << "reset re-seeds";
    EXPECT_EQ(history.advance(m_profile, 2.9f), LodLevel::LOD_3);

    // A replacement re-seeds rather than carrying the old deadband's tier.
    const LodZoomProfile replacement = profileOf({2.0f, 2.5f, 8.0f, 16.0f}, 0.0f);
    EXPECT_EQ(history.advance(replacement, 2.9f), LodLevel::LOD_2);
    EXPECT_EQ(history.profile_, replacement);
    // And back: the original profile seeds afresh too.
    EXPECT_EQ(history.advance(m_profile, 2.9f), LodLevel::LOD_4);
}

TEST_F(LodZoomHysteresis, ANonfiniteZoomKeepsThePreviousTier) {
    LodTierHistory history;
    history.advance(m_profile, 7.0f);
    EXPECT_EQ(history.advance(m_profile, kNaN), LodLevel::LOD_2);
    EXPECT_EQ(history.advance(m_profile, kInf), LodLevel::LOD_2);
    EXPECT_EQ(LodZoomProfile{}.tierAt(kNaN), LodLevel::LOD_4);
}

// ---------------------------------------------------------------------------
// Validation
// ---------------------------------------------------------------------------

TEST(LodZoomProfile, InvalidValuesAreRejected) {
    struct Case {
        Thresholds thresholds_;
        float hysteresis_;
    };
    const Case invalid[] = {
        {{0.0f, 4.0f, 8.0f, 16.0f}, 0.0f},
        {{-2.0f, 4.0f, 8.0f, 16.0f}, 0.0f},
        {{2.0f, kNaN, 8.0f, 16.0f}, 0.0f},
        {{2.0f, 4.0f, 8.0f, kInf}, 0.0f},
        {{2.0f, 8.0f, 4.0f, 16.0f}, 0.0f},
        {{2.0f, 4.0f, 4.0f, 16.0f}, 0.0f},
        {{2.0f, 4.0f, 8.0f, 16.0f}, -0.1f},
        {{2.0f, 4.0f, 8.0f, 16.0f}, kNaN},
        {{2.0f, 4.0f, 8.0f, 16.0f}, kInf},
        // Adjacent thresholds one octave apart leave each band half an octave.
        {{2.0f, 4.0f, 8.0f, 16.0f}, 0.51f},
        {{2.0f, 2.5f, 8.0f, 16.0f}, 0.2f},
    };
    for (const Case &c : invalid) {
        EXPECT_TRUE(LodZoomProfile::whyInvalid(c.thresholds_, c.hysteresis_).has_value())
            << c.thresholds_[0] << " " << c.thresholds_[1] << " h=" << c.hysteresis_;
        EXPECT_FALSE(LodZoomProfile::make(c.thresholds_, c.hysteresis_).has_value());
    }
    // Touching bands are allowed: each band's top edge is the next one's floor.
    EXPECT_TRUE(LodZoomProfile::make({1.0f, 4.0f, 16.0f, 64.0f}, 1.0f).has_value());
    EXPECT_FALSE(LodZoomProfile::make({1.0f, 4.0f, 16.0f, 64.0f}, 1.01f).has_value());
    EXPECT_TRUE(LodZoomProfile::make({1.5f, 3.0f, 10.0f, 40.0f}, 0.0f).has_value());
}

TEST(LodZoomProfile, AnInvalidSetKeepsThePreviousProfile) {
    C_LodZoomProfile component{profileOf({3.0f, 6.0f, 12.0f, 24.0f}, 0.25f)};
    const LodZoomProfile before = component.profile_;
    EXPECT_FALSE(component.set({3.0f, 6.0f, 5.0f, 24.0f}, 0.25f));
    EXPECT_FALSE(component.set({3.0f, 6.0f, 12.0f, 24.0f}, 0.75f));
    EXPECT_EQ(component.profile_, before);
    EXPECT_TRUE(component.set({2.0f, 4.0f, 8.0f, 16.0f}, 0.0f));
    EXPECT_EQ(component.profile_, LodZoomProfile{});
}

// ---------------------------------------------------------------------------
// The main camera (LOD_UPDATE)
// ---------------------------------------------------------------------------

class LodZoomMainCamera : public testing::Test {
  protected:
    IREntity::EntityManager m_entityManager;
    IRSystem::SystemManager m_systemManager;
    IREntity::EntityId m_camera = IREntity::kNullEntity;

    void makeCamera() {
        m_camera = IREntity::createEntity();
        IREntity::setName(m_camera, "camera");
    }

    static LodLevel write(float zoom) {
        IRSystem::System<IRSystem::LOD_UPDATE>::writeActiveTier(vec2(zoom));
        return IRRender::getActiveLodLevel();
    }
};

TEST_F(LodZoomMainCamera, NoCameraOrNoProfileSelectsWithTheDefault) {
    EXPECT_EQ(IRPrefab::Lod::mainZoomProfile(), LodZoomProfile{});
    EXPECT_EQ(write(below(4.0f)), LodLevel::LOD_3);
    EXPECT_EQ(write(4.0f), LodLevel::LOD_2);
    makeCamera();
    EXPECT_EQ(IRPrefab::Lod::mainZoomProfile(), LodZoomProfile{});
    EXPECT_EQ(write(below(4.0f)), LodLevel::LOD_3);
    EXPECT_FALSE(IRPrefab::Lod::setMainZoomProfile({2.0f, 4.0f, 8.0f, 4.0f}));
    EXPECT_FALSE(IREntity::getComponentOptional<C_LodZoomProfile>(m_camera).has_value());
}

TEST_F(LodZoomMainCamera, TheCameraProfileHoldsTheTierInsideItsDeadband) {
    makeCamera();
    ASSERT_TRUE(IRPrefab::Lod::setMainZoomProfile({3.0f, 6.0f, 12.0f, 24.0f}, 0.25f));
    EXPECT_EQ(write(1.0f), LodLevel::LOD_4);
    for (int i = 0; i < 8; ++i) {
        EXPECT_EQ(write(i % 2 == 0 ? 3.1f : 2.9f), LodLevel::LOD_4) << i;
    }
    EXPECT_EQ(write(3.6f), LodLevel::LOD_3);
    for (int i = 0; i < 8; ++i) {
        EXPECT_EQ(write(i % 2 == 0 ? 2.9f : 3.1f), LodLevel::LOD_3) << i;
    }

    // An invalid write leaves the profile and its history in place.
    const LodZoomProfile before = IRPrefab::Lod::mainZoomProfile();
    EXPECT_FALSE(IRPrefab::Lod::setMainZoomProfile({3.0f, 6.0f, 12.0f, 24.0f}, 2.0f));
    EXPECT_EQ(IRPrefab::Lod::mainZoomProfile(), before);
    EXPECT_EQ(write(2.9f), LodLevel::LOD_3);

    // Reset: the next write seeds from the zoom it sees.
    IRPrefab::Lod::resetMainZoomHistory();
    EXPECT_EQ(write(2.9f), LodLevel::LOD_4);

    // Removal returns to the default and re-seeds under it.
    write(3.6f);
    IRPrefab::Lod::clearMainZoomProfile();
    EXPECT_FALSE(IREntity::getComponentOptional<C_LodZoomProfile>(m_camera).has_value());
    EXPECT_EQ(write(3.6f), LodLevel::LOD_3);
    EXPECT_EQ(write(below(2.0f)), LodLevel::LOD_4);
}

TEST_F(LodZoomMainCamera, ReplacingTheProfileReseedsTheTier) {
    makeCamera();
    ASSERT_TRUE(IRPrefab::Lod::setMainZoomProfile({3.0f, 6.0f, 12.0f, 24.0f}, 0.25f));
    write(1.0f);
    EXPECT_EQ(write(3.2f), LodLevel::LOD_4);
    // An in-place replacement through the setter...
    ASSERT_TRUE(IRPrefab::Lod::setMainZoomProfile({3.0f, 6.0f, 12.0f, 24.0f}, 0.125f));
    EXPECT_EQ(write(3.2f), LodLevel::LOD_3);
    // ...and a whole-component replacement re-seed alike.
    IREntity::setComponent(m_camera, C_LodZoomProfile{profileOf({4.0f, 8.0f, 16.0f, 32.0f}, 0.0f)});
    EXPECT_EQ(write(3.2f), LodLevel::LOD_4);
    EXPECT_EQ(write(32.0f), LodLevel::LOD_0);
}

TEST_F(LodZoomMainCamera, TheHistoryIsTheSingletonsAndRecordsItsProfile) {
    makeCamera();
    ASSERT_TRUE(IRPrefab::Lod::setMainZoomProfile({3.0f, 6.0f, 12.0f, 24.0f}, 0.25f));
    EXPECT_FALSE(IREntity::singleton<IRComponents::C_ActiveLodLevel>().history_.seeded_);
    write(1.0f);
    const auto &active = IREntity::singleton<IRComponents::C_ActiveLodLevel>();
    EXPECT_TRUE(active.history_.seeded_);
    EXPECT_EQ(active.history_.profile_, IRPrefab::Lod::mainZoomProfile());
    EXPECT_EQ(active.history_.tier_, active.current_);
}

// ---------------------------------------------------------------------------
// Persistence
// ---------------------------------------------------------------------------

TEST(LodZoomProfile, SavedFormRoundTripsTheAuthoredProfileOnly) {
    static_assert(IRWorld::shouldSave<C_LodZoomProfile>());
    static_assert(!IRWorld::shouldSave<IRComponents::C_ActiveLodLevel>());
    const C_LodZoomProfile authored{profileOf({3.0f, 6.0f, 12.0f, 24.0f}, 0.25f)};
    IRAsset::MemoryBinaryWriter writer;
    IRWorld::SaveSerialize<C_LodZoomProfile>::write(writer, authored);
    EXPECT_EQ(writer.buffer().size(), 5 * sizeof(float));
    IRAsset::MemoryBinaryReader reader(writer.buffer().data(), writer.buffer().size(), "lod");
    const auto loaded = IRWorld::SaveSerialize<C_LodZoomProfile>::read(reader);
    ASSERT_TRUE(loaded.ok()) << loaded.status_.message_;
    EXPECT_EQ(loaded.value_.profile_, authored.profile_);

    // A record that is not a valid profile fails the read.
    IRAsset::MemoryBinaryWriter bad;
    for (const float value : {3.0f, 2.0f, 12.0f, 24.0f, 0.0f}) {
        bad.writeF32(value);
    }
    IRAsset::MemoryBinaryReader badReader(bad.buffer().data(), bad.buffer().size(), "lod");
    EXPECT_FALSE(IRWorld::SaveSerialize<C_LodZoomProfile>::read(badReader).ok());
}

} // namespace
