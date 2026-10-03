#include <gtest/gtest.h>

#include <irreden/ir_entity.hpp>
#include <irreden/ir_math.hpp>
#include <irreden/ir_system.hpp>

#include <irreden/common/components/component_size_triangles.hpp>
#include <irreden/common/components/component_world_transform.hpp>
#include <irreden/render/components/component_canvas_camera.hpp>
#include <irreden/render/components/component_canvas_local_rotation.hpp>
#include <irreden/render/components/component_lod_tier_override.hpp>
#include <irreden/render/components/component_viewport_camera.hpp>
#include <irreden/render/components/component_viewport_subject.hpp>
#include <irreden/render/components/component_zoom_level.hpp>
#include <irreden/render/systems/system_sync_viewport_subjects.hpp>
#include <irreden/render/viewport.hpp>
#include <irreden/voxel/components/component_voxel_pool.hpp>
#include <irreden/voxel/components/component_voxel_set.hpp>

#include <initializer_list>
#include <vector>

// Secondary-viewport layout math and the per-frame subject sync.
//
// Headless: a viewport's canvas here carries only the components the sync
// reads (pool, canvas rotation, canvas camera, size), never GPU textures, and
// every subject set allocates from an explicit pool canvas, so nothing reaches
// the RenderManager.

namespace {

using IRComponents::C_CanvasCamera;
using IRComponents::C_CanvasLocalRotation;
using IRComponents::C_SizeTriangles;
using IRComponents::C_ViewportCamera;
using IRComponents::C_ViewportSubject;
using IRComponents::C_Voxel;
using IRComponents::C_VoxelPool;
using IRComponents::C_VoxelSetNew;
using IRComponents::C_ZoomLevel;
using IRComponents::EntityAnchor;
using IRMath::Color;
using IRMath::ivec2;
using IRMath::ivec3;
using IRMath::vec2;
using IRMath::vec3;
using IRMath::vec4;
using IRPrefab::Viewport::SubjectPart;
using IRRender::LodLevel;

namespace Viewport = IRPrefab::Viewport;

constexpr Color kRed{230, 90, 60, 255};
constexpr Color kBlue{60, 140, 230, 255};
constexpr vec4 kIdentity{0.0f, 0.0f, 0.0f, 1.0f};

class ViewportTest : public testing::Test {
  protected:
    IREntity::EntityManager m_entity_manager;
    IRSystem::SystemManager m_system_manager;
    // The pool the subjects' own sets live in — the stand-in for the world canvas.
    IREntity::EntityId m_world = IREntity::createEntity(C_VoxelPool{ivec3(16, 16, 16)});
    Viewport::detail::SubjectBox m_box;

    static IREntity::EntityId makeCanvas() {
        return IREntity::createEntity(
            C_VoxelPool{ivec3(0)},
            C_CanvasLocalRotation{},
            C_CanvasCamera{},
            C_SizeTriangles{ivec2(1)}
        );
    }

    static IREntity::EntityId makeViewport(const Viewport::Desc &desc = {}) {
        return Viewport::detail::createCamera(makeCanvas(), desc);
    }

    IREntity::EntityId makeSubject(ivec3 size, Color color, vec3 translation = vec3(0.0f)) const {
        return IREntity::createEntity(
            IRComponents::C_LocalTransform{translation},
            C_VoxelSetNew{size, color, EntityAnchor::CORNER, m_world}
        );
    }

    static SubjectPart partOf(IREntity::EntityId entity) {
        return SubjectPart{
            entity,
            &IREntity::getComponent<C_VoxelSetNew>(entity),
            IREntity::getComponent<IRComponents::C_LocalTransform>(entity).translation_
        };
    }

    void sync(IREntity::EntityId viewport, const std::vector<SubjectPart> &parts) {
        Viewport::detail::syncSubjectPool(
            IREntity::getComponent<C_ViewportCamera>(viewport),
            parts,
            m_box
        );
    }

    // The parts one SYNC_VIEWPORT_SUBJECTS gather hands @p viewport's pool when
    // @p subjects are tagged to it; the camera and composite halves need a
    // RenderManager and are left out.
    static std::vector<SubjectPart>
    gather(IREntity::EntityId viewport, std::initializer_list<IREntity::EntityId> subjects) {
        IRSystem::System<IRSystem::SYNC_VIEWPORT_SUBJECTS> system;
        system.beginTick();
        for (const IREntity::EntityId subject : subjects) {
            system.tick(
                subject,
                C_ViewportSubject{viewport},
                IREntity::getComponent<C_VoxelSetNew>(subject),
                IRComponents::C_WorldTransform{
                    IREntity::getComponent<IRComponents::C_LocalTransform>(subject).translation_,
                    kIdentity,
                    vec3(1.0f)
                }
            );
        }
        for (std::size_t i = 0; i < system.slotCount_; ++i) {
            if (system.slots_[i].viewport_ == viewport) {
                return system.slots_[i].parts_;
            }
        }
        return {};
    }

    // Two co-located variants on disjoint bands: a coarse red cube for zoom < 4
    // and a fine blue one for zoom >= 4.
    struct VariantFamily {
        IREntity::EntityId coarse_;
        IREntity::EntityId fine_;
    };

    VariantFamily makeVariantFamily() const {
        const IREntity::EntityId coarse = makeSubject(ivec3(2), kRed);
        auto &coarseSet = IREntity::getComponent<C_VoxelSetNew>(coarse);
        coarseSet.lodMin_ = LodLevel::LOD_4;
        coarseSet.lodMax_ = LodLevel::LOD_3;
        const IREntity::EntityId fine = makeSubject(ivec3(3), kBlue);
        auto &fineSet = IREntity::getComponent<C_VoxelSetNew>(fine);
        fineSet.lodMin_ = LodLevel::LOD_2;
        fineSet.lodMax_ = LodLevel::LOD_0;
        return {coarse, fine};
    }

    static void expectPoolHoldsOnly(IREntity::EntityId viewport, ivec3 size, Color color) {
        const C_VoxelPool &pool = poolOf(viewport);
        EXPECT_EQ(pool.getVoxelPoolSize3D(), size);
        const int count = size.x * size.y * size.z;
        ASSERT_EQ(pool.getLiveVoxelCount(), count);
        EXPECT_EQ(activeCount(pool), count);
        for (int i = 0; i < count; ++i) {
            EXPECT_EQ(pool.getColors()[i].color_.toPackedRGBA(), color.toPackedRGBA());
        }
    }

    static C_VoxelPool &poolOf(IREntity::EntityId viewport) {
        return IREntity::getComponent<C_VoxelPool>(Viewport::canvasOf(viewport));
    }

    static int activeCount(const C_VoxelPool &pool) {
        int active = 0;
        for (int i = 0; i < pool.getLiveVoxelCount(); ++i) {
            active += pool.getColors()[i].color_.alpha_ != 0 ? 1 : 0;
        }
        return active;
    }

    static int taggedCount(IREntity::EntityId viewport) {
        int tagged = 0;
        IREntity::forEachComponent<C_ViewportSubject>([&](C_ViewportSubject &tag) {
            tagged += tag.viewport_ == viewport ? 1 : 0;
        });
        return tagged;
    }

    static bool isTagged(IREntity::EntityId entity, IREntity::EntityId viewport) {
        auto tag = IREntity::getComponentOptional<C_ViewportSubject>(entity);
        return tag.has_value() && tag.value()->viewport_ == viewport;
    }
};

// ---------------------------------------------------------------------------
// Layout math
// ---------------------------------------------------------------------------

TEST_F(ViewportTest, TexelCoversTwoByOnePixelsScaledByZoomOverDensity) {
    EXPECT_EQ(Viewport::detail::texelFramebufferSize(1.0f, 1), vec2(2.0f, 1.0f));
    EXPECT_EQ(Viewport::detail::texelFramebufferSize(16.0f, 1), vec2(32.0f, 16.0f));
    EXPECT_EQ(Viewport::detail::texelFramebufferSize(16.0f, 16), vec2(2.0f, 1.0f));
    EXPECT_EQ(Viewport::detail::texelFramebufferSize(16.0f, 0), vec2(32.0f, 16.0f));
}

TEST_F(ViewportTest, CanvasSizeIsTheLargestEvenTexelCountInsideTheRect) {
    // 360 / 32 = 11.25 -> 11 -> even 10; 260 / 16 = 16.25 -> 16.
    EXPECT_EQ(Viewport::detail::canvasSizeForRect(vec2(360.0f, 260.0f), 16.0f, 1), ivec2(10, 16));
    // Full density: one texel per 2x1 pixels.
    EXPECT_EQ(
        Viewport::detail::canvasSizeForRect(vec2(360.0f, 260.0f), 16.0f, 16),
        ivec2(180, 260)
    );
    // A rect smaller than one texel still yields a drawable canvas.
    EXPECT_EQ(Viewport::detail::canvasSizeForRect(vec2(8.0f, 8.0f), 16.0f, 1), ivec2(2, 2));
}

TEST_F(ViewportTest, GuiRectScalesByTheFramebufferOverGuiCanvasRatio) {
    const auto rect = Viewport::detail::framebufferRect(
        ivec2(50, 370),
        ivec2(180, 260),
        ivec2(640, 720),
        ivec2(1280, 720)
    );
    EXPECT_EQ(rect.origin_, vec2(100.0f, 370.0f));
    EXPECT_EQ(rect.size_, vec2(360.0f, 260.0f));
    // Full-resolution GUI canvas: one GUI trixel is one framebuffer pixel.
    const auto native = Viewport::detail::framebufferRect(
        ivec2(50, 370),
        ivec2(180, 260),
        ivec2(1280, 720),
        ivec2(1280, 720)
    );
    EXPECT_EQ(native.origin_, vec2(50.0f, 370.0f));
    EXPECT_EQ(native.size_, vec2(180.0f, 260.0f));
}

TEST_F(ViewportTest, FocusPanCentersTheFocusAndKeepsLatticeParity) {
    EXPECT_EQ(Viewport::detail::panTexelsForFocus(vec3(0.0f), kIdentity, 1), ivec2(0, 0));
    // iso(1,0,0) = (-1,-1): shift the raster by (1,1) to bring it to center.
    EXPECT_EQ(
        Viewport::detail::panTexelsForFocus(vec3(1.0f, 0.0f, 0.0f), kIdentity, 1),
        ivec2(1, 1)
    );
    // Density multiplies the shift.
    EXPECT_EQ(
        Viewport::detail::panTexelsForFocus(vec3(1.0f, 0.0f, 0.0f), kIdentity, 4),
        ivec2(4, 4)
    );
    // iso(1,0,0.5) = (-1,0): an odd-sum shift is bumped to the next even one.
    const ivec2 odd = Viewport::detail::panTexelsForFocus(vec3(1.0f, 0.0f, 0.5f), kIdentity, 1);
    EXPECT_EQ(odd, ivec2(1, 1));
    // A quarter turn of camera yaw moves the focus to the other screen side:
    // world +x is view -y, and iso(0,-1,0) = (-1,1).
    const vec4 quarterTurn = IRMath::quatAxisAngle(vec3(0.0f, 0.0f, 1.0f), IRMath::kHalfPi);
    EXPECT_EQ(
        Viewport::detail::panTexelsForFocus(vec3(1.0f, 0.0f, 0.0f), quarterTurn, 1),
        ivec2(1, -1)
    );
}

TEST_F(ViewportTest, PanIsoFloorsBackToTheRequestedTexelsAtEveryDensity) {
    for (int subdivisions : {1, 2, 3, 5, 7, 16}) {
        for (int texel = -9; texel <= 9; ++texel) {
            const vec2 pan = Viewport::detail::panIsoForTexels(ivec2(texel, -texel), subdivisions);
            const vec2 scaled = pan * static_cast<float>(subdivisions);
            EXPECT_EQ(static_cast<int>(IRMath::floor(scaled.x)), texel) << subdivisions;
            EXPECT_EQ(static_cast<int>(IRMath::floor(scaled.y)), -texel) << subdivisions;
        }
    }
}

TEST_F(ViewportTest, BoxMustFitTheVoxelBuffersAtRestAndRotated) {
    // 4^3 = 64 cells; rotated it needs the 7^3 = 343 cube around radius 2.6.
    EXPECT_EQ(Viewport::detail::rotatedCellCount(ivec3(4)), 343);
    EXPECT_TRUE(Viewport::detail::boxFitsVoxelBuffers(ivec3(4), 343));
    EXPECT_FALSE(Viewport::detail::boxFitsVoxelBuffers(ivec3(4), 342));
    EXPECT_FALSE(Viewport::detail::boxFitsVoxelBuffers(ivec3(0), 1 << 20));
}

// ---------------------------------------------------------------------------
// Camera sync
// ---------------------------------------------------------------------------

TEST_F(ViewportTest, CreateCameraSnapsZoomAndCarriesYawAndRect) {
    Viewport::Desc desc{};
    desc.rectOrigin_ = ivec2(50, 370);
    desc.rectSize_ = ivec2(180, 260);
    desc.zoom_ = 13.0f;
    desc.yawRadians_ = IRMath::kHalfPi;
    desc.focus_ = vec3(0.0f, 0.0f, 1.0f);
    const IREntity::EntityId viewport = makeViewport(desc);

    ASSERT_TRUE(Viewport::isViewport(viewport));
    const auto &camera = IREntity::getComponent<C_ViewportCamera>(viewport);
    EXPECT_EQ(camera.rectOrigin_, ivec2(50, 370));
    EXPECT_EQ(camera.rectSize_, ivec2(180, 260));
    EXPECT_EQ(camera.focus_, vec3(0.0f, 0.0f, 1.0f));
    EXPECT_EQ(IREntity::getComponent<C_ZoomLevel>(viewport).zoom_, vec2(16.0f));
    const vec4 rotation =
        IREntity::getComponent<IRComponents::C_LocalTransform>(viewport).rotation_;
    EXPECT_NEAR(rotation.z, IRMath::sin(IRMath::kHalfPi * 0.5f), 1e-6f);
    EXPECT_NEAR(rotation.w, IRMath::cos(IRMath::kHalfPi * 0.5f), 1e-6f);
    EXPECT_FALSE(Viewport::isViewport(Viewport::canvasOf(viewport)));
}

TEST_F(ViewportTest, SettersMoveResizeAndRecameraTheViewport) {
    const IREntity::EntityId viewport = makeViewport();
    Viewport::setRect(viewport, ivec2(60, 380), ivec2(160, 240));
    Viewport::setCamera(viewport, 8.0f, 0.0f);
    Viewport::setFocus(viewport, vec3(1.0f, 2.0f, 3.0f));
    Viewport::setVisible(viewport, false);

    const auto &camera = IREntity::getComponent<C_ViewportCamera>(viewport);
    EXPECT_EQ(camera.rectOrigin_, ivec2(60, 380));
    EXPECT_EQ(camera.rectSize_, ivec2(160, 240));
    EXPECT_EQ(camera.focus_, vec3(1.0f, 2.0f, 3.0f));
    EXPECT_FALSE(camera.visible_);
    EXPECT_EQ(IREntity::getComponent<C_ZoomLevel>(viewport).zoom_, vec2(8.0f));

    // A zero or negative size is clamped to one trixel.
    Viewport::setRect(viewport, ivec2(0), ivec2(0, -4));
    EXPECT_EQ(camera.rectSize_, ivec2(1, 1));
}

TEST_F(ViewportTest, CameraSyncPublishesTheCanvasCameraAndSizesTheCanvas) {
    Viewport::Desc desc{};
    desc.rectOrigin_ = ivec2(50, 370);
    desc.rectSize_ = ivec2(180, 260);
    const IREntity::EntityId viewport = makeViewport(desc);
    const IREntity::EntityId canvas = Viewport::canvasOf(viewport);
    const vec4 quarterTurn = IRMath::quatAxisAngle(vec3(0.0f, 0.0f, 1.0f), IRMath::kHalfPi);

    Viewport::detail::syncCanvasCamera(
        IREntity::getComponent<C_ViewportCamera>(viewport),
        quarterTurn,
        vec2(16.0f),
        1,
        ivec2(640, 720),
        ivec2(1280, 720)
    );

    const auto &canvasCamera = IREntity::getComponent<C_CanvasCamera>(canvas);
    EXPECT_EQ(canvasCamera.rotation_, quarterTurn);
    EXPECT_EQ(canvasCamera.zoom_, vec2(16.0f));
    const auto &rotation = IREntity::getComponent<C_CanvasLocalRotation>(canvas);
    EXPECT_TRUE(rotation.isDetached());
    EXPECT_EQ(rotation.rotation_, IRMath::quatInverse(quarterTurn));
    EXPECT_TRUE(rotation.reVoxelize_);
    EXPECT_FALSE(rotation.worldPlaced_);
    EXPECT_FALSE(rotation.castsWorldShadow_);
    EXPECT_EQ(IREntity::getComponent<C_SizeTriangles>(canvas).size_, ivec2(10, 16));
}

TEST_F(ViewportTest, AnUnrotatedCameraLeavesTheCanvasAtTheIdentityRotation) {
    const IREntity::EntityId viewport = makeViewport();
    Viewport::detail::syncCanvasCamera(
        IREntity::getComponent<C_ViewportCamera>(viewport),
        kIdentity,
        vec2(1.0f),
        1,
        ivec2(640, 720),
        ivec2(1280, 720)
    );
    // Identity, not the all-zero "world canvas" sentinel.
    const auto &rotation =
        IREntity::getComponent<C_CanvasLocalRotation>(Viewport::canvasOf(viewport));
    EXPECT_TRUE(rotation.isDetached());
    EXPECT_EQ(rotation.rotation_, kIdentity);
}

// ---------------------------------------------------------------------------
// Subject sync
// ---------------------------------------------------------------------------

TEST_F(ViewportTest, SubjectIsRasterizedIntoACenteredBoxWithItsEntityId) {
    const IREntity::EntityId viewport = makeViewport();
    const IREntity::EntityId subject = makeSubject(ivec3(2, 3, 4), kRed, vec3(40.0f, -7.0f, 3.0f));

    sync(viewport, {partOf(subject)});

    const C_VoxelPool &pool = poolOf(viewport);
    EXPECT_EQ(pool.getVoxelPoolSize3D(), ivec3(2, 3, 4));
    ASSERT_EQ(pool.getLiveVoxelCount(), 24);
    EXPECT_EQ(activeCount(pool), 24);
    vec3 positionSum(0.0f);
    for (int i = 0; i < 24; ++i) {
        EXPECT_EQ(pool.getColors()[i].color_.toPackedRGBA(), kRed.toPackedRGBA());
        EXPECT_EQ(pool.getEntityIds()[i], subject);
        EXPECT_EQ(pool.getPositionGlobals()[i].pos_, pool.getPositions()[i].pos_);
        positionSum += pool.getPositions()[i].pos_;
    }
    // Centered on the pool origin wherever the subject stands in the world.
    EXPECT_EQ(positionSum, vec3(0.0f));
    EXPECT_EQ(Viewport::drawnSubject(viewport), subject);
    // The subject's own span is untouched.
    EXPECT_EQ(IREntity::getComponent<C_VoxelPool>(m_world).getLiveVoxelCount(), 24);
}

TEST_F(ViewportTest, AuthoredAlphaIsMirroredIncludingForAHiddenSet) {
    const IREntity::EntityId viewport = makeViewport();
    const IREntity::EntityId subject = makeSubject(ivec3(2, 2, 2), kRed);
    auto &set = IREntity::getComponent<C_VoxelSetNew>(subject);
    set.carve([](vec3 position) { return position.x > 0.5f; });
    set.visible_ = false;

    sync(viewport, {partOf(subject)});

    const C_VoxelPool &pool = poolOf(viewport);
    ASSERT_EQ(pool.getLiveVoxelCount(), 8);
    EXPECT_EQ(activeCount(pool), 4);
}

TEST_F(ViewportTest, SourceOnlyVoxelStateIsNotCarriedIntoThePool) {
    const IREntity::EntityId viewport = makeViewport();
    const IREntity::EntityId subject = makeSubject(ivec3(1, 1, 1), kRed);
    auto &set = IREntity::getComponent<C_VoxelSetNew>(subject);
    set.voxels_[0].bone_id_ = 3;
    set.voxels_[0].reserved_ = IRComponents::VoxelReserved::kRotatedEmit | 2u;
    set.voxels_[0].flags_ |= IRComponents::VoxelFlags::kFaceOccludedMask;

    sync(viewport, {partOf(subject)});

    const C_Voxel &voxel = poolOf(viewport).getColors()[0];
    EXPECT_EQ(voxel.bone_id_, 0);
    EXPECT_EQ(voxel.reserved_, 0u);
    EXPECT_EQ(voxel.flags_ & IRComponents::VoxelFlags::kFaceOccludedMask, 0);
    EXPECT_NE(voxel.flags_ & IRComponents::VoxelFlags::kAoContrib, 0);
}

TEST_F(ViewportTest, RetargetRebuildsThePoolAndRestampsTheEntityId) {
    const IREntity::EntityId viewport = makeViewport();
    const IREntity::EntityId first = makeSubject(ivec3(2, 2, 2), kRed);
    const IREntity::EntityId second = makeSubject(ivec3(3, 1, 2), kBlue);

    sync(viewport, {partOf(first)});
    sync(viewport, {partOf(second)});

    const C_VoxelPool &pool = poolOf(viewport);
    EXPECT_EQ(pool.getVoxelPoolSize3D(), ivec3(3, 1, 2));
    ASSERT_EQ(pool.getLiveVoxelCount(), 6);
    for (int i = 0; i < 6; ++i) {
        EXPECT_EQ(pool.getColors()[i].color_.toPackedRGBA(), kBlue.toPackedRGBA());
        EXPECT_EQ(pool.getEntityIds()[i], second);
    }
    EXPECT_EQ(Viewport::drawnSubject(viewport), second);
}

TEST_F(ViewportTest, SameSizeRetargetRestampsWithoutRebuilding) {
    const IREntity::EntityId viewport = makeViewport();
    const IREntity::EntityId first = makeSubject(ivec3(2, 2, 2), kRed);
    const IREntity::EntityId second = makeSubject(ivec3(2, 2, 2), kBlue);

    sync(viewport, {partOf(first)});
    const auto contentGeneration = poolOf(viewport).getContentGeneration();
    sync(viewport, {partOf(second)});

    const C_VoxelPool &pool = poolOf(viewport);
    ASSERT_EQ(pool.getLiveVoxelCount(), 8);
    EXPECT_GT(pool.getContentGeneration(), contentGeneration);
    for (int i = 0; i < 8; ++i) {
        EXPECT_EQ(pool.getColors()[i].color_.toPackedRGBA(), kBlue.toPackedRGBA());
        EXPECT_EQ(pool.getEntityIds()[i], second);
    }
}

TEST_F(ViewportTest, SubjectEditsReachThePoolOnTheNextSync) {
    const IREntity::EntityId viewport = makeViewport();
    const IREntity::EntityId subject = makeSubject(ivec3(2, 1, 1), kRed);
    sync(viewport, {partOf(subject)});
    const auto contentGeneration = poolOf(viewport).getContentGeneration();

    IREntity::getComponent<C_VoxelSetNew>(subject).changeVoxelColor(ivec3(1, 0, 0), kBlue);
    sync(viewport, {partOf(subject)});

    const C_VoxelPool &pool = poolOf(viewport);
    EXPECT_GT(pool.getContentGeneration(), contentGeneration);
    EXPECT_EQ(pool.getColors()[0].color_.toPackedRGBA(), kRed.toPackedRGBA());
    EXPECT_EQ(pool.getColors()[1].color_.toPackedRGBA(), kBlue.toPackedRGBA());
}

TEST_F(ViewportTest, TaggedPartsMergeIntoOneBoxAtTheirRelativeCells) {
    const IREntity::EntityId viewport = makeViewport();
    const IREntity::EntityId left = makeSubject(ivec3(1, 1, 1), kRed, vec3(10.0f, 4.0f, 0.0f));
    const IREntity::EntityId right = makeSubject(ivec3(1, 1, 1), kBlue, vec3(12.0f, 4.0f, 0.0f));

    sync(viewport, {partOf(left), partOf(right)});

    const C_VoxelPool &pool = poolOf(viewport);
    EXPECT_EQ(pool.getVoxelPoolSize3D(), ivec3(3, 1, 1));
    ASSERT_EQ(pool.getLiveVoxelCount(), 3);
    EXPECT_EQ(pool.getColors()[0].color_.toPackedRGBA(), kRed.toPackedRGBA());
    EXPECT_EQ(pool.getColors()[1].color_.alpha_, 0);
    EXPECT_EQ(pool.getColors()[2].color_.toPackedRGBA(), kBlue.toPackedRGBA());
    // The canvas carries the first part's id.
    EXPECT_EQ(Viewport::drawnSubject(viewport), left);
}

TEST_F(ViewportTest, ALowerPartBeforeTheReferencePartStillLandsInsideTheBox) {
    const IREntity::EntityId viewport = makeViewport();
    const IREntity::EntityId reference = makeSubject(ivec3(1, 1, 1), kRed, vec3(5.0f, 5.0f, 5.0f));
    const IREntity::EntityId below = makeSubject(ivec3(1, 1, 2), kBlue, vec3(5.0f, 4.0f, 3.0f));

    sync(viewport, {partOf(reference), partOf(below)});

    const C_VoxelPool &pool = poolOf(viewport);
    EXPECT_EQ(pool.getVoxelPoolSize3D(), ivec3(1, 2, 3));
    EXPECT_EQ(activeCount(pool), 3);
    // The reference part sits at box cell (0,1,2); the lower part at (0,0,0..1).
    EXPECT_EQ(
        pool.getColors()[IRMath::index3DtoIndex1D(ivec3(0, 1, 2), ivec3(1, 2, 3))]
            .color_.toPackedRGBA(),
        kRed.toPackedRGBA()
    );
    EXPECT_EQ(
        pool.getColors()[IRMath::index3DtoIndex1D(ivec3(0, 0, 1), ivec3(1, 2, 3))]
            .color_.toPackedRGBA(),
        kBlue.toPackedRGBA()
    );
}

TEST_F(ViewportTest, NoSubjectEmptiesThePool) {
    const IREntity::EntityId viewport = makeViewport();
    const IREntity::EntityId subject = makeSubject(ivec3(2, 2, 2), kRed);
    sync(viewport, {partOf(subject)});
    ASSERT_EQ(poolOf(viewport).getLiveVoxelCount(), 8);

    sync(viewport, {});

    EXPECT_EQ(poolOf(viewport).getLiveVoxelCount(), 0);
    EXPECT_EQ(Viewport::drawnSubject(viewport), IREntity::kNullEntity);
    EXPECT_FALSE(IREntity::getComponent<C_ViewportCamera>(viewport).subjectOversize_);
}

// ---------------------------------------------------------------------------
// LOD bands
// ---------------------------------------------------------------------------

TEST_F(ViewportTest, ACloseUpViewportDrawsOnlyTheFineVariantOfABandedFamily) {
    Viewport::Desc desc{};
    desc.zoom_ = 16.0f;
    const IREntity::EntityId viewport = makeViewport(desc);
    const VariantFamily family = makeVariantFamily();

    const std::vector<SubjectPart> parts = gather(viewport, {family.coarse_, family.fine_});
    ASSERT_EQ(parts.size(), 1u);
    EXPECT_EQ(parts[0].entity_, family.fine_);
    sync(viewport, parts);

    expectPoolHoldsOnly(viewport, ivec3(3), kBlue);
    EXPECT_EQ(Viewport::drawnSubject(viewport), family.fine_);
}

TEST_F(ViewportTest, AWideViewportDrawsOnlyTheCoarseVariantOfABandedFamily) {
    const IREntity::EntityId viewport = makeViewport();
    const VariantFamily family = makeVariantFamily();

    const std::vector<SubjectPart> parts = gather(viewport, {family.coarse_, family.fine_});
    ASSERT_EQ(parts.size(), 1u);
    EXPECT_EQ(parts[0].entity_, family.coarse_);
    sync(viewport, parts);

    expectPoolHoldsOnly(viewport, ivec3(2), kRed);
}

TEST_F(ViewportTest, EachViewportBandsAtItsOwnZoom) {
    Viewport::Desc closeUp{};
    closeUp.zoom_ = 16.0f;
    const IREntity::EntityId fineView = makeViewport(closeUp);
    const IREntity::EntityId coarseView = makeViewport();
    const VariantFamily family = makeVariantFamily();

    const std::vector<SubjectPart> fineParts = gather(fineView, {family.coarse_, family.fine_});
    const std::vector<SubjectPart> coarseParts = gather(coarseView, {family.coarse_, family.fine_});

    ASSERT_EQ(fineParts.size(), 1u);
    EXPECT_EQ(fineParts[0].entity_, family.fine_);
    ASSERT_EQ(coarseParts.size(), 1u);
    EXPECT_EQ(coarseParts[0].entity_, family.coarse_);
}

// The override pins the world's tier; the portrait still resolves its own zoom.
TEST_F(ViewportTest, ATierOverridePinsTheWorldNotTheViewport) {
    Viewport::Desc desc{};
    desc.zoom_ = 16.0f;
    const IREntity::EntityId viewport = makeViewport(desc);
    const VariantFamily family = makeVariantFamily();
    IREntity::setComponent(family.coarse_, IRComponents::C_LodTierOverride{LodLevel::LOD_4});
    IREntity::setComponent(family.fine_, IRComponents::C_LodTierOverride{LodLevel::LOD_4});

    const std::vector<SubjectPart> parts = gather(viewport, {family.coarse_, family.fine_});
    ASSERT_EQ(parts.size(), 1u);
    EXPECT_EQ(parts[0].entity_, family.fine_);
    sync(viewport, parts);

    expectPoolHoldsOnly(viewport, ivec3(3), kBlue);
}

TEST_F(ViewportTest, NoVariantInBandEmptiesThePool) {
    Viewport::Desc desc{};
    desc.zoom_ = 16.0f;
    const IREntity::EntityId viewport = makeViewport(desc);
    const IREntity::EntityId coarseOnly = makeVariantFamily().coarse_;

    sync(viewport, gather(viewport, {coarseOnly}));

    EXPECT_EQ(poolOf(viewport).getLiveVoxelCount(), 0);
    EXPECT_EQ(Viewport::drawnSubject(viewport), IREntity::kNullEntity);
}

// ---------------------------------------------------------------------------
// Subject tags
// ---------------------------------------------------------------------------

TEST_F(ViewportTest, SetSubjectTagsOneEntityAndRetargetMovesTheTagInOneDrain) {
    const IREntity::EntityId viewport = makeViewport();
    const IREntity::EntityId first = makeSubject(ivec3(1, 1, 1), kRed);
    const IREntity::EntityId second = makeSubject(ivec3(1, 1, 1), kBlue);

    Viewport::setSubject(viewport, first);
    // Deferred: nothing is tagged until the structural drain.
    EXPECT_EQ(taggedCount(viewport), 0);
    IREntity::flushStructuralChanges();
    EXPECT_TRUE(isTagged(first, viewport));
    EXPECT_EQ(taggedCount(viewport), 1);

    Viewport::setSubject(viewport, second);
    IREntity::flushStructuralChanges();
    EXPECT_FALSE(isTagged(first, viewport));
    EXPECT_TRUE(isTagged(second, viewport));
    EXPECT_EQ(taggedCount(viewport), 1);

    // Re-tagging the current subject changes nothing.
    Viewport::setSubject(viewport, second);
    IREntity::flushStructuralChanges();
    EXPECT_TRUE(isTagged(second, viewport));
    EXPECT_EQ(taggedCount(viewport), 1);

    Viewport::setSubject(viewport, IREntity::kNullEntity);
    IREntity::flushStructuralChanges();
    EXPECT_EQ(taggedCount(viewport), 0);
}

TEST_F(ViewportTest, SetSubjectLeavesAnotherViewportsTagAlone) {
    const IREntity::EntityId viewport = makeViewport();
    const IREntity::EntityId other = makeViewport();
    const IREntity::EntityId first = makeSubject(ivec3(1, 1, 1), kRed);
    const IREntity::EntityId second = makeSubject(ivec3(1, 1, 1), kBlue);
    Viewport::setSubject(other, first);
    IREntity::flushStructuralChanges();

    Viewport::setSubject(viewport, second);
    IREntity::flushStructuralChanges();

    EXPECT_TRUE(isTagged(first, other));
    EXPECT_TRUE(isTagged(second, viewport));
}

TEST_F(ViewportTest, DestroyRemovesTheViewportItsCanvasAndItsTags) {
    const IREntity::EntityId viewport = makeViewport();
    const IREntity::EntityId canvas = Viewport::canvasOf(viewport);
    const IREntity::EntityId subject = makeSubject(ivec3(1, 1, 1), kRed);
    Viewport::setSubject(viewport, subject);
    IREntity::flushStructuralChanges();

    Viewport::destroy(viewport);
    IREntity::flushStructuralChanges();
    m_entity_manager.destroyMarkedEntities();

    EXPECT_FALSE(IREntity::entityExists(viewport));
    EXPECT_FALSE(IREntity::entityExists(canvas));
    EXPECT_TRUE(IREntity::entityExists(subject));
    EXPECT_FALSE(IREntity::getComponentOptional<C_ViewportSubject>(subject).has_value());
    EXPECT_FALSE(Viewport::isViewport(viewport));
    // Every entry point is a no-op on a dead id.
    Viewport::setRect(viewport, ivec2(1), ivec2(1));
    Viewport::setCamera(viewport, 2.0f, 0.0f);
    Viewport::destroy(viewport);
    EXPECT_EQ(Viewport::drawnSubject(viewport), IREntity::kNullEntity);
}

} // namespace
