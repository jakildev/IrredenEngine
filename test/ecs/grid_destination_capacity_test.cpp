// Destination-capacity contract for REBUILD_GRID_VOXELS: a rotated GRID pose
// emits every exposed destination cell or fails closed, never a truncated
// surface. Every expectation is graded against an independent double-precision
// oracle that knows only the authored box, the carve, and an explicit rotation
// matrix — no production quaternion, AABB walk, rounding helper, or scratch.
//
// Headless: `C_VoxelPool` lives on a bare canvas entity and each set targets it
// explicitly, so no RenderManager is needed. Without a render viewport the
// rebuild's cull gate is open; the recovery case drives the gate explicitly.

#include <gtest/gtest.h>

#include <irreden/ir_entity.hpp>
#include <irreden/ir_math.hpp>
#include <irreden/ir_system.hpp>
#include <irreden/voxel/systems/system_rebuild_grid_voxels.hpp>
#include <irreden/voxel/voxel_pool_teardown.hpp>

#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace {

using IRComponents::C_RotationMode;
using IRComponents::C_Voxel;
using IRComponents::C_VoxelPool;
using IRComponents::C_VoxelSetNew;
using IRComponents::C_WorldTransform;
using IRComponents::EntityAnchor;
using IRMath::ivec3;
using IRMath::vec3;
using IRSystem::REBUILD_GRID_VOXELS;
namespace Face = IRComponents::VoxelFlags;

using Cell = std::array<int, 3>;
using Cells = std::set<Cell>;
using Records = std::map<Cell, std::uint8_t>;
using Rebuild = IRSystem::System<REBUILD_GRID_VOXELS>;

constexpr double kTwoPi = 6.28318530717958647692;

enum class Axis { Y, Z };

struct Pose {
    Axis axis_ = Axis::Y;
    double degrees_ = 0.0;
    double scale_ = 1.0;
    Cell translation_{0, 0, 0};
};

// Keeps (local index) -> active for a carve; null keeps the whole box.
using Carve = std::function<bool(ivec3)>;

struct Shape {
    std::string name_;
    ivec3 size_;
    Carve keep_;
    vec3 offset_{0.0f};
};

C_WorldTransform productionTransform(const Pose &pose) {
    C_WorldTransform transform;
    const float half = static_cast<float>(pose.degrees_ * kTwoPi / 720.0);
    transform.rotation_ = pose.axis_ == Axis::Y
                              ? IRMath::vec4(0.0f, IRMath::sin(half), 0.0f, IRMath::cos(half))
                              : IRMath::vec4(0.0f, 0.0f, IRMath::sin(half), IRMath::cos(half));
    transform.scale_ = vec3(static_cast<float>(pose.scale_));
    transform.translation_ = vec3(pose.translation_[0], pose.translation_[1], pose.translation_[2]);
    return transform;
}

// Authored source cells: CENTER places index i at i - (size - 1) / 2, plus the
// per-voxel offset; a cell is the round-half-up of that position.
Cells sourceCells(const Shape &shape) {
    Cells cells;
    for (int z = 0; z < shape.size_.z; ++z) {
        for (int y = 0; y < shape.size_.y; ++y) {
            for (int x = 0; x < shape.size_.x; ++x) {
                if (shape.keep_ && !shape.keep_(ivec3(x, y, z))) {
                    continue;
                }
                const std::array<double, 3> local{
                    x - (shape.size_.x - 1) * 0.5 + shape.offset_.x,
                    y - (shape.size_.y - 1) * 0.5 + shape.offset_.y,
                    z - (shape.size_.z - 1) * 0.5 + shape.offset_.z,
                };
                cells.insert(
                    {static_cast<int>(IRMath::floor(local[0] + 0.5)),
                     static_cast<int>(IRMath::floor(local[1] + 0.5)),
                     static_cast<int>(IRMath::floor(local[2] + 0.5))}
                );
            }
        }
    }
    return cells;
}

struct Oracle {
    Cells covered_;
    // Smallest distance of any inverse-mapped coordinate from a rounding
    // boundary. Float and double disagree only near one, so a case whose
    // margin is tiny would grade float noise, not the contract.
    double margin_ = 1.0;
};

// Destination cells whose inverse image rounds into an authored source cell:
// world = t + R(s * local), so local = R^T (world - t) / s.
Oracle destinationOracle(const Cells &source, const Pose &pose) {
    Oracle oracle;
    const double angle = pose.degrees_ * kTwoPi / 360.0;
    // IRMath's trig is float-only; the oracle needs double precision.
    const double c = std::cos(angle);
    const double s = std::sin(angle);
    int reach = 0;
    Cell lo{1 << 20, 1 << 20, 1 << 20};
    Cell hi{-(1 << 20), -(1 << 20), -(1 << 20)};
    for (const Cell &cell : source) {
        for (int axis = 0; axis < 3; ++axis) {
            reach = IRMath::max(reach, cell[axis] < 0 ? -cell[axis] : cell[axis]);
            lo[axis] = IRMath::min(lo[axis], cell[axis]);
            hi[axis] = IRMath::max(hi[axis], cell[axis]);
        }
    }
    const int radius = static_cast<int>(IRMath::ceil((reach + 1) * pose.scale_ * 1.75)) + 2;
    for (int z = -radius; z <= radius; ++z) {
        for (int y = -radius; y <= radius; ++y) {
            for (int x = -radius; x <= radius; ++x) {
                const double wx = x;
                const double wy = y;
                const double wz = z;
                std::array<double, 3> local{};
                if (pose.axis_ == Axis::Y) {
                    local = {c * wx - s * wz, wy, s * wx + c * wz};
                } else {
                    local = {c * wx + s * wy, -s * wx + c * wy, wz};
                }
                Cell rounded{};
                double margin = 1.0;
                bool nearSource = true;
                for (int axis = 0; axis < 3; ++axis) {
                    const double shifted = local[axis] / pose.scale_ + 0.5;
                    const double nearest = IRMath::floor(shifted + 0.5);
                    margin = IRMath::min(margin, IRMath::abs(shifted - nearest));
                    rounded[axis] = static_cast<int>(IRMath::floor(shifted));
                    nearSource = nearSource && rounded[axis] >= lo[axis] - 1 &&
                                 rounded[axis] <= hi[axis] + 1;
                }
                // Only a cell that rounds into or beside the source box can
                // change classification across a boundary.
                if (nearSource) {
                    oracle.margin_ = IRMath::min(oracle.margin_, margin);
                }
                if (source.contains(rounded)) {
                    oracle.covered_.insert(
                        {x + pose.translation_[0],
                         y + pose.translation_[1],
                         z + pose.translation_[2]}
                    );
                }
            }
        }
    }
    return oracle;
}

std::uint8_t neighborMask(const Cells &cells, const Cell &cell) {
    constexpr std::array<Cell, 6> offsets{
        {{-1, 0, 0}, {1, 0, 0}, {0, -1, 0}, {0, 1, 0}, {0, 0, -1}, {0, 0, 1}}
    };
    constexpr std::array<std::uint8_t, 6> bits{
        Face::kFaceOccludedNegX,
        Face::kFaceOccludedPosX,
        Face::kFaceOccludedNegY,
        Face::kFaceOccludedPosY,
        Face::kFaceOccludedNegZ,
        Face::kFaceOccludedPosZ
    };
    std::uint8_t mask = 0;
    for (std::size_t face = 0; face < offsets.size(); ++face) {
        const Cell neighbor{
            cell[0] + offsets[face][0],
            cell[1] + offsets[face][1],
            cell[2] + offsets[face][2]
        };
        if (cells.contains(neighbor)) {
            mask |= bits[face];
        }
    }
    return mask;
}

std::size_t exposedCount(const Cells &cells) {
    std::size_t exposed = 0;
    for (const Cell &cell : cells) {
        exposed += neighborMask(cells, cell) != Face::kFaceOccludedMask;
    }
    return exposed;
}

bool sameColor(IRMath::Color a, IRMath::Color b) {
    return a.red_ == b.red_ && a.green_ == b.green_ && a.blue_ == b.blue_ && a.alpha_ == b.alpha_;
}

bool activeBit(const C_VoxelPool &pool, std::size_t slot) {
    return (pool.getActiveMask()[slot / IRComponents::kVoxelActiveMaskBits] >>
            (slot % IRComponents::kVoxelActiveMaskBits)) &
           1u;
}

// Emitted destination records across the set's whole allocation. Every live
// slot sits on an integer cell, carries the rotated marker, and mirrors its
// alpha in the pool active mask; a duplicate cell is a failure.
Records emitted(const C_VoxelPool &pool, const C_VoxelSetNew &set) {
    Records records;
    for (int i = 0; i < set.allocatedVoxels_; ++i) {
        const std::size_t slot = set.voxelStartIdx_ + static_cast<std::size_t>(i);
        const C_Voxel &voxel = pool.getColors()[slot];
        EXPECT_EQ(activeBit(pool, slot), voxel.color_.alpha_ != 0) << "slot " << i;
        if (voxel.color_.alpha_ == 0) {
            continue;
        }
        const vec3 position = pool.getPositionGlobals()[slot].pos_;
        const Cell cell{
            static_cast<int>(position.x),
            static_cast<int>(position.y),
            static_cast<int>(position.z)
        };
        EXPECT_EQ(position, vec3(cell[0], cell[1], cell[2]));
        EXPECT_NE(voxel.reserved_ & IRComponents::VoxelReserved::kRotatedEmit, 0u);
        EXPECT_TRUE(records.emplace(cell, voxel.flags_ & Face::kFaceOccludedMask).second)
            << "duplicate destination cell";
    }
    return records;
}

// Grades @p actual against the oracle: nothing outside it, every exposed cell
// present, full-occupancy masks, and interior cells fill the allocation.
void expectMatchesOracle(const Cells &expected, const Records &actual, int capacity) {
    std::size_t missingExposed = 0;
    for (const Cell &cell : expected) {
        if (neighborMask(expected, cell) != Face::kFaceOccludedMask && !actual.contains(cell)) {
            ++missingExposed;
        }
    }
    EXPECT_EQ(missingExposed, 0u);
    for (const auto &[cell, mask] : actual) {
        ASSERT_TRUE(expected.contains(cell)) << testing::PrintToString(cell);
        EXPECT_EQ(mask, neighborMask(expected, cell)) << testing::PrintToString(cell);
    }
    EXPECT_EQ(actual.size(), IRMath::min(expected.size(), static_cast<std::size_t>(capacity)));
}

// Byte image of a slot range: records, local/global positions, offsets, mask
// bits, owner ids and transform indices — everything a neighbor could corrupt.
std::vector<std::uint8_t> spanBytes(const C_VoxelPool &pool, std::size_t start, std::size_t count) {
    std::vector<std::uint8_t> bytes;
    const auto append = [&bytes](const void *data, std::size_t size) {
        const auto *begin = static_cast<const std::uint8_t *>(data);
        bytes.insert(bytes.end(), begin, begin + size);
    };
    for (std::size_t slot = start; slot < start + count; ++slot) {
        append(&pool.getColors()[slot], sizeof(C_Voxel));
        append(&pool.getPositions()[slot], sizeof(IRRender::VoxelGpuPosition));
        append(&pool.getPositionGlobals()[slot], sizeof(IRRender::VoxelGpuPosition));
        append(&pool.getPositionOffsets()[slot], sizeof(vec3));
        append(&pool.getEntityIds()[slot], sizeof(IREntity::EntityId));
        append(&pool.getTransformIndices()[slot], sizeof(std::uint32_t));
        bytes.push_back(activeBit(pool, slot) ? 1u : 0u);
    }
    return bytes;
}

std::vector<std::uint8_t> authoredBytes(const C_VoxelSetNew &set) {
    const auto records = set.authoredRecords();
    std::vector<std::uint8_t> bytes(records.size_bytes());
    std::memcpy(bytes.data(), records.data(), bytes.size());
    return bytes;
}

const Shape kPlate{"plate12x1x12", ivec3(12, 1, 12), nullptr};

class GridDestinationCapacityTest : public testing::Test {
  protected:
    IREntity::EntityManager m_entities;
    Rebuild m_rebuild;

    IREntity::EntityId makeCanvas(int slots) {
        return IREntity::createEntity(C_VoxelPool{ivec3(slots, 1, 1)});
    }

    C_VoxelPool &pool(IREntity::EntityId canvas) {
        return IREntity::getComponent<C_VoxelPool>(canvas);
    }

    IREntity::EntityId makeSet(IREntity::EntityId canvas, const Shape &shape, IRMath::Color color) {
        const auto entity =
            IREntity::createEntity(C_VoxelSetNew{shape.size_, color, EntityAnchor::CENTER, canvas});
        auto &set = IREntity::getComponent<C_VoxelSetNew>(entity);
        EXPECT_EQ(set.numVoxels_, shape.size_.x * shape.size_.y * shape.size_.z);
        if (shape.keep_) {
            const vec3 originShift = (vec3(shape.size_) - vec3(1.0f)) * 0.5f;
            set.editVoxels([&](int, C_Voxel &voxel, vec3 localPos) {
                if (!shape.keep_(IRMath::roundVec3HalfUp(localPos + originShift))) {
                    voxel.deactivate();
                }
            });
        }
        if (shape.offset_ != vec3(0.0f)) {
            for (int i = 0; i < set.numVoxels_; ++i) {
                pool(canvas).getPositionOffsets()[set.voxelStartIdx_ + i] = shape.offset_;
            }
        }
        // The owner stamp UPDATE_VOXEL_SET_CHILDREN applies on a set's first frame.
        set.ownerEntityId_ = entity;
        pool(canvas).setEntityIdForRange(set.voxelStartIdx_, set.allocatedVoxels_, entity);
        return entity;
    }

    C_VoxelSetNew &set(IREntity::EntityId entity) {
        return IREntity::getComponent<C_VoxelSetNew>(entity);
    }

    void rebuild(IREntity::EntityId entity, const Pose &pose) {
        m_rebuild.tick(set(entity), productionTransform(pose), C_RotationMode{});
    }

    void destroyNow(IREntity::EntityId entity) {
        IREntity::destroyEntity(entity);
        m_entities.destroyMarkedEntities();
    }
};

TEST_F(GridDestinationCapacityTest, ExactFitPlateGrowsOnceToRetainEveryExposedCell) {
    const auto canvas = makeCanvas(4096);
    const auto plate = makeSet(canvas, kPlate, IRMath::Color{200, 80, 40, 255});
    const Pose pose{Axis::Y, 45.0};
    const Oracle oracle = destinationOracle(sourceCells(kPlate), pose);
    ASSERT_GT(oracle.margin_, 1e-3);
    ASSERT_EQ(oracle.covered_.size(), 145u);
    ASSERT_EQ(exposedCount(oracle.covered_), 145u);

    rebuild(plate, pose);
    const auto actual = emitted(pool(canvas), set(plate));
    EXPECT_EQ(actual.size(), 145u);
    expectMatchesOracle(oracle.covered_, actual, set(plate).allocatedVoxels_);
    EXPECT_EQ(m_rebuild.capacityStats_.growths_, 1u);
    EXPECT_EQ(m_rebuild.capacityStats_.failures_, 0u);
    EXPECT_EQ(set(plate).numVoxels_, 144);
    EXPECT_GE(set(plate).allocatedVoxels_, 145);
    EXPECT_LE(set(plate).allocatedVoxels_, 216);
    EXPECT_EQ(set(plate).allocatedVoxels_, 216) << "3/2 high-water request";
    EXPECT_EQ(m_rebuild.capacityStats_.peakCapacity_, 216);
    EXPECT_EQ(set(plate).unadmittedDestinationCells_, 0);
}

TEST_F(GridDestinationCapacityTest, DenseSolidNeverGrowsForInteriorCells) {
    const Shape solid{"solid12", ivec3(12), nullptr};
    const auto canvas = makeCanvas(4096);
    const auto entity = makeSet(canvas, solid, IRMath::Color{60, 160, 90, 255});
    for (const Pose &pose : {Pose{Axis::Y, 45.0}, Pose{Axis::Z, -45.0}, Pose{Axis::Y, 30.5}}) {
        SCOPED_TRACE(pose.degrees_);
        const Oracle oracle = destinationOracle(sourceCells(solid), pose);
        ASSERT_GT(oracle.margin_, 1e-3);
        rebuild(entity, pose);
        const auto actual = emitted(pool(canvas), set(entity));
        expectMatchesOracle(oracle.covered_, actual, set(entity).allocatedVoxels_);
    }
    EXPECT_EQ(set(entity).allocatedVoxels_, 1728);
    EXPECT_EQ(m_rebuild.capacityStats_.growths_, 0u);
    EXPECT_EQ(m_rebuild.capacityStats_.peakCapacity_, 1728) << "peak reads a set that never grew";
}

// Shapes x signed Y/Z rotations x scales x translations, each subject placed
// after a guard span so its pool offset is nonzero.
TEST_F(GridDestinationCapacityTest, IndependentOracleMatrixRetainsEveryExposedCell) {
    const std::vector<Shape> shapes{
        kPlate,
        {"plate12x12x1", ivec3(12, 12, 1), nullptr},
        {"solid12", ivec3(12), nullptr},
        {"solid7", ivec3(7), nullptr},
        {"hollow10",
         ivec3(10),
         [](ivec3 p) {
             return p.x == 0 || p.y == 0 || p.z == 0 || p.x == 9 || p.y == 9 || p.z == 9;
         }},
        {"cross12x1x12", ivec3(12, 1, 12), [](ivec3 p) { return p.x == 5 || p.z == 6; }},
        {"checker8", ivec3(8), [](ivec3 p) { return (p.x + p.y + p.z) % 2 == 0; }},
        {"offsetSolid6", ivec3(6), nullptr, vec3(1.0f, 0.0f, -2.0f)},
    };
    const std::vector<Pose> poses{
        {Axis::Y, 45.0},
        {Axis::Y, -45.0},
        {Axis::Y, 37.0, 1.0, {3, -2, 5}},
        {Axis::Y, -61.0, 1.5},
        {Axis::Z, 45.0},
        {Axis::Z, -29.0, 0.75},
        {Axis::Z, 53.0, 1.75, {-4, 1, 0}},
        {Axis::Y, 135.0, 1.25},
    };
    int cases = 0;
    std::uint64_t grownCases = 0;
    for (const Shape &shape : shapes) {
        for (const Pose &pose : poses) {
            SCOPED_TRACE(
                testing::Message() << shape.name_ << " axis=" << (pose.axis_ == Axis::Y ? "Y" : "Z")
                                   << " deg=" << pose.degrees_ << " scale=" << pose.scale_
            );
            const Oracle oracle = destinationOracle(sourceCells(shape), pose);
            if (oracle.margin_ <= 1e-3) {
                continue;
            }
            const auto canvas = makeCanvas(65536);
            const auto guard = makeSet(canvas, {"guard", ivec3(3, 1, 1), nullptr}, {1, 2, 3, 255});
            const auto entity = makeSet(canvas, shape, IRMath::Color{240, 200, 30, 255});
            ASSERT_EQ(set(entity).voxelStartIdx_, 3u);
            const std::uint64_t growthsBefore = m_rebuild.capacityStats_.growths_;
            rebuild(entity, pose);
            ASSERT_EQ(set(entity).unadmittedDestinationCells_, 0);
            expectMatchesOracle(
                oracle.covered_,
                emitted(pool(canvas), set(entity)),
                set(entity).allocatedVoxels_
            );
            const bool needsGrowth =
                exposedCount(oracle.covered_) > static_cast<std::size_t>(set(entity).numVoxels_);
            EXPECT_EQ(m_rebuild.capacityStats_.growths_ - growthsBefore, needsGrowth ? 1u : 0u);
            grownCases += needsGrowth;
            destroyNow(entity);
            destroyNow(guard);
            destroyNow(canvas);
            ++cases;
        }
    }
    EXPECT_GE(cases, 56);
    EXPECT_GT(grownCases, 0u) << "the matrix must exercise growth";
}

TEST_F(GridDestinationCapacityTest, SpinRebuildEditRestoreAndReuseKeepSourceAndNeighbors) {
    const auto canvas = makeCanvas(4096);
    const auto low = makeSet(canvas, {"low", ivec3(7, 1, 1), nullptr}, {9, 9, 9, 255});
    const auto plate = makeSet(canvas, kPlate, IRMath::Color{200, 80, 40, 255});
    const auto high = makeSet(canvas, {"high", ivec3(5, 1, 1), nullptr}, {7, 7, 7, 255});
    const std::size_t plateStart = set(plate).voxelStartIdx_;
    const auto lowBytes = spanBytes(pool(canvas), set(low).voxelStartIdx_, 7);
    const auto highBytes = spanBytes(pool(canvas), set(high).voxelStartIdx_, 5);
    const auto expectNeighborsIntact = [&] {
        EXPECT_EQ(spanBytes(pool(canvas), set(low).voxelStartIdx_, 7), lowBytes);
        EXPECT_EQ(spanBytes(pool(canvas), set(high).voxelStartIdx_, 5), highBytes);
    };

    constexpr std::uint32_t kSlot = 3;
    set(plate).gpuTransformSlot_ = kSlot;
    pool(canvas).setTransformIndexForRange(plateStart, 144, kSlot);
    const auto authored = authoredBytes(set(plate));

    rebuild(plate, {Axis::Y, 45.0});
    ASSERT_EQ(m_rebuild.capacityStats_.growths_, 1u);
    const std::size_t grownStart = set(plate).voxelStartIdx_;
    const int capacity = set(plate).allocatedVoxels_;
    EXPECT_NE(grownStart, plateStart) << "an occupied neighbor forces a migration";
    EXPECT_EQ(authoredBytes(set(plate)), authored);
    expectNeighborsIntact();
    for (int i = 0; i < capacity; ++i) {
        EXPECT_EQ(pool(canvas).getEntityIds()[grownStart + i], plate);
        EXPECT_EQ(pool(canvas).getTransformIndices()[grownStart + i], kSlot);
    }
    for (std::size_t slot = plateStart; slot < plateStart + 144; ++slot) {
        EXPECT_FALSE(activeBit(pool(canvas), slot));
        EXPECT_EQ(pool(canvas).getEntityIds()[slot], IREntity::kNullEntity);
        EXPECT_EQ(pool(canvas).getTransformIndices()[slot], IRRender::kVoxelTransformStatic);
    }

    // A full spin after warmup allocates nothing and stays complete.
    for (int step = 0; step < 72; ++step) {
        const Pose pose{Axis::Y, step * 5.0 + 2.5};
        const Oracle oracle = destinationOracle(sourceCells(kPlate), pose);
        rebuild(plate, pose);
        if (oracle.margin_ > 1e-3) {
            SCOPED_TRACE(pose.degrees_);
            expectMatchesOracle(oracle.covered_, emitted(pool(canvas), set(plate)), capacity);
        }
        EXPECT_EQ(authoredBytes(set(plate)), authored);
    }
    EXPECT_EQ(m_rebuild.capacityStats_.growths_, 1u);
    EXPECT_EQ(set(plate).voxelStartIdx_, grownStart);
    EXPECT_EQ(set(plate).allocatedVoxels_, capacity);
    expectNeighborsIntact();

    // An edit mid-spin lands in the authored source and in the next pose.
    const IRMath::Color edited{10, 250, 10, 255};
    set(plate).changeVoxelColor(ivec3(0, 0, 0), edited);
    rebuild(plate, {Axis::Y, 45.0});
    bool editedEmitted = false;
    for (int i = 0; i < capacity; ++i) {
        editedEmitted |= sameColor(pool(canvas).getColors()[grownStart + i].color_, edited);
    }
    EXPECT_TRUE(editedEmitted);
    EXPECT_TRUE(sameColor(set(plate).authoredRecords()[0].color_, edited));

    // Identity restores the authored records and parks the tail, keeping capacity.
    rebuild(plate, {Axis::Y, 0.0});
    EXPECT_TRUE(set(plate).rotationSourceVoxels_.empty());
    EXPECT_EQ(set(plate).allocatedVoxels_, capacity);
    EXPECT_TRUE(sameColor(set(plate).voxels_[0].color_, edited));
    for (int i = 1; i < 144; ++i) {
        EXPECT_TRUE(sameColor(set(plate).voxels_[i].color_, IRMath::Color{200, 80, 40, 255}));
    }
    for (int i = 144; i < capacity; ++i) {
        EXPECT_EQ(pool(canvas).getColors()[grownStart + i].color_.alpha_, 0);
        EXPECT_FALSE(activeBit(pool(canvas), grownStart + i));
    }
    rebuild(plate, {Axis::Y, 45.0});
    EXPECT_EQ(m_rebuild.capacityStats_.growths_, 1u) << "identity keeps the high-water mark";
    expectNeighborsIntact();

    // Both freed spans are reusable at their exact sizes.
    const std::size_t residentBefore = pool(canvas).residentSlotCount();
    destroyNow(plate);
    EXPECT_EQ(
        pool(canvas).residentSlotCount(),
        residentBefore - static_cast<std::size_t>(capacity)
    );
    const auto reuseGrown =
        makeSet(canvas, {"reuse", ivec3(capacity, 1, 1), nullptr}, {1, 1, 1, 255});
    const auto reuseOld = makeSet(canvas, kPlate, IRMath::Color{3, 3, 3, 255});
    EXPECT_EQ(set(reuseGrown).voxelStartIdx_, grownStart);
    EXPECT_EQ(set(reuseOld).voxelStartIdx_, plateStart);
    expectNeighborsIntact();
}

// A pool that admits neither the 3/2 request nor the exact exposed count fails
// closed, preserves authored records, and recovers on a later admissible pose
// and once the pool frees an exact-size span.
TEST_F(GridDestinationCapacityTest, UnadmittedPoseFailsClosedAndRecovers) {
    // [low 7][plate 144][spare 216][high fills the tail]: no free span fits.
    const auto canvas = makeCanvas(7 + 144 + 216 + 33);
    const auto low = makeSet(canvas, {"low", ivec3(7, 1, 1), nullptr}, {9, 9, 9, 255});
    const auto plate = makeSet(canvas, kPlate, IRMath::Color{200, 80, 40, 255});
    const auto spare = makeSet(canvas, {"spare", ivec3(216, 1, 1), nullptr}, {5, 5, 5, 255});
    const auto high = makeSet(canvas, {"high", ivec3(33, 1, 1), nullptr}, {7, 7, 7, 255});
    ASSERT_FALSE(pool(canvas).canAllocateVoxels(145));
    const auto authored = authoredBytes(set(plate));
    const auto lowBytes = spanBytes(pool(canvas), set(low).voxelStartIdx_, 7);
    const auto highBytes = spanBytes(pool(canvas), set(high).voxelStartIdx_, 33);

    rebuild(plate, {Axis::Y, 45.0});
    EXPECT_EQ(m_rebuild.capacityStats_.failures_, 1u);
    EXPECT_EQ(m_rebuild.capacityStats_.lastFailure_, Rebuild::AdmissionFailure::CAPACITY);
    EXPECT_EQ(m_rebuild.capacityStats_.lastAuthored_, 144);
    EXPECT_EQ(m_rebuild.capacityStats_.lastRequired_, 145);
    EXPECT_EQ(m_rebuild.capacityStats_.lastCapacity_, 144);
    EXPECT_EQ(m_rebuild.capacityStats_.lastBudget_, 400);
    EXPECT_EQ(set(plate).unadmittedDestinationCells_, 145);
    EXPECT_EQ(set(plate).allocatedVoxels_, 144);
    EXPECT_TRUE(emitted(pool(canvas), set(plate)).empty()) << "zero partial destination cells";
    EXPECT_EQ(authoredBytes(set(plate)), authored);
    EXPECT_EQ(spanBytes(pool(canvas), set(low).voxelStartIdx_, 7), lowBytes);
    EXPECT_EQ(spanBytes(pool(canvas), set(high).voxelStartIdx_, 33), highBytes);

    // A pose inside the retained capacity recovers without growth.
    const Pose quarter{Axis::Y, 90.0};
    rebuild(plate, quarter);
    EXPECT_EQ(set(plate).unadmittedDestinationCells_, 0);
    expectMatchesOracle(
        destinationOracle(sourceCells(kPlate), quarter).covered_,
        emitted(pool(canvas), set(plate)),
        144
    );

    // Fail again; the pool then frees an exact 216-slot span, and render
    // rebuilds the cull bounds of the now-empty allocation so the ordinary
    // gate would reject the set before it could retry.
    rebuild(plate, {Axis::Y, 45.0});
    ASSERT_EQ(m_rebuild.capacityStats_.failures_, 2u);
    const std::size_t spareStart = set(spare).voxelStartIdx_;
    destroyNow(spare);
    pool(canvas).rebuildChunkBounds();
    m_rebuild.cullValid_ = true;
    m_rebuild.chunkVp_ = IRMath::IsoBounds2D{IRMath::vec2(-1.0e6f), IRMath::vec2(-1.0e6f + 1.0f)};
    ASSERT_FALSE(pool(canvas).isRangeVisible(
        set(plate).voxelStartIdx_,
        static_cast<std::size_t>(set(plate).allocatedVoxels_),
        m_rebuild.chunkVp_
    ));

    const Pose pose{Axis::Y, 45.0};
    rebuild(plate, pose);
    EXPECT_EQ(set(plate).unadmittedDestinationCells_, 0);
    EXPECT_EQ(set(plate).voxelStartIdx_, spareStart);
    EXPECT_EQ(set(plate).allocatedVoxels_, 216);
    const auto recovered = emitted(pool(canvas), set(plate));
    EXPECT_EQ(recovered.size(), 145u);
    expectMatchesOracle(destinationOracle(sourceCells(kPlate), pose).covered_, recovered, 216);
    EXPECT_EQ(authoredBytes(set(plate)), authored);
    EXPECT_EQ(spanBytes(pool(canvas), set(low).voxelStartIdx_, 7), lowBytes);
    EXPECT_EQ(spanBytes(pool(canvas), set(high).voxelStartIdx_, 33), highBytes);
}

// Without the exact-fit fallback the 3/2 request would strand an admissible pose.
TEST_F(GridDestinationCapacityTest, SpeculativeRequestFallsBackToTheExactExposedCount) {
    const auto canvas = makeCanvas(144 + 145);
    const auto plate = makeSet(canvas, kPlate, IRMath::Color{200, 80, 40, 255});
    rebuild(plate, {Axis::Y, 45.0});
    EXPECT_EQ(set(plate).unadmittedDestinationCells_, 0);
    EXPECT_EQ(set(plate).allocatedVoxels_, 145);
    EXPECT_EQ(emitted(pool(canvas), set(plate)).size(), 145u);
    EXPECT_EQ(m_rebuild.capacityStats_.peakCapacity_, 145);
}

TEST_F(GridDestinationCapacityTest, WalkGuardFailsClosedInsteadOfForwardMapping) {
    const auto canvas = makeCanvas(4096);
    const auto plate = makeSet(canvas, kPlate, IRMath::Color{200, 80, 40, 255});
    const auto authored = authoredBytes(set(plate));
    rebuild(plate, {Axis::Y, 45.0, 200.0});
    EXPECT_EQ(m_rebuild.capacityStats_.failures_, 1u);
    EXPECT_EQ(m_rebuild.capacityStats_.lastFailure_, Rebuild::AdmissionFailure::WALK_GUARD);
    EXPECT_GT(m_rebuild.capacityStats_.lastRequired_, Rebuild::kMaxDestWalkCells);
    EXPECT_GT(set(plate).unadmittedDestinationCells_, 0);
    EXPECT_TRUE(emitted(pool(canvas), set(plate)).empty());
    EXPECT_EQ(authoredBytes(set(plate)), authored);

    const Pose pose{Axis::Y, 45.0};
    rebuild(plate, pose);
    EXPECT_EQ(set(plate).unadmittedDestinationCells_, 0);
    expectMatchesOracle(
        destinationOracle(sourceCells(kPlate), pose).covered_,
        emitted(pool(canvas), set(plate)),
        set(plate).allocatedVoxels_
    );
}

// The implicit-GRID twin shares the policy.
TEST_F(GridDestinationCapacityTest, ImplicitGridArmGrowsTheSameWay) {
    const auto canvas = makeCanvas(4096);
    const auto plate = makeSet(canvas, kPlate, IRMath::Color{200, 80, 40, 255});
    IRSystem::System<IRSystem::REBUILD_GRID_VOXELS_IMPLICIT> implicit;
    implicit.tick(set(plate), productionTransform({Axis::Y, 45.0}));
    EXPECT_EQ(implicit.impl_.capacityStats_.growths_, 1u);
    EXPECT_EQ(emitted(pool(canvas), set(plate)).size(), 145u);
}

class VoxelPoolCapacityTest : public GridDestinationCapacityTest {};

TEST_F(VoxelPoolCapacityTest, ResidentAndPeakSlotsTrackAllocationsNotTheTail) {
    const auto canvas = makeCanvas(1024);
    const auto a = makeSet(canvas, {"a", ivec3(10, 1, 1), nullptr}, {1, 1, 1, 255});
    const auto b = makeSet(canvas, {"b", ivec3(20, 1, 1), nullptr}, {1, 1, 1, 255});
    EXPECT_EQ(pool(canvas).residentSlotCount(), 30u);
    EXPECT_EQ(pool(canvas).peakResidentSlotCount(), 30u);
    destroyNow(a);
    EXPECT_EQ(pool(canvas).residentSlotCount(), 20u);
    EXPECT_EQ(pool(canvas).peakResidentSlotCount(), 30u);
    EXPECT_EQ(pool(canvas).getLiveVoxelCount(), 30) << "the freed span strands until reused";
    const auto c = makeSet(canvas, {"c", ivec3(10, 1, 1), nullptr}, {1, 1, 1, 255});
    EXPECT_EQ(set(c).voxelStartIdx_, 0u);
    EXPECT_EQ(pool(canvas).residentSlotCount(), 30u);
    destroyNow(b);
    destroyNow(c);
    EXPECT_EQ(pool(canvas).residentSlotCount(), 0u);
}

TEST_F(VoxelPoolCapacityTest, RefusedGrowthChangesNothing) {
    const auto canvas = makeCanvas(150);
    const auto plate = makeSet(canvas, kPlate, IRMath::Color{200, 80, 40, 255});
    const auto before = spanBytes(pool(canvas), 0, 150);
    const std::uint64_t generation = pool(canvas).getContentGeneration();
    EXPECT_FALSE(set(plate).growAllocation(pool(canvas), 145));
    EXPECT_FALSE(set(plate).growAllocation(pool(canvas), 144)) << "never shrinks";
    EXPECT_EQ(spanBytes(pool(canvas), 0, 150), before);
    EXPECT_EQ(pool(canvas).getContentGeneration(), generation);
    EXPECT_EQ(set(plate).voxelStartIdx_, 0u);
    EXPECT_EQ(set(plate).allocatedVoxels_, 144);
    EXPECT_EQ(pool(canvas).residentSlotCount(), 144u);
}

TEST_F(VoxelPoolCapacityTest, GrowthCarriesAuthoredStateAndParksTheTail) {
    const auto canvas = makeCanvas(512);
    const auto plate = makeSet(canvas, kPlate, IRMath::Color{200, 80, 40, 255});
    auto &p = pool(canvas);
    for (int i = 0; i < 144; ++i) {
        p.getPositionOffsets()[i] = vec3(0.0f, 0.25f * (i % 3), 0.0f);
        p.getPositionGlobals()[i].pos_ = vec3(static_cast<float>(i));
    }
    set(plate).changeVoxelColor(ivec3(3, 0, 4), IRMath::Color{0, 0, 255, 0});
    const auto authored = spanBytes(p, 0, 144);
    ASSERT_TRUE(set(plate).growAllocation(p, 200));
    EXPECT_EQ(set(plate).voxelStartIdx_, 144u);
    EXPECT_EQ(set(plate).allocatedVoxels_, 200);
    EXPECT_EQ(set(plate).numVoxels_, 144);
    EXPECT_EQ(set(plate).voxels_.size(), 144u);
    EXPECT_EQ(set(plate).voxels_.data(), p.getColors().data() + 144);
    EXPECT_EQ(spanBytes(p, 144, 144), authored);
    for (std::size_t slot = 288; slot < 344; ++slot) {
        EXPECT_EQ(p.getColors()[slot].color_.alpha_, 0);
        EXPECT_FALSE(activeBit(p, slot));
        EXPECT_EQ(p.getEntityIds()[slot], plate);
    }
    for (std::size_t slot = 0; slot < 144; ++slot) {
        EXPECT_FALSE(activeBit(p, slot));
        EXPECT_EQ(p.getEntityIds()[slot], IREntity::kNullEntity);
    }
    EXPECT_EQ(p.residentSlotCount(), 200u);
    EXPECT_EQ(p.peakResidentSlotCount(), 344u) << "both spans are resident mid-migration";
}

// Canvas teardown re-stages the authored records only and frees the whole
// allocation, including the output tail.
TEST_F(VoxelPoolCapacityTest, RestageReleasesTheWholeAllocationAndKeepsAuthoredRecords) {
    const auto canvas = makeCanvas(4096);
    const auto plate = makeSet(canvas, kPlate, IRMath::Color{200, 80, 40, 255});
    const auto authored = authoredBytes(set(plate));
    rebuild(plate, {Axis::Y, 45.0});
    ASSERT_EQ(set(plate).allocatedVoxels_, 216);
    IRPrefab::VoxelPool::restageSet(set(plate));
    EXPECT_EQ(pool(canvas).residentSlotCount(), 0u);
    EXPECT_EQ(set(plate).allocatedVoxels_, 0);
    EXPECT_EQ(set(plate).pendingVoxels_.size(), 144u);
    EXPECT_EQ(authoredBytes(set(plate)), authored);
    ASSERT_TRUE(set(plate).attachToCanvas(canvas));
    EXPECT_EQ(set(plate).allocatedVoxels_, 144);
}

} // namespace
