// A component's heap storage keeps its address across structural changes:
// an archetype move and the swap-and-pop that fills a removed row both move
// the row rather than copy it. `C_VoxelSetNew` relies on it — its spans alias
// its canvas's `C_VoxelPool` vectors, and a canvas changes archetype whenever a
// component is added to it after its sets have seeded.

#include <gtest/gtest.h>

#include <irreden/ir_entity.hpp>
#include <irreden/ir_math.hpp>

#include <irreden/voxel/components/component_voxel_pool.hpp>
#include <irreden/voxel/components/component_voxel_set.hpp>

#include <cstddef>
#include <utility>
#include <vector>

namespace {

using IRComponents::C_VoxelPool;
using IRComponents::C_VoxelSetNew;
using IRMath::Color;
using IRMath::ivec3;

struct C_HeapStorageProbe {
    std::vector<int> values_;
};

struct C_ArchetypeMoveTag {
    int value_ = 0;
};

constexpr std::size_t kProbeValues = 4096;

class ArchetypeMoveStorage : public ::testing::Test {
  protected:
    static IREntity::EntityId makeProbe() {
        return IREntity::createEntity(C_HeapStorageProbe{std::vector<int>(kProbeValues, 7)});
    }

    static const int *storageOf(IREntity::EntityId entity) {
        return IREntity::getComponent<C_HeapStorageProbe>(entity).values_.data();
    }

    IREntity::EntityManager m_entityManager;
};

TEST_F(ArchetypeMoveStorage, AddingAComponentKeepsStorageAddress) {
    const IREntity::EntityId entity = makeProbe();
    const int *before = storageOf(entity);

    IREntity::setComponent(entity, C_ArchetypeMoveTag{1});

    EXPECT_EQ(storageOf(entity), before);
    EXPECT_EQ(IREntity::getComponent<C_HeapStorageProbe>(entity).values_.size(), kProbeValues);
}

TEST_F(ArchetypeMoveStorage, RemovingAComponentKeepsStorageAddress) {
    const IREntity::EntityId entity = makeProbe();
    IREntity::setComponent(entity, C_ArchetypeMoveTag{1});
    const int *before = storageOf(entity);

    IREntity::removeComponent<C_ArchetypeMoveTag>(entity);

    EXPECT_EQ(storageOf(entity), before);
}

TEST_F(ArchetypeMoveStorage, RowPackedIntoAHoleKeepsStorageAddress) {
    const IREntity::EntityId removed = makeProbe();
    const IREntity::EntityId packed = makeProbe();
    const int *before = storageOf(packed);

    IREntity::destroyEntity(removed);
    IREntity::getEntityManager().destroyMarkedEntities();

    EXPECT_EQ(storageOf(packed), before);
    EXPECT_EQ(IREntity::getComponent<C_HeapStorageProbe>(packed).values_.size(), kProbeValues);
}

// Two canvases join the same archetype one after the other, so the second
// move grows the destination column and relocates the first canvas's pool.
TEST_F(ArchetypeMoveStorage, ResidentSetSpansFollowTheirCanvasThroughArchetypeMoves) {
    const ivec3 poolSize{8, 8, 8};
    const IREntity::EntityId canvasA = IREntity::createEntity(C_VoxelPool{poolSize});
    const IREntity::EntityId canvasB = IREntity::createEntity(C_VoxelPool{poolSize});
    const IREntity::EntityId setA =
        IREntity::createEntity(C_VoxelSetNew{ivec3(4), Color{200, 100, 50, 255}, false, canvasA});
    const IREntity::EntityId setB =
        IREntity::createEntity(C_VoxelSetNew{ivec3(2), Color{10, 20, 30, 255}, false, canvasB});

    IREntity::setComponent(canvasA, C_ArchetypeMoveTag{1});
    IREntity::setComponent(canvasB, C_ArchetypeMoveTag{2});

    for (const auto [canvas, object] : {std::pair{canvasA, setA}, std::pair{canvasB, setB}}) {
        auto &pool = IREntity::getComponent<C_VoxelPool>(canvas);
        const auto &set = IREntity::getComponent<C_VoxelSetNew>(object);
        ASSERT_GT(set.numVoxels_, 0);
        const std::size_t start = set.voxelStartIdx_;
        EXPECT_EQ(set.voxels_.data(), pool.getColors().data() + start);
        EXPECT_EQ(set.positions_.data(), pool.getPositions().data() + start);
        EXPECT_EQ(set.globalPositions_.data(), pool.getPositionGlobals().data() + start);
        EXPECT_EQ(set.authoredRecords().data(), pool.getColors().data() + start);
    }
    EXPECT_EQ(
        IREntity::getComponent<C_VoxelSetNew>(setA).authoredRecords()[0].color_.toPackedRGBA(),
        (Color{200, 100, 50, 255}).toPackedRGBA()
    );
}

} // namespace
