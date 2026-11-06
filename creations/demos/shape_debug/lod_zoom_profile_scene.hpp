#ifndef LOD_ZOOM_PROFILE_SCENE_H
#define LOD_ZOOM_PROFILE_SCENE_H

// --lod-zoom-profile: authored LOD thresholds and hysteresis under fractional
// main-camera zoom and two secondary viewports.
//
// Every variant pair is a coarse red member on [LOD_3 .. LOD_4] and a fine
// blue member on [LOD_0 .. LOD_2], co-located, so the pair swaps at the
// LOD_3 / LOD_2 boundary. The main camera's profile puts that boundary at 5x.
// Shots 0-3 oscillate 4.9 / 5.1 at width zero (a swap every shot); the capture
// hook of shot 3 widens the profile to a quarter octave, band [4.20, 5.95],
// and the rest oscillate inside it, leave it downward, oscillate, and leave it
// upward (one swap at each exit). World pairs: GRID, DETACHED and
// DETACHED_REVOXELIZE DENSE sets, SDF shapes, and a GRID pair pinned to LOD_0.
// Two portraits follow their own power-of-two zoom sequences: one inherits the
// main profile (4x reads coarse, where the default would read fine), one owns
// a 0.45-octave profile on the default thresholds (4x holds whichever side it
// came from).
//
// Each capture asserts the resolved tiers and gate state and logs
// `LOD-PROFILE-REGION` boxes (window points) with the variant each must show;
// scripts/render-lod-zoom-profile-metric.py reads the screenshots against them.

#include <irreden/ir_entity.hpp>
#include <irreden/ir_math.hpp>
#include <irreden/ir_render.hpp>
#include <irreden/ir_video.hpp>
#include <irreden/ir_window.hpp>
#include <irreden/common/components/component_size_triangles.hpp>
#include <irreden/common/rotation_mode.hpp>
#include <irreden/render/camera.hpp>
#include <irreden/render/components/component_lod_tier_override.hpp>
#include <irreden/render/components/component_viewport_subject.hpp>
#include <irreden/render/lod_utils.hpp>
#include <irreden/render/lod_zoom_policy.hpp>
#include <irreden/render/viewport.hpp>
#include <irreden/voxel/components/component_shape_descriptor.hpp>
#include <irreden/voxel/components/component_voxel_set.hpp>

#include <array>
#include <cstddef>
#include <string_view>

namespace IRShapeDebug {

class LodZoomProfileScene {
  public:
    static constexpr std::size_t kShotCount = 15;

    static constexpr IRVideo::AutoScreenshotShot shots_[kShotCount] = {
        {4.9f, IRMath::vec2(0), 0.0f, "lod_profile_00_sharp_z4_9"},
        {5.1f, IRMath::vec2(0), 0.0f, "lod_profile_01_sharp_z5_1"},
        {4.9f, IRMath::vec2(0), 0.0f, "lod_profile_02_sharp_z4_9"},
        {5.1f, IRMath::vec2(0), 0.0f, "lod_profile_03_sharp_z5_1"},
        {6.0f, IRMath::vec2(0), 0.0f, "lod_profile_04_band_z6_0"},
        {4.9f, IRMath::vec2(0), 0.0f, "lod_profile_05_band_z4_9"},
        {5.1f, IRMath::vec2(0), 0.0f, "lod_profile_06_band_z5_1"},
        {4.9f, IRMath::vec2(0), 0.0f, "lod_profile_07_band_z4_9"},
        {4.1f, IRMath::vec2(0), 0.0f, "lod_profile_08_band_exit_down_z4_1"},
        {5.1f, IRMath::vec2(0), 0.0f, "lod_profile_09_band_z5_1"},
        {4.9f, IRMath::vec2(0), 0.0f, "lod_profile_10_band_z4_9"},
        {5.9f, IRMath::vec2(0), 0.0f, "lod_profile_11_band_z5_9"},
        {6.0f, IRMath::vec2(0), 0.0f, "lod_profile_12_band_exit_up_z6_0"},
        {4.9f, IRMath::vec2(0), 0.0f, "lod_profile_13_band_z4_9"},
        {5.1f, IRMath::vec2(0), 0.0f, "lod_profile_14_band_z5_1"},
    };

    void initialize() {
        using namespace IRComponents;
        using IRMath::vec3;
        IRPrefab::Camera::setZoomContinuous(true);
        IR_ASSERT(
            IRPrefab::Lod::setMainZoomProfile(kMainThresholds, 0.0f),
            "the main LOD profile must be valid"
        );

        for (std::size_t i = 0; i < m_world.size(); ++i) {
            const vec3 center = kWorldCenters[i];
            WorldPair &pair = m_world[i];
            pair.center_ = center;
            if (i == kSdf) {
                pair.coarse_ = IREntity::createEntity(
                    C_LocalTransform{center},
                    C_ShapeDescriptor{
                        IRRender::ShapeType::BOX,
                        IRMath::vec4(6.0f, 6.0f, 6.0f, 0.0f),
                        kCoarse
                    }
                );
                pair.fine_ = IREntity::createEntity(
                    C_LocalTransform{center},
                    C_ShapeDescriptor{
                        IRRender::ShapeType::SPHERE,
                        IRMath::vec4(3.5f, 3.5f, 3.5f, 0.0f),
                        kFine
                    }
                );
                setShapeBand(pair.coarse_, LodLevel::LOD_4, LodLevel::LOD_3);
                setShapeBand(pair.fine_, LodLevel::LOD_2, LodLevel::LOD_0);
                continue;
            }
            spawnDensePair(pair, center);
            if (i == kDetached || i == kRevoxelize) {
                const RotationMode mode =
                    i == kDetached ? RotationMode::DETACHED : RotationMode::DETACHED_REVOXELIZE;
                IRPrefab::RotationMode::setMode(pair.coarse_, mode);
                IRPrefab::RotationMode::setMode(pair.fine_, mode);
            }
            if (i == kPinned) {
                IREntity::setComponent(pair.coarse_, C_LodTierOverride{LodLevel::LOD_0});
                IREntity::setComponent(pair.fine_, C_LodTierOverride{LodLevel::LOD_0});
            }
        }

        const IRMath::ivec2 guiSize =
            IREntity::getComponent<C_SizeTriangles>(IRRender::getCanvas("gui")).size_;
        for (std::size_t i = 0; i < m_portraits.size(); ++i) {
            Portrait &portrait = m_portraits[i];
            spawnDensePair(portrait.subject_, kPortraitSubjectCenters[i]);
            IRPrefab::Viewport::Desc desc{};
            desc.rectSize_ = IRMath::ivec2(guiSize.x / 6, guiSize.y / 3);
            // Top-right and bottom-right corners, clear of the world row's band.
            desc.rectOrigin_ = IRMath::ivec2(
                guiSize.x - desc.rectSize_.x - guiSize.x / 40,
                i == kInherit ? guiSize.y / 20 : guiSize.y - desc.rectSize_.y - guiSize.y / 20
            );
            desc.zoom_ = kPortraitZooms[i][0];
            portrait.origin_ = desc.rectOrigin_;
            portrait.size_ = desc.rectSize_;
            portrait.viewport_ =
                IRPrefab::Viewport::create(desc, i == kInherit ? "lod_inherit" : "lod_own");
            IRPrefab::Viewport::setSubject(portrait.viewport_, portrait.subject_.coarse_);
            IREntity::setComponent(portrait.subject_.fine_, C_ViewportSubject{portrait.viewport_});
        }
        IR_ASSERT(
            IRPrefab::Lod::setViewportZoomProfile(
                m_portraits[kOwn].viewport_,
                IRRender::LodZoomProfile::kDefaultThresholds,
                kOwnHysteresis
            ),
            "the portrait LOD profile must be valid"
        );
        IR_LOG_INFO(
            "LOD zoom-profile scene: main thresholds 2,5,10,20; portrait '{}' inherits, '{}' "
            "owns 2,4,8,16 h={}",
            "lod_inherit",
            "lod_own",
            kOwnHysteresis
        );
    }

    void capture(int shotIndex) {
        IR_ASSERT(
            shotIndex >= 0 && shotIndex < static_cast<int>(kShotCount),
            "LOD profile capture index must belong to its shot table"
        );
        const auto shot = static_cast<std::size_t>(shotIndex);
        const bool mainFine = kMainVariants[shot] == 'f';
        const LodLevel mainTier = IRRender::getActiveLodLevel();
        IR_ASSERT(
            (mainTier <= LodLevel::LOD_2) == mainFine,
            "main tier {} contradicts the expected {} variant at shot {}",
            IRRender::toUnderlying(mainTier),
            mainFine ? "fine" : "coarse",
            shotIndex
        );
        for (std::size_t i = 0; i < m_world.size(); ++i) {
            const WorldPair &pair = m_world[i];
            const bool fine = i == kPinned ? true : mainFine;
            if (i != kSdf) {
                verifyDensePair(pair, fine, shotIndex);
            } else {
                verifyShapeRecords(pair);
            }
            logWorldRegion(shotIndex, kWorldNames[i], pair, fine);
        }
        for (std::size_t i = 0; i < m_portraits.size(); ++i) {
            const Portrait &portrait = m_portraits[i];
            const bool fine = kPortraitVariants[i][shot] == 'f';
            const LodLevel tier = IRPrefab::Lod::viewportTier(portrait.viewport_);
            IR_ASSERT(
                (tier <= LodLevel::LOD_2) == fine,
                "portrait {} tier {} contradicts the expected variant at shot {}",
                i,
                IRRender::toUnderlying(tier),
                shotIndex
            );
            IR_ASSERT(
                IRPrefab::Viewport::drawnSubject(portrait.viewport_) ==
                    (fine ? portrait.subject_.fine_ : portrait.subject_.coarse_),
                "portrait {} must draw exactly the variant of its own tier",
                i
            );
            verifyAuthoredRecords(portrait.subject_);
            logPortraitRegion(
                shotIndex,
                i == kInherit ? "portrait_inherit" : "portrait_own",
                portrait,
                fine
            );
        }
        IR_LOG_INFO(
            "LOD-PROFILE-TIER shot={} zoom={} main={} portrait_inherit={} (zoom {}) "
            "portrait_own={} (zoom {}) hysteresis={}",
            shotIndex,
            IRRender::getCameraZoom().x,
            IRRender::toUnderlying(mainTier),
            IRRender::toUnderlying(IRPrefab::Lod::viewportTier(m_portraits[kInherit].viewport_)),
            kPortraitZooms[kInherit][shot],
            IRRender::toUnderlying(IRPrefab::Lod::viewportTier(m_portraits[kOwn].viewport_)),
            kPortraitZooms[kOwn][shot],
            IRPrefab::Lod::mainZoomProfile().hysteresisOctaves()
        );

        // Changes for the next shot. The profile edit is in place, so no
        // archetype moves under the capture system's iteration.
        if (shot + 1 == kFirstBandShot) {
            IR_ASSERT(
                IRPrefab::Lod::setMainZoomProfile(kMainThresholds, kMainHysteresis),
                "the widened main LOD profile must be valid"
            );
        }
        if (shot + 1 < kShotCount) {
            for (std::size_t i = 0; i < m_portraits.size(); ++i) {
                IRPrefab::Viewport::setCamera(
                    m_portraits[i].viewport_,
                    kPortraitZooms[i][shot + 1],
                    0.0f
                );
            }
        }
    }

  private:
    using LodLevel = IRRender::LodLevel;

    struct WorldPair {
        IREntity::EntityId coarse_ = IREntity::kNullEntity;
        IREntity::EntityId fine_ = IREntity::kNullEntity;
        IRMath::vec3 center_{0.0f};
        int coarseVoxels_ = 0;
        int fineVoxels_ = 0;
    };

    struct Portrait {
        WorldPair subject_;
        IREntity::EntityId viewport_ = IREntity::kNullEntity;
        IRMath::ivec2 origin_{0};
        IRMath::ivec2 size_{0};
    };

    static constexpr std::size_t kGrid = 0;
    static constexpr std::size_t kDetached = 1;
    static constexpr std::size_t kRevoxelize = 2;
    static constexpr std::size_t kSdf = 3;
    static constexpr std::size_t kPinned = 4;
    static constexpr std::size_t kInherit = 0;
    static constexpr std::size_t kOwn = 1;
    static constexpr std::size_t kFirstBandShot = 4;

    static constexpr int kCoarseSize = 6;
    static constexpr int kFineSize = 7;
    static constexpr IRMath::Color kCoarse{225, 55, 45, 255};
    static constexpr IRMath::Color kFine{50, 95, 235, 255};

    static constexpr IRRender::LodZoomProfile::Thresholds kMainThresholds{2.0f, 5.0f, 10.0f, 20.0f};
    static constexpr float kMainHysteresis = 0.25f;
    static constexpr float kOwnHysteresis = 0.45f;

    // One row at iso depth 0 along the screen's horizontal (world +x, -y runs
    // left), left of the portraits at every zoom the shots reach.
    static constexpr std::array<IRMath::vec3, 5> kWorldCenters{
        IRMath::vec3(-11.5f, 11.5f, 0.0f),
        IRMath::vec3(-3.0f, 3.0f, 0.0f),
        IRMath::vec3(5.5f, -5.5f, 0.0f),
        IRMath::vec3(14.0f, -14.0f, 0.0f),
        IRMath::vec3(22.5f, -22.5f, 0.0f),
    };
    static constexpr std::array<const char *, 5> kWorldNames{
        "grid",
        "detached",
        "detached_revoxelize",
        "sdf",
        "pinned_lod0",
    };
    // Below the screen at every shot zoom; the world would draw them at the
    // main tier.
    static constexpr std::array<IRMath::vec3, 2> kPortraitSubjectCenters{
        IRMath::vec3(-80.0f, -80.0f, 0.0f),
        IRMath::vec3(-80.0f, -100.0f, 0.0f),
    };

    // The variant each shot must show, 'c'oarse or 'f'ine. Main: width zero
    // swaps every shot; the band holds fine, leaves below 4.20, holds coarse
    // through 5.9 and leaves above 5.95.
    static constexpr std::string_view kMainVariants = "cfcf"
                                                      "ffffc"
                                                      "cccfff";
    static_assert(kMainVariants.size() == kShotCount);
    static constexpr std::array<std::array<float, kShotCount>, 2> kPortraitZooms{{
        {4, 8, 4, 8, 4, 8, 4, 8, 4, 8, 4, 8, 4, 8, 4},
        {2, 4, 2, 4, 8, 4, 8, 4, 2, 4, 2, 4, 8, 4, 2},
    }};
    // Inherit: under 2,5,10,20 the 4x shots are LOD_3 and the 8x shots LOD_2,
    // at either main width. Own (2,4,8,16, 0.45 octave): 4x holds the side the
    // portrait arrived from; 8x and 2x leave the 4x band.
    static constexpr std::array<std::string_view, 2> kPortraitVariants{
        "cfcfcfcfcfcfcfc",
        "ccccffffccccffc",
    };
    static_assert(
        kPortraitVariants[kInherit].size() == kShotCount &&
        kPortraitVariants[kOwn].size() == kShotCount
    );

    static void setShapeBand(IREntity::EntityId entity, LodLevel coarsest, LodLevel finest) {
        auto &shape = IREntity::getComponent<IRComponents::C_ShapeDescriptor>(entity);
        shape.lodMin_ = coarsest;
        shape.lodMax_ = finest;
    }

    static int occupied(const IRComponents::C_VoxelSetNew &set) {
        int count = 0;
        for (int i = 0; i < set.numVoxels_; ++i) {
            count += set.voxels_[i].color_.alpha_ != 0 ? 1 : 0;
        }
        return count;
    }

    static void spawnDensePair(WorldPair &pair, IRMath::vec3 center) {
        using namespace IRComponents;
        pair.center_ = center;
        pair.coarse_ = IREntity::createEntity(
            C_LocalTransform{center},
            C_VoxelSetNew{IRMath::ivec3(kCoarseSize), kCoarse, EntityAnchor::CENTER}
        );
        pair.fine_ = IREntity::createEntity(
            C_LocalTransform{center},
            C_VoxelSetNew{IRMath::ivec3(kFineSize), kFine, EntityAnchor::CENTER}
        );
        auto &coarse = IREntity::getComponent<C_VoxelSetNew>(pair.coarse_);
        coarse.lodMin_ = LodLevel::LOD_4;
        coarse.lodMax_ = LodLevel::LOD_3;
        auto &fine = IREntity::getComponent<C_VoxelSetNew>(pair.fine_);
        const float radius = 0.5f * static_cast<float>(kFineSize);
        fine.carve([radius](IRMath::vec3 position) { return IRMath::length(position) > radius; });
        fine.lodMin_ = LodLevel::LOD_2;
        fine.lodMax_ = LodLevel::LOD_0;
        pair.coarseVoxels_ = occupied(coarse);
        pair.fineVoxels_ = occupied(fine);
    }

    // The gate hides one member by its band and leaves both members' authored
    // records alone.
    static void verifyDensePair(const WorldPair &pair, bool fine, int shotIndex) {
        const auto &coarseSet = IREntity::getComponent<IRComponents::C_VoxelSetNew>(pair.coarse_);
        const auto &fineSet = IREntity::getComponent<IRComponents::C_VoxelSetNew>(pair.fine_);
        IR_ASSERT(
            coarseSet.renders() == !fine && fineSet.renders() == fine,
            "exactly the expected DENSE variant must render at shot {}",
            shotIndex
        );
        verifyAuthoredRecords(pair);
    }

    static void verifyAuthoredRecords(const WorldPair &pair) {
        const auto &coarseSet = IREntity::getComponent<IRComponents::C_VoxelSetNew>(pair.coarse_);
        const auto &fineSet = IREntity::getComponent<IRComponents::C_VoxelSetNew>(pair.fine_);
        IR_ASSERT(
            occupied(coarseSet) == pair.coarseVoxels_ && occupied(fineSet) == pair.fineVoxels_,
            "a gated DENSE variant must keep its authored records"
        );
    }

    static void verifyShapeRecords(const WorldPair &pair) {
        const auto &coarse = IREntity::getComponent<IRComponents::C_ShapeDescriptor>(pair.coarse_);
        const auto &fine = IREntity::getComponent<IRComponents::C_ShapeDescriptor>(pair.fine_);
        IR_ASSERT(
            coarse.lodMin_ == LodLevel::LOD_4 && coarse.lodMax_ == LodLevel::LOD_3 &&
                fine.lodMin_ == LodLevel::LOD_2 && fine.lodMax_ == LodLevel::LOD_0 &&
                coarse.params_.x == 6.0f && fine.params_.x == 3.5f,
            "the SDF variants must keep their authored records"
        );
    }

    static void
    logRegion(int shotIndex, const char *name, IRMath::ivec2 low, IRMath::ivec2 high, bool fine) {
        IRMath::ivec2 window{0};
        IRWindow::getWindowSize(window);
        IR_LOG_INFO(
            "LOD-PROFILE-REGION shot={} name={} expect={} x0={} y0={} x1={} y1={} window={}x{}",
            shotIndex,
            name,
            fine ? "fine" : "coarse",
            low.x,
            low.y,
            high.x,
            high.y,
            window.x,
            window.y
        );
    }

    // The screen box of the pair's larger member, from its eight corners.
    static void logWorldRegion(int shotIndex, const char *name, const WorldPair &pair, bool fine) {
        const float half = 0.5f * static_cast<float>(kFineSize);
        IRMath::ivec2 low{1 << 30};
        IRMath::ivec2 high{-(1 << 30)};
        for (int corner = 0; corner < 8; ++corner) {
            const IRMath::vec3 offset(
                corner & 1 ? half : -half,
                corner & 2 ? half : -half,
                corner & 4 ? half : -half
            );
            const IRMath::ivec2 px = IRRender::worldPos3DToMouseScreenPx(pair.center_ + offset);
            low = IRMath::min(low, px);
            high = IRMath::max(high, px);
        }
        logRegion(shotIndex, name, low, high, fine);
    }

    static void
    logPortraitRegion(int shotIndex, const char *name, const Portrait &portrait, bool fine) {
        const IRMath::ivec2 low = IRRender::guiTrixelToScreenPx(IRMath::vec2(portrait.origin_));
        const IRMath::ivec2 high =
            IRRender::guiTrixelToScreenPx(IRMath::vec2(portrait.origin_ + portrait.size_));
        logRegion(shotIndex, name, IRMath::min(low, high), IRMath::max(low, high), fine);
    }

    std::array<WorldPair, 5> m_world{};
    std::array<Portrait, 2> m_portraits{};
};

} // namespace IRShapeDebug

#endif /* LOD_ZOOM_PROFILE_SCENE_H */
