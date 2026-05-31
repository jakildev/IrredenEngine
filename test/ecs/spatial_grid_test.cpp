#include <gtest/gtest.h>

#include <irreden/ir_entity.hpp>
#include <irreden/ir_math.hpp>
#include <irreden/ir_system.hpp>
#include <irreden/ir_time.hpp>

#include <irreden/common/components/component_world_transform.hpp>
#include <irreden/spatial/components/component_spatial_index.hpp>
#include <irreden/spatial/spatial_grid.hpp>
#include <irreden/spatial/spatial_query.hpp>
#include <irreden/spatial/systems/system_build_spatial_index.hpp>

#include <algorithm>
#include <vector>

namespace {

using IRPrefab::Spatial::SpatialGrid;
using IRPrefab::Spatial::SpatialHit;

bool containsId(const std::vector<SpatialHit> &hits, IREntity::EntityId id) {
    return std::any_of(hits.begin(), hits.end(), [id](const SpatialHit &h) { return h.id_ == id; });
}

const SpatialHit *findId(const std::vector<SpatialHit> &hits, IREntity::EntityId id) {
    for (const SpatialHit &h : hits) {
        if (h.id_ == id) {
            return &h;
        }
    }
    return nullptr;
}

// ---- Pure SpatialGrid (no ECS) ------------------------------------------

TEST(SpatialGridTest, QueryRadiusReturnsOnlyInRange) {
    SpatialGrid grid{64.0f};
    grid.insert(1, IRMath::vec3(0.0f, 0.0f, 0.0f));
    grid.insert(2, IRMath::vec3(10.0f, 0.0f, 0.0f));   // inside r=20
    grid.insert(3, IRMath::vec3(100.0f, 0.0f, 0.0f));  // outside r=20

    std::vector<SpatialHit> hits;
    grid.queryRadius(IRMath::vec3(0.0f), 20.0f, hits);

    EXPECT_EQ(hits.size(), 2u);
    EXPECT_TRUE(containsId(hits, 1));
    EXPECT_TRUE(containsId(hits, 2));
    EXPECT_FALSE(containsId(hits, 3));
}

TEST(SpatialGridTest, QueryRadiusBoundaryIsInclusive) {
    SpatialGrid grid{64.0f};
    grid.insert(1, IRMath::vec3(5.0f, 0.0f, 0.0f));   // exactly at radius
    grid.insert(2, IRMath::vec3(5.001f, 0.0f, 0.0f)); // just past radius

    std::vector<SpatialHit> hits;
    grid.queryRadius(IRMath::vec3(0.0f), 5.0f, hits);

    EXPECT_TRUE(containsId(hits, 1)) << "distance == radius must be included";
    EXPECT_FALSE(containsId(hits, 2));
}

TEST(SpatialGridTest, QueryReturnsInlinePosition) {
    SpatialGrid grid{64.0f};
    const IRMath::vec3 expected(3.0f, -7.0f, 11.0f);
    grid.insert(42, expected);

    std::vector<SpatialHit> hits;
    grid.queryRadius(IRMath::vec3(0.0f), 100.0f, hits);

    const SpatialHit *hit = findId(hits, 42);
    ASSERT_NE(hit, nullptr);
    EXPECT_EQ(hit->pos_, expected) << "position must be returned inline, no foreign read needed";
}

TEST(SpatialGridTest, QueryAabbIsInclusive) {
    SpatialGrid grid{16.0f};
    grid.insert(1, IRMath::vec3(0.0f, 0.0f, 0.0f));
    grid.insert(2, IRMath::vec3(10.0f, 10.0f, 10.0f)); // on the boundary
    grid.insert(3, IRMath::vec3(11.0f, 0.0f, 0.0f));   // x outside

    std::vector<SpatialHit> hits;
    grid.queryAabb(IRMath::vec3(0.0f), IRMath::vec3(10.0f), hits);

    EXPECT_TRUE(containsId(hits, 1));
    EXPECT_TRUE(containsId(hits, 2));
    EXPECT_FALSE(containsId(hits, 3));
}

TEST(SpatialGridTest, NegativeCoordinatesAndMultiCellSpan) {
    // cellSize 8 → these three land in three different cells, two of them
    // negative; the query sphere spans all three.
    SpatialGrid grid{8.0f};
    grid.insert(1, IRMath::vec3(-20.0f, 0.0f, 0.0f));
    grid.insert(2, IRMath::vec3(0.0f, 0.0f, 0.0f));
    grid.insert(3, IRMath::vec3(20.0f, 0.0f, 0.0f));

    std::vector<SpatialHit> hits;
    grid.queryRadius(IRMath::vec3(0.0f), 25.0f, hits);
    EXPECT_EQ(hits.size(), 3u);
}

TEST(SpatialGridTest, ClearDropsStaleEntriesAcrossFrames) {
    SpatialGrid grid{64.0f};
    std::vector<SpatialHit> hits;

    grid.insert(1, IRMath::vec3(0.0f));
    grid.queryRadius(IRMath::vec3(0.0f), 10.0f, hits);
    EXPECT_EQ(hits.size(), 1u);

    // Next "frame": clear, insert a different entity. The stale entity 1
    // must NOT survive into this frame's query.
    grid.clear();
    grid.insert(2, IRMath::vec3(0.0f));
    grid.queryRadius(IRMath::vec3(0.0f), 10.0f, hits);
    EXPECT_EQ(hits.size(), 1u);
    EXPECT_TRUE(containsId(hits, 2));
    EXPECT_FALSE(containsId(hits, 1)) << "clear() must drop last frame's entries";
}

TEST(SpatialGridTest, OutVectorCapacityReusedAfterWarmup) {
    SpatialGrid grid{64.0f};
    for (IREntity::EntityId id = 1; id <= 8; ++id) {
        grid.insert(id, IRMath::vec3(static_cast<float>(id), 0.0f, 0.0f));
    }

    std::vector<SpatialHit> hits;
    grid.queryRadius(IRMath::vec3(0.0f), 100.0f, hits); // warm up out capacity
    const std::size_t warmCapacity = hits.capacity();
    ASSERT_GE(warmCapacity, 8u);

    // A second identical query into the same vector must not reallocate —
    // queryRadius clears (keeping capacity) and re-fills.
    grid.queryRadius(IRMath::vec3(0.0f), 100.0f, hits);
    EXPECT_EQ(hits.size(), 8u);
    EXPECT_EQ(hits.capacity(), warmCapacity) << "reused out vector must not reallocate";
}

TEST(SpatialGridTest, ZeroOrNegativeRadiusReturnsEmpty) {
    SpatialGrid grid{64.0f};
    grid.insert(1, IRMath::vec3(0.0f));

    std::vector<SpatialHit> hits;
    grid.queryRadius(IRMath::vec3(0.0f), 0.0f, hits);
    EXPECT_TRUE(hits.empty());
    grid.queryRadius(IRMath::vec3(0.0f), -5.0f, hits);
    EXPECT_TRUE(hits.empty());
}

// ---- BUILD_SPATIAL_INDEX system + IRPrefab::Spatial query surface --------

class SpatialIndexSystemTest : public testing::Test {
  protected:
    SpatialIndexSystemTest()
        : m_entity_manager{}
        , m_system_manager{} {}

    IREntity::EntityId makeQueryable(IRMath::vec3 pos) {
        return IREntity::createEntity(
            IRComponents::C_WorldTransform(pos, IRMath::vec4(0.0f, 0.0f, 0.0f, 1.0f), IRMath::vec3(1.0f)),
            IRComponents::C_SpatialQueryable{}
        );
    }

    IREntity::EntityId makeUnqueryable(IRMath::vec3 pos) {
        return IREntity::createEntity(
            IRComponents::C_WorldTransform(pos, IRMath::vec4(0.0f, 0.0f, 0.0f, 1.0f), IRMath::vec3(1.0f))
        );
    }

    void runBuild() {
        IRSystem::SystemId sysId = IRSystem::createSystem<IRSystem::BUILD_SPATIAL_INDEX>();
        m_system_manager.registerPipeline(IRTime::Events::UPDATE, {sysId});
        m_system_manager.executePipeline(IRTime::Events::UPDATE);
    }

    IREntity::EntityManager m_entity_manager;
    IRSystem::SystemManager m_system_manager;
};

TEST_F(SpatialIndexSystemTest, IndexesQueryableEntitiesWithInlinePosition) {
    const IRMath::vec3 nearPos(5.0f, 0.0f, 0.0f);
    IREntity::EntityId near = makeQueryable(nearPos);
    IREntity::EntityId far = makeQueryable(IRMath::vec3(500.0f, 0.0f, 0.0f));

    runBuild();

    std::vector<SpatialHit> hits;
    IRPrefab::Spatial::queryRadius(IRMath::vec3(0.0f), 50.0f, hits);

    EXPECT_TRUE(containsId(hits, near));
    EXPECT_FALSE(containsId(hits, far));
    const SpatialHit *hit = findId(hits, near);
    ASSERT_NE(hit, nullptr);
    EXPECT_EQ(hit->pos_, nearPos);
}

TEST_F(SpatialIndexSystemTest, UntaggedEntitiesAreNotIndexed) {
    IREntity::EntityId tagged = makeQueryable(IRMath::vec3(5.0f, 0.0f, 0.0f));
    IREntity::EntityId untagged = makeUnqueryable(IRMath::vec3(6.0f, 0.0f, 0.0f));

    runBuild();

    std::vector<SpatialHit> hits;
    IRPrefab::Spatial::queryRadius(IRMath::vec3(0.0f), 50.0f, hits);

    EXPECT_TRUE(containsId(hits, tagged));
    EXPECT_FALSE(containsId(hits, untagged)) << "entity without C_SpatialQueryable must not be indexed";
}

TEST_F(SpatialIndexSystemTest, RebuildReflectsCurrentFrameOnly) {
    IREntity::EntityId a = makeQueryable(IRMath::vec3(5.0f, 0.0f, 0.0f));
    runBuild();

    std::vector<SpatialHit> hits;
    IRPrefab::Spatial::queryRadius(IRMath::vec3(0.0f), 50.0f, hits);
    EXPECT_TRUE(containsId(hits, a));

    // Move the entity far away and rebuild. The index is cleared and rebuilt
    // each frame, so the stale position must NOT linger: a is gone from the
    // origin query and present at its new location.
    IREntity::getComponent<IRComponents::C_WorldTransform>(a).translation_ =
        IRMath::vec3(500.0f, 0.0f, 0.0f);
    m_system_manager.executePipeline(IRTime::Events::UPDATE);

    IRPrefab::Spatial::queryRadius(IRMath::vec3(0.0f), 50.0f, hits);
    EXPECT_FALSE(containsId(hits, a)) << "stale frame-1 position must not survive the rebuild";
    IRPrefab::Spatial::queryRadius(IRMath::vec3(500.0f, 0.0f, 0.0f), 50.0f, hits);
    EXPECT_TRUE(containsId(hits, a)) << "rebuild must index the current position";
}

TEST_F(SpatialIndexSystemTest, QueryBeforeBuildReturnsEmpty) {
    makeQueryable(IRMath::vec3(5.0f, 0.0f, 0.0f));
    // No build run: the singleton was never created.
    std::vector<SpatialHit> hits;
    hits.push_back(SpatialHit{99, IRMath::vec3(1.0f)}); // pre-seed to prove it clears
    IRPrefab::Spatial::queryRadius(IRMath::vec3(0.0f), 50.0f, hits);
    EXPECT_TRUE(hits.empty()) << "querying an unbuilt index clears out and returns empty";
}

} // namespace
