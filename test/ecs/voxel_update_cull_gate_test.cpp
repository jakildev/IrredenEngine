// The pose writers' cull gate (`IRPrefab::Voxel::isVoxelSetCullVisible`),
// driven frame by frame: one UPDATE_VOXEL_SET_CHILDREN tick (and, for GRID
// sets, one REBUILD_GRID_VOXELS tick) against a fixed gate viewport, then the
// render-side `rebuildChunkBounds` at a cardinal pose or under continuous yaw.
// The destination-bound suite checks that the bound the gate re-admits a set
// with contains every global either writer produces.
//
// Headless: the pool lives on a bare canvas entity, each set targets it
// explicitly, and the test sets the systems' gate members directly instead of
// reading the render cull viewport.

#include <gtest/gtest.h>

#include <irreden/ir_entity.hpp>
#include <irreden/ir_job.hpp>
#include <irreden/ir_math.hpp>
#include <irreden/ir_system.hpp>
#include <irreden/voxel/set_cull_gate.hpp>
#include <irreden/voxel/systems/system_rebuild_grid_voxels.hpp>
#include <irreden/voxel/systems/system_update_voxel_set_children.hpp>

#include <cstddef>
#include <vector>

namespace {

using IRComponents::C_RotationMode;
using IRComponents::C_Voxel;
using IRComponents::C_VoxelPool;
using IRComponents::C_VoxelSetNew;
using IRComponents::C_WorldTransform;
using IRComponents::EntityAnchor;
using IRMath::CardinalIndex;
using IRMath::IsoBounds2D;
using IRMath::ivec3;
using IRMath::vec2;
using IRMath::vec3;
using Rebuild = IRSystem::System<IRSystem::REBUILD_GRID_VOXELS>;
using Update = IRSystem::System<IRSystem::UPDATE_VOXEL_SET_CHILDREN>;

constexpr int kChunk = IRRender::kVoxelChunkSize;
constexpr int kPlateSide = 16;
static_assert(kPlateSide * kPlateSide == kChunk, "one plate fills exactly one pool chunk");
constexpr float kYaw = 0.6f;

struct Pose {
    bool yawed_ = false;
    float yaw_ = 0.0f;
};

constexpr Pose kCardinal{false, 0.0f};
constexpr Pose kYawed{true, kYaw};

// The gate viewport over a world box, margined so the yaw branch's half-cell
// shift and a plate's extent stay inside it.
IsoBounds2D viewportOver(vec3 worldMin, vec3 worldMax, const Pose &pose) {
    const IsoBounds2D iso = IRMath::isoAABBOfWorldAABBUnderYaw(worldMin, worldMax, pose.yaw_);
    return IsoBounds2D{iso.min_ - vec2(4.0f), iso.max_ + vec2(4.0f)};
}

const vec3 kInside(0.0f, 0.0f, 0.0f);
const vec3 kInsideFar(20.0f, -10.0f, 0.0f);
const vec3 kOutside(600.0f, -600.0f, 0.0f);
const vec3 kOutsideFar(-600.0f, 600.0f, 0.0f);

IsoBounds2D gateViewport(const Pose &pose) {
    return viewportOver(
        IRMath::min(kInside, kInsideFar),
        IRMath::max(kInside, kInsideFar) + vec3(kPlateSide, kPlateSide, 1),
        pose
    );
}

IRMath::vec4 quatAboutAxis(vec3 axis, float radians) {
    const vec3 unit = IRMath::normalize(axis);
    return IRMath::vec4(unit * IRMath::sin(radians * 0.5f), IRMath::cos(radians * 0.5f));
}

const IRMath::vec4 kGridRotation = quatAboutAxis(vec3(0.3f, 0.5f, 1.0f), 0.7f);
const vec3 kGridInside(10.0f, 6.0f, 2.0f);

bool contains(const IRComponents::ChunkWorldBounds &bounds, vec3 cell) {
    constexpr float kEps = 1e-4f;
    for (int axis = 0; axis < 3; ++axis) {
        if (cell[axis] < bounds.worldMin_[axis] - kEps ||
            cell[axis] > bounds.worldMax_[axis] + kEps) {
            return false;
        }
    }
    return true;
}

class VoxelUpdateCullGateTest : public ::testing::Test {
  protected:
    IREntity::EntityManager m_entities;
    Update m_update;
    Rebuild m_rebuild;
    IREntity::EntityId m_canvas = IREntity::createEntity(C_VoxelPool{ivec3(16, 16, 16)});
    std::vector<IREntity::EntityId> m_sets;
    std::vector<vec3> m_translations;
    std::vector<IRMath::vec4> m_rotations;
    std::vector<bool> m_grid;

    C_VoxelPool &pool() {
        return IREntity::getComponent<C_VoxelPool>(m_canvas);
    }

    C_VoxelSetNew &set(std::size_t index) {
        return IREntity::getComponent<C_VoxelSetNew>(m_sets[index]);
    }

    std::size_t makeSet(vec3 translation) {
        const IREntity::EntityId entity = IREntity::createEntity(
            C_VoxelSetNew{
                ivec3(kPlateSide, kPlateSide, 1),
                IRMath::Color{200, 120, 60, 255},
                EntityAnchor::CORNER,
                m_canvas
            }
        );
        const std::size_t index = track(entity, translation);
        const C_VoxelSetNew &created = set(index);
        EXPECT_EQ(created.voxelStartIdx_ % kChunk, 0u);
        EXPECT_EQ(created.allocatedVoxels_, kChunk);
        return index;
    }

    std::size_t track(
        IREntity::EntityId entity,
        vec3 translation,
        IRMath::vec4 rotation = IRMath::vec4(0, 0, 0, 1),
        bool grid = false
    ) {
        m_sets.push_back(entity);
        m_translations.push_back(translation);
        m_rotations.push_back(rotation);
        m_grid.push_back(grid);
        return m_sets.size() - 1;
    }

    // An 8x8x4 CENTER block: one pool chunk at identity, more once rotated.
    std::size_t makeGridSet(vec3 translation, IRMath::vec4 rotation) {
        const IREntity::EntityId entity = IREntity::createEntity(
            C_VoxelSetNew{
                ivec3(8, 8, 4),
                IRMath::Color{90, 160, 220, 255},
                EntityAnchor::CENTER,
                m_canvas
            }
        );
        return track(entity, translation, rotation, true);
    }

    // Live globals of the set's whole allocation, the cells a GRID pose emits.
    std::vector<vec3> liveGlobals(std::size_t index) {
        const C_VoxelSetNew &s = set(index);
        std::vector<vec3> cells;
        for (int i = 0; i < s.allocatedVoxels_; ++i) {
            const std::size_t slot = s.voxelStartIdx_ + static_cast<std::size_t>(i);
            if (pool().getColors()[slot].color_.alpha_ != 0) {
                cells.push_back(pool().getPositionGlobals()[slot].pos_);
            }
        }
        return cells;
    }

    // True when a GRID set's live cells all sit in the destination bound of
    // its current transform, which a write at any other translation misses.
    bool isRasterizedAt(std::size_t index) {
        const IRComponents::ChunkWorldBounds bounds =
            IRPrefab::Voxel::destinationBounds(set(index), pool(), transformOf(index));
        const std::vector<vec3> cells = liveGlobals(index);
        if (cells.empty()) {
            return false;
        }
        for (const vec3 cell : cells) {
            if (!contains(bounds, cell)) {
                return false;
            }
        }
        return true;
    }

    void configureUpdate(bool cullValid, const IsoBounds2D &viewport) {
        m_update.canvasToPool_.clear();
        m_update.canvasToPool_[m_canvas] = &pool();
        m_update.activeCanvas_ = IREntity::kNullEntity;
        m_update.activePool_ = nullptr;
        m_update.pendingByWorker_.assign(static_cast<std::size_t>(IRJob::workerCount()) + 1u, {});
        m_update.cullValid_ = cullValid;
        m_update.viewport_ = viewport;
        m_rebuild.lastCanvas_ = IREntity::kNullEntity;
        m_rebuild.lastPool_ = nullptr;
        m_rebuild.cullValid_ = cullValid;
        m_rebuild.chunkVp_ = viewport;
    }

    C_WorldTransform transformOf(std::size_t index) const {
        return C_WorldTransform{m_translations[index], m_rotations[index], vec3(1)};
    }

    // One UPDATE tick: the child-position baseline for every set, then the
    // GRID re-rasterization for the GRID sets, in pipeline order.
    void update(const IsoBounds2D &viewport) {
        configureUpdate(true, viewport);
        for (std::size_t i = 0; i < m_sets.size(); ++i) {
            IREntity::EntityId entity = m_sets[i];
            m_update.tick(entity, set(i), transformOf(i));
        }
        m_update.endTick();
        for (std::size_t i = 0; i < m_sets.size(); ++i) {
            if (m_grid[i]) {
                m_rebuild.tick(set(i), transformOf(i), C_RotationMode{});
            }
        }
    }

    void render(const Pose &pose) {
        pool().rebuildChunkBounds(CardinalIndex::k0, pose.yawed_, pose.yaw_);
    }

    void frame(const Pose &pose) {
        update(gateViewport(pose));
        render(pose);
    }

    // True when the set's pool globals are its locals plus @p translation.
    bool isAt(std::size_t index, vec3 translation) {
        const C_VoxelSetNew &s = set(index);
        for (int i = 0; i < s.numVoxels_; ++i) {
            const std::size_t slot = s.voxelStartIdx_ + static_cast<std::size_t>(i);
            const vec3 want =
                pool().getPositions()[slot].pos_ + pool().getPositionOffsets()[slot] + translation;
            if (pool().getPositionGlobals()[slot].pos_ != want) {
                return false;
            }
        }
        return true;
    }

    // A set that leaves the gate viewport by its own transform must be
    // written again once its transform brings it back.
    void expectLeaveAndReturnIsWritten(const Pose &pose) {
        const std::size_t mover = makeSet(kInside);
        frame(pose);
        frame(pose);
        ASSERT_TRUE(isAt(mover, kInside));

        m_translations[mover] = kOutside;
        frame(pose);
        ASSERT_TRUE(isAt(mover, kOutside)) << "the update that leaves the viewport is written";
        frame(pose);

        m_translations[mover] = kInside;
        update(gateViewport(pose));
        EXPECT_TRUE(isAt(mover, kInside))
            << "a set whose transform returns into the viewport must be written";
        render(pose);
        frame(pose);
        EXPECT_TRUE(isAt(mover, kInside));
    }

    // The GRID twin: a rotated set that leaves the viewport and returns is
    // re-rasterized at its returned pose.
    void expectGridLeaveAndReturnIsRerasterized(const Pose &pose) {
        const std::size_t mover = makeGridSet(kGridInside, kGridRotation);
        frame(pose);
        frame(pose);
        ASSERT_TRUE(isRasterizedAt(mover));

        m_translations[mover] = kOutside;
        frame(pose);
        ASSERT_TRUE(isRasterizedAt(mover)) << "the pose that leaves the viewport is rasterized";
        frame(pose);

        m_translations[mover] = kGridInside;
        update(gateViewport(pose));
        EXPECT_TRUE(isRasterizedAt(mover))
            << "a GRID set whose transform returns into the viewport must be re-rasterized";
    }

    void expectInsideMoverTracksItsTransform(const Pose &pose) {
        const std::size_t mover = makeSet(kInside);
        for (int step = 0; step < 12; ++step) {
            m_translations[mover] = kInside + vec3(static_cast<float>(step), 0.0f, 0.0f);
            update(gateViewport(pose));
            EXPECT_TRUE(isAt(mover, m_translations[mover])) << "step " << step;
            render(pose);
        }
    }
};

TEST_F(VoxelUpdateCullGateTest, LeaveAndReturnAtCardinalPoseIsWritten) {
    expectLeaveAndReturnIsWritten(kCardinal);
}

TEST_F(VoxelUpdateCullGateTest, LeaveAndReturnUnderYawIsWritten) {
    expectLeaveAndReturnIsWritten(kYawed);
}

TEST_F(VoxelUpdateCullGateTest, GridLeaveAndReturnAtCardinalPoseIsRerasterized) {
    expectGridLeaveAndReturnIsRerasterized(kCardinal);
}

TEST_F(VoxelUpdateCullGateTest, GridLeaveAndReturnUnderYawIsRerasterized) {
    expectGridLeaveAndReturnIsRerasterized(kYawed);
}

TEST_F(VoxelUpdateCullGateTest, InsideMoverTracksItsTransformAtCardinalPose) {
    expectInsideMoverTracksItsTransform(kCardinal);
}

TEST_F(VoxelUpdateCullGateTest, InsideMoverTracksItsTransformUnderYaw) {
    expectInsideMoverTracksItsTransform(kYawed);
}

TEST_F(VoxelUpdateCullGateTest, YawUpdateWritesOnlyTheSetsInsideTheViewport) {
    for (const vec3 at : {kInside, kOutside, kInsideFar, kOutsideFar}) {
        makeSet(at);
    }
    frame(kYawed);
    frame(kYawed);

    const vec3 nudge(1.0f, 0.0f, 0.0f);
    for (vec3 &translation : m_translations) {
        translation += nudge;
    }
    update(gateViewport(kYawed));

    int written = 0;
    for (std::size_t i = 0; i < m_sets.size(); ++i) {
        written += isAt(i, m_translations[i]) ? 1 : 0;
    }
    EXPECT_EQ(written, 2);
    EXPECT_TRUE(isAt(0, m_translations[0]));
    EXPECT_TRUE(isAt(2, m_translations[2]));
    EXPECT_FALSE(isAt(1, m_translations[1]));
    EXPECT_FALSE(isAt(3, m_translations[3]));
}

// `destinationBounds` is O(1) from the authored box, so it must contain every
// global the two pose writers produce for a set: the child-position baseline
// and each GRID arm (identity, inverse resample, zero-scale forward map).
class VoxelSetDestinationBoundsTest : public ::testing::Test {
  protected:
    IREntity::EntityManager m_entities;
    Update m_update;
    Rebuild m_rebuild;
    IREntity::EntityId m_canvas = IREntity::createEntity(C_VoxelPool{ivec3(32, 32, 32)});

    C_VoxelPool &pool() {
        return IREntity::getComponent<C_VoxelPool>(m_canvas);
    }

    IREntity::EntityId makeSized(EntityAnchor anchor) {
        return IREntity::createEntity(
            C_VoxelSetNew{ivec3(5, 4, 3), IRMath::Color{200, 120, 60, 255}, anchor, m_canvas}
        );
    }

    IREntity::EntityId makeDense() {
        const ivec3 boundsMin(3, -2, 1);
        const ivec3 size(4, 3, 2);
        std::vector<C_Voxel> voxels(static_cast<std::size_t>(size.x * size.y * size.z));
        for (C_Voxel &voxel : voxels) {
            voxel.color_ = IRMath::Color{60, 200, 90, 255};
        }
        return IREntity::createEntity(
            C_VoxelSetNew{boundsMin, boundsMin + size, voxels, EntityAnchor::CORNER, m_canvas}
        );
    }

    void expectWritesContained(IREntity::EntityId entity, const C_WorldTransform &transform) {
        C_VoxelSetNew &voxelSet = IREntity::getComponent<C_VoxelSetNew>(entity);
        ASSERT_GT(voxelSet.numVoxels_, 0);
        const IRComponents::ChunkWorldBounds bounds =
            IRPrefab::Voxel::destinationBounds(voxelSet, pool(), transform);

        m_update.canvasToPool_.clear();
        m_update.canvasToPool_[m_canvas] = &pool();
        m_update.pendingByWorker_.assign(static_cast<std::size_t>(IRJob::workerCount()) + 1u, {});
        m_update.cullValid_ = false;
        m_update.tick(entity, voxelSet, transform);
        m_update.endTick();
        for (int i = 0; i < voxelSet.numVoxels_; ++i) {
            const vec3 cell = pool().getPositionGlobals()[voxelSet.voxelStartIdx_ + i].pos_;
            EXPECT_TRUE(contains(bounds, cell)) << "child-position slot " << i;
        }

        m_rebuild.lastPool_ = nullptr;
        m_rebuild.cullValid_ = false;
        m_rebuild.tick(voxelSet, transform, C_RotationMode{});
        ASSERT_EQ(voxelSet.unadmittedDestinationCells_, 0);
        const IRComponents::ChunkWorldBounds regrown =
            IRPrefab::Voxel::destinationBounds(voxelSet, pool(), transform);
        EXPECT_EQ(regrown.worldMin_, bounds.worldMin_) << "growth keeps the authored origin";
        EXPECT_EQ(regrown.worldMax_, bounds.worldMax_);
        int live = 0;
        for (int i = 0; i < voxelSet.allocatedVoxels_; ++i) {
            const std::size_t slot = voxelSet.voxelStartIdx_ + static_cast<std::size_t>(i);
            if (pool().getColors()[slot].color_.alpha_ == 0) {
                continue;
            }
            ++live;
            EXPECT_TRUE(contains(bounds, pool().getPositionGlobals()[slot].pos_))
                << "GRID slot " << i;
        }
        EXPECT_GT(live, 0);
    }

    void expectEveryShapeContained(const C_WorldTransform &transform) {
        for (const EntityAnchor anchor :
             {EntityAnchor::CORNER, EntityAnchor::CENTER, EntityAnchor::GROUND}) {
            SCOPED_TRACE(::testing::Message() << "size ctor, anchor " << static_cast<int>(anchor));
            expectWritesContained(makeSized(anchor), transform);
        }
        SCOPED_TRACE("dense ctor, boundsMin (3,-2,1)");
        expectWritesContained(makeDense(), transform);
    }
};

TEST_F(VoxelSetDestinationBoundsTest, IdentityPoseWritesAreContained) {
    expectEveryShapeContained(
        C_WorldTransform{vec3(10.0f, -7.0f, 3.0f), IRMath::vec4(0, 0, 0, 1), vec3(1)}
    );
}

TEST_F(VoxelSetDestinationBoundsTest, RotatedScaledPoseWritesAreContained) {
    for (const vec3 scale : {vec3(1.7f, 0.6f, 1.2f), vec3(0.4f), vec3(2.5f)}) {
        SCOPED_TRACE(
            ::testing::Message() << "scale " << scale.x << "," << scale.y << "," << scale.z
        );
        expectEveryShapeContained(C_WorldTransform{vec3(-5.0f, 12.0f, 4.0f), kGridRotation, scale});
    }
}

TEST_F(VoxelSetDestinationBoundsTest, ZeroScaleForwardMapWritesAreContained) {
    expectEveryShapeContained(
        C_WorldTransform{vec3(-5.0f, 12.0f, 4.0f), kGridRotation, vec3(1.3f, 0.0f, 1.0f)}
    );
}

} // namespace
