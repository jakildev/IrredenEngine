// Canvas-lifecycle contract of `IRPrefab::RotationMode::setMode`: DETACHED and
// DETACHED_REVOXELIZE are one canvas-owning family, and the helper keeps
// `C_EntityCanvas` in sync with the mode in both directions. See #2908.
//
// Every arm below runs headless. The one transition that cannot is
// GRID -> a canvas-owning mode: it calls `IRPrefab::EntityCanvas::create`,
// which `setMode` guards with `IR_ASSERT(IRRender::g_renderManager != nullptr)`.
// That arm needs a live RenderManager and is deliberately absent rather than
// silently skipped — the reconcile-on-mismatch behaviour it would cover is
// pinned from the release side by `SameModeReleasesACanvas...` instead.

#include <gtest/gtest.h>

#include <irreden/common/components/component_rotation_mode.hpp>
#include <irreden/common/rotation_mode.hpp>
#include <irreden/entity/entity_manager.hpp>
#include <irreden/ir_entity.hpp>
#include <irreden/render/components/component_entity_canvas.hpp>

#include <cstdint>

namespace {

using IRComponents::C_EntityCanvas;
using IRComponents::C_RotationMode;
using IRComponents::RotationMode;

// ---- The predicate itself, and its completeness ------------------------

constexpr int modeCount() {
    return static_cast<int>(RotationMode::kLast) - static_cast<int>(RotationMode::kFirst) + 1;
}

constexpr int canvasOwningModeCount() {
    int owning = 0;
    for (auto raw = static_cast<std::uint8_t>(RotationMode::kFirst);
         raw <= static_cast<std::uint8_t>(RotationMode::kLast);
         ++raw) {
        if (IRPrefab::RotationMode::ownsEntityCanvas(static_cast<RotationMode>(raw))) {
            ++owning;
        }
    }
    return owning;
}

// The pair is the guard, not either line alone. Counting only the owning modes
// stays green when a mode is added and left unclassified — it is the total that
// moves. Adding a mode must trip this and force a deliberate visit to
// `ownsEntityCanvas`.
static_assert(
    modeCount() == 3,
    "A RotationMode was added or removed. Classify it in "
    "IRPrefab::RotationMode::ownsEntityCanvas() and update this count (#2908)."
);
static_assert(
    canvasOwningModeCount() == 2,
    "Exactly DETACHED and DETACHED_REVOXELIZE own a per-entity C_EntityCanvas."
);

TEST(RotationModeOwnsEntityCanvas, ClassifiesEachModeExplicitly) {
    EXPECT_FALSE(IRPrefab::RotationMode::ownsEntityCanvas(RotationMode::GRID));
    EXPECT_TRUE(IRPrefab::RotationMode::ownsEntityCanvas(RotationMode::DETACHED));
    EXPECT_TRUE(IRPrefab::RotationMode::ownsEntityCanvas(RotationMode::DETACHED_REVOXELIZE));
}

// ---- setMode's canvas lifecycle ----------------------------------------

class RotationModeSetMode : public testing::Test {
  protected:
    // A canvas-owning entity as it exists between frames: tagged with the
    // mode and carrying the wrapper. `canvasEntity_` stays kNullEntity so
    // `setMode`'s teardown skips `destroyEntity` and the arm needs no
    // RenderManager — the assertion under test is whether the *component*
    // is released, which is what strands the GPU textures in production.
    static IREntity::EntityId makeCanvasBackedEntity(RotationMode mode) {
        return IREntity::createEntity(C_RotationMode{mode}, C_EntityCanvas{});
    }

    static bool hasCanvas(IREntity::EntityId entity) {
        return IREntity::getComponentOptional<C_EntityCanvas>(entity).has_value();
    }

    static RotationMode modeOf(IREntity::EntityId entity) {
        return IREntity::getComponent<C_RotationMode>(entity).mode_;
    }

    IREntity::EntityManager m_entity_manager;
};

// Leaving the canvas-owning family releases the canvas for BOTH members, not
// just DETACHED — this is the arm the family predicate exists for.
TEST_F(RotationModeSetMode, LeavingDetachedRevoxelizeForGridReleasesTheCanvas) {
    const IREntity::EntityId entity = makeCanvasBackedEntity(RotationMode::DETACHED_REVOXELIZE);
    ASSERT_TRUE(hasCanvas(entity));

    IRPrefab::RotationMode::setMode(entity, RotationMode::GRID);

    EXPECT_EQ(modeOf(entity), RotationMode::GRID);
    EXPECT_FALSE(hasCanvas(entity));
}

// The paired control: the same release, entered from the other family member.
// Without it a blanket break in the teardown path would read as a pass on the
// arm above alone, rather than as the mode-specific gap it is.
TEST_F(RotationModeSetMode, LeavingDetachedForGridReleasesTheCanvas) {
    const IREntity::EntityId entity = makeCanvasBackedEntity(RotationMode::DETACHED);
    ASSERT_TRUE(hasCanvas(entity));

    IRPrefab::RotationMode::setMode(entity, RotationMode::GRID);

    EXPECT_EQ(modeOf(entity), RotationMode::GRID);
    EXPECT_FALSE(hasCanvas(entity));
}

// A swap inside the family is a re-tag, not a re-allocation: both modes own a
// canvas, so neither releasing nor re-creating it is correct here.
TEST_F(RotationModeSetMode, SwitchingDetachedToDetachedRevoxelizeKeepsTheCanvas) {
    const IREntity::EntityId entity = makeCanvasBackedEntity(RotationMode::DETACHED);

    IRPrefab::RotationMode::setMode(entity, RotationMode::DETACHED_REVOXELIZE);

    EXPECT_EQ(modeOf(entity), RotationMode::DETACHED_REVOXELIZE);
    EXPECT_TRUE(hasCanvas(entity));
}

TEST_F(RotationModeSetMode, SwitchingDetachedRevoxelizeToDetachedKeepsTheCanvas) {
    const IREntity::EntityId entity = makeCanvasBackedEntity(RotationMode::DETACHED_REVOXELIZE);

    IRPrefab::RotationMode::setMode(entity, RotationMode::DETACHED);

    EXPECT_EQ(modeOf(entity), RotationMode::DETACHED);
    EXPECT_TRUE(hasCanvas(entity));
}

// The early return gates on the canvas matching the mode, not on the mode
// alone. A consistent same-mode call must still take it and attempt no
// allocation — an allocation would trip the RenderManager assert here.
TEST_F(RotationModeSetMode, SameModeWithAMatchingCanvasIsANoOp) {
    const IREntity::EntityId entity = makeCanvasBackedEntity(RotationMode::DETACHED_REVOXELIZE);

    IRPrefab::RotationMode::setMode(entity, RotationMode::DETACHED_REVOXELIZE);

    EXPECT_EQ(modeOf(entity), RotationMode::DETACHED_REVOXELIZE);
    EXPECT_TRUE(hasCanvas(entity));
}

TEST_F(RotationModeSetMode, SameModeOnAPlainGridEntityIsANoOp) {
    const IREntity::EntityId entity = IREntity::createEntity(C_RotationMode{RotationMode::GRID});

    IRPrefab::RotationMode::setMode(entity, RotationMode::GRID);

    EXPECT_EQ(modeOf(entity), RotationMode::GRID);
    EXPECT_FALSE(hasCanvas(entity));
}

// Mode-matches-but-canvas-does-not is the case the early return must NOT
// swallow: a GRID entity carrying a stray C_EntityCanvas gets reconciled
// rather than short-circuited. This is the headlessly-reachable half of that
// rule; its mirror (a detached-tagged entity with no canvas) is the
// allocation path the file header notes cannot run without a RenderManager.
TEST_F(RotationModeSetMode, SameModeReleasesACanvasTheModeDoesNotOwn) {
    const IREntity::EntityId entity = makeCanvasBackedEntity(RotationMode::GRID);
    ASSERT_TRUE(hasCanvas(entity));

    IRPrefab::RotationMode::setMode(entity, RotationMode::GRID);

    EXPECT_EQ(modeOf(entity), RotationMode::GRID);
    EXPECT_FALSE(hasCanvas(entity));
}

// An entity that never carried C_RotationMode is implicitly GRID, so the
// component-less path must reach the same conclusion as the tagged one.
TEST_F(RotationModeSetMode, ImplicitGridEntityWithAStrayCanvasIsReconciled) {
    const IREntity::EntityId entity = IREntity::createEntity(C_EntityCanvas{});

    IRPrefab::RotationMode::setMode(entity, RotationMode::GRID);

    EXPECT_EQ(modeOf(entity), RotationMode::GRID);
    EXPECT_FALSE(hasCanvas(entity));
}

} // namespace
