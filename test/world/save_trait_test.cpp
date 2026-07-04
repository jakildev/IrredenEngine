#include <gtest/gtest.h>

#include <irreden/world/save_component_inventory.hpp>
#include <irreden/world/save_trait.hpp>

#include <cstddef>
#include <cstdint>
#include <utility>

namespace {

using namespace IRWorld;
using namespace IRComponents;

// Class A — GPU-handle / ResourceId-owning bearers must opt out; W-10
// regenerates the handle post-load, so the pre-load value is never
// meaningful to persist.
TEST(SaveTrait, ClassABearersOptOut) {
    EXPECT_FALSE(shouldSave<C_TrixelCanvasFramebuffer>());
    EXPECT_FALSE(shouldSave<C_TriangleCanvasTextures>());
    EXPECT_FALSE(shouldSave<C_CanvasAOTexture>());
    EXPECT_FALSE(shouldSave<C_CanvasFogOfWar>());
    EXPECT_FALSE(shouldSave<C_CanvasSunShadow>());
    EXPECT_FALSE(shouldSave<C_CanvasLightVolume>());
    EXPECT_FALSE(shouldSave<C_PerAxisTrixelCanvases>());
    EXPECT_FALSE(shouldSave<C_GPUParticlePool>());
    EXPECT_FALSE(shouldSave<C_StatelessParticleEmitters>());
    EXPECT_FALSE(shouldSave<C_DetachedRevoxelizeBuffer>());
    EXPECT_FALSE(shouldSave<C_SpriteSheet>());
    EXPECT_FALSE(shouldSave<C_Sprite>());
}

// Safety-critical opt-outs called out by the plan: derived/rebuildable
// state and engine-internal plumbing must never round-trip through a
// world snapshot.
TEST(SaveTrait, SafetyCriticalOptOuts) {
    EXPECT_FALSE(shouldSave<C_LambdaModifiers>());
    EXPECT_FALSE(shouldSave<C_ContactEvent>());
    EXPECT_FALSE(shouldSave<C_SpatialIndex>());
    EXPECT_FALSE(shouldSave<C_VoxelPool>());
}

// Representative Class F gameplay data opts in with a schema version >= 1.
TEST(SaveTrait, RepresentativeGameplayDataOptsIn) {
    EXPECT_TRUE(shouldSave<C_Velocity3D>());
    EXPECT_GE(saveVersion<C_Velocity3D>(), 1u);

    EXPECT_TRUE(shouldSave<C_LocalTransform>());
    EXPECT_GE(saveVersion<C_LocalTransform>(), 1u);

    EXPECT_TRUE(shouldSave<C_Name>());
    EXPECT_GE(saveVersion<C_Name>(), 1u);

    EXPECT_TRUE(shouldSave<C_ShapeDescriptor>());
    EXPECT_GE(saveVersion<C_ShapeDescriptor>(), 1u);

    EXPECT_TRUE(shouldSave<C_SimClock>());
    EXPECT_GE(saveVersion<C_SimClock>(), 1u);
}

// Class E — the two explicit-call opt-ins and the deprecated shim opt-out.
TEST(SaveTrait, ClassEExplicitCalls) {
    EXPECT_TRUE(shouldSave<C_VoxelSetNew>());
    EXPECT_GE(saveVersion<C_VoxelSetNew>(), 1u);

    EXPECT_TRUE(shouldSave<C_Skeleton>());
    EXPECT_GE(saveVersion<C_Skeleton>(), 1u);

    EXPECT_FALSE(shouldSave<C_JointHierarchy>());
}

template <typename C> constexpr bool foldInvariantHolds() {
    return SaveTrait<C>::kSave ? (SaveTrait<C>::kSaveVersion >= 1)
                               : (SaveTrait<C>::kSaveVersion == 0);
}

template <typename Tuple, std::size_t... I>
constexpr bool allFoldInvariantsHold(std::index_sequence<I...>) {
    return (... && foldInvariantHolds<std::tuple_element_t<I, Tuple>>());
}

// Fold invariants across the entire inventory: shouldSave() implies
// kSaveVersion >= 1, and !shouldSave() implies kSaveVersion == 0. Checked
// at compile time (so a violation is a build break, not just a red test)
// and mirrored here so it also shows up as a normal test result.
static_assert(
    allFoldInvariantsHold<AllEngineComponents>(
        std::make_index_sequence<std::tuple_size_v<AllEngineComponents>>{}
    ),
    "shouldSave() must imply kSaveVersion >= 1, and !shouldSave() must imply kSaveVersion == 0"
);

TEST(SaveTrait, FoldInvariantsHoldAcrossInventory) {
    EXPECT_TRUE((allFoldInvariantsHold<AllEngineComponents>(
        std::make_index_sequence<std::tuple_size_v<AllEngineComponents>>{}
    )));
}

// Completeness backstop — adding a component to the engine without adding
// a matching IR_SAVE_OPT_IN/OPT_OUT + AllEngineComponents entry fails this
// test (the compile-time gate in save_component_inventory.hpp would also
// have caught a missing *decision*; this catches a missing *tuple entry*
// for a component that does have one, e.g. a copy-paste line drop).
TEST(SaveTrait, InventoryIsComplete) {
    EXPECT_EQ(std::tuple_size_v<AllEngineComponents>, kExpectedEngineComponentCount);
}

} // namespace
