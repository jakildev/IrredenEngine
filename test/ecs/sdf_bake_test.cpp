#include <gtest/gtest.h>

#include <irreden/ir_entity.hpp>
#include <irreden/voxel/components/component_voxel_pool.hpp>
#include <irreden/voxel/sdf_bake.hpp>
#include <irreden/voxel/systems/system_rebuild_grid_voxels.hpp>

#include <array>
#include <limits>

namespace {
using namespace IRMath;
using IRComponents::C_VoxelPool;
using IRComponents::C_VoxelSetNew;
using IRComponents::EntityAnchor;
using IRMath::SDF::ShapeType;
using IRPrefab::Voxel::bakeSdfGrid;
namespace Flags = IRComponents::VoxelFlags;
const Color kColor{40, 120, 220, 7};

bool occupied(const IRAsset::DenseVoxelSet &dense, ivec3 index) {
    return dense.voxels_[index3DtoIndex1D(index, dense.boundsMax_ - dense.boundsMin_)]
               .color_.alpha_ != 0;
}

TEST(SdfBake, BoxUsesAuthoredParametersExactlyOnceOnOddAndEvenGrids) {
    // Independent inequality: effective half extent=(authored-1)/2, then
    // threshold +1/2, hence 2*abs(cell center)<=authored. Fractional sizes
    // distinguish omitted normalization from accidentally applying it twice.
    const vec4 params{3.2f, 2.2f, 4.0f, 0.0f};
    for (int edge : {8, 9}) {
        const auto dense = bakeSdfGrid(ivec3(edge), ShapeType::BOX, params, kColor);
        ASSERT_EQ(dense.voxels_.size(), static_cast<std::size_t>(edge * edge * edge));
        EXPECT_EQ(dense.boundsMin_, ivec3(0));
        EXPECT_EQ(dense.boundsMax_, ivec3(edge));
        for (int z = 0; z < edge; ++z)
            for (int y = 0; y < edge; ++y)
                for (int x = 0; x < edge; ++x) {
                    const vec3 p = vec3(x, y, z) - vec3((edge - 1) * 0.5f);
                    const bool expected = 2 * abs(p.x) <= params.x && 2 * abs(p.y) <= params.y &&
                                          2 * abs(p.z) <= params.z;
                    const auto &record = dense.voxels_[index3DtoIndex1D({x, y, z}, ivec3(edge))];
                    EXPECT_EQ(record.color_.alpha_, expected ? 255 : 0)
                        << x << ',' << y << ',' << z;
                    if (expected) {
                        EXPECT_EQ(record.color_.red_, kColor.red_);
                        EXPECT_EQ(record.flags_, Flags::kAoContrib);
                        EXPECT_EQ(record.reserved_, 0u);
                    }
                }
    }
}

TEST(SdfBake, SphereThresholdIncludesBoundaryAndKeepsInteriorFilled) {
    const auto odd = bakeSdfGrid(ivec3(7), ShapeType::SPHERE, vec4(1.5f, 0, 0, 0), kColor);
    unsigned count = 0;
    for (int z = 0; z < 7; ++z)
        for (int y = 0; y < 7; ++y)
            for (int x = 0; x < 7; ++x) {
                const int dx = x - 3, dy = y - 3, dz = z - 3;
                const bool expected = dx * dx + dy * dy + dz * dz <= 4;
                EXPECT_EQ(occupied(odd, {x, y, z}), expected);
                count += occupied(odd, {x, y, z});
            }
    EXPECT_EQ(count, 33u);
    const auto even = bakeSdfGrid(ivec3(6), ShapeType::SPHERE, vec4(3, 0, 0, 0), kColor);
    count = 0;
    for (const auto &record : even.voxels_)
        count += record.color_.alpha_ != 0;
    EXPECT_EQ(count, 160u); // Radius 3 plus threshold 0.5, not the radius-3-only 136-cell carve.
}

TEST(SdfBake, TorusPreservesHoleAndTubeThreshold) {
    const auto dense = bakeSdfGrid(ivec3(11), ShapeType::TORUS, vec4(3, 0.5f, 0, 0), kColor);
    // Grid index 5 is local zero. Major radius 3; tube radius + threshold = 1.
    EXPECT_FALSE(occupied(dense, {5, 5, 5}));
    EXPECT_FALSE(occupied(dense, {6, 5, 5}));
    EXPECT_TRUE(occupied(dense, {8, 5, 5}));
    EXPECT_TRUE(occupied(dense, {9, 5, 5}));
    EXPECT_TRUE(occupied(dense, {8, 5, 6}));
    EXPECT_FALSE(occupied(dense, {10, 5, 5}));
    EXPECT_FALSE(occupied(dense, {8, 5, 7}));
}

TEST(SdfBake, RejectsInvalidInputsBeforeAllocating) {
    const int limit = std::numeric_limits<int>::max();
    for (ivec3 size : {ivec3(0, 2, 2), ivec3(2, -1, 2), ivec3(limit, 2, 2)}) {
        EXPECT_TRUE(bakeSdfGrid(size, ShapeType::BOX, vec4(3), kColor).voxels_.empty());
    }
    for (const auto type : {ShapeType::CUSTOM_SDF, static_cast<ShapeType>(99)}) {
        EXPECT_TRUE(bakeSdfGrid(ivec3(3), type, vec4(3), kColor).voxels_.empty());
    }
    for (float bad :
         {std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN()}) {
        for (int axis = 0; axis < 4; ++axis) {
            vec4 params(3);
            params[axis] = bad;
            EXPECT_TRUE(bakeSdfGrid(ivec3(3), ShapeType::SPHERE, params, kColor).voxels_.empty());
        }
    }
}

TEST(SdfBake, BudgetIncludesOutputAndScratchWithInclusiveByteBoundary) {
    constexpr std::size_t cells = 2 * 3 * 4;
    constexpr std::size_t outputBytes = cells * sizeof(IRAsset::VoxelRecord);
    constexpr std::size_t workingBytes = outputBytes + cells * sizeof(float);
    static_assert(IRPrefab::Voxel::sdfBakeScratchBytes(cells) == workingBytes - outputBytes);
    for (const auto budget : {std::size_t(0), outputBytes, workingBytes - 1}) {
        const auto rejected = bakeSdfGrid(ivec3(2, 3, 4), ShapeType::BOX, vec4(4), kColor, budget);
        EXPECT_TRUE(rejected.voxels_.empty());
        EXPECT_EQ(rejected.boundsMin_, ivec3(0));
        EXPECT_EQ(rejected.boundsMax_, ivec3(0));
    }
    const auto accepted =
        bakeSdfGrid(ivec3(2, 3, 4), ShapeType::BOX, vec4(4), kColor, workingBytes);
    ASSERT_EQ(accepted.voxels_.size(), cells);
    for (const auto &record : accepted.voxels_)
        EXPECT_EQ(record.color_.alpha_, 255);
}

TEST(SdfBake, DefaultBudgetRejectsLargeValidGridAndAllowsBeyondLegacy64Cube) {
    EXPECT_TRUE(bakeSdfGrid(ivec3(1024), ShapeType::BOX, vec4(1), kColor).voxels_.empty());
    const int limit = std::numeric_limits<int>::max();
    EXPECT_TRUE(bakeSdfGrid(ivec3(limit), ShapeType::BOX, vec4(1), kColor).voxels_.empty());
    EXPECT_TRUE(bakeSdfGrid(
                    ivec3(limit),
                    ShapeType::BOX,
                    vec4(1),
                    kColor,
                    std::numeric_limits<std::size_t>::max()
    )
                    .voxels_.empty());
    const auto dense = bakeSdfGrid(ivec3(65), ShapeType::SPHERE, vec4(1), kColor);
    ASSERT_EQ(dense.voxels_.size(), 65u * 65u * 65u);
    EXPECT_TRUE(occupied(dense, ivec3(32)));
    EXPECT_FALSE(occupied(dense, ivec3(0)));
}

TEST(SdfBake, ExplicitBuiltinSupportRejectsUnknownTypes) {
    for (const auto type :
         {ShapeType::BOX,
          ShapeType::SPHERE,
          ShapeType::CYLINDER,
          ShapeType::ELLIPSOID,
          ShapeType::CURVED_PANEL,
          ShapeType::WEDGE,
          ShapeType::TAPERED_BOX,
          ShapeType::CONE,
          ShapeType::TORUS}) {
        const auto dense = bakeSdfGrid(ivec3(7), type, vec4(2, 2, 4, 1), kColor);
        ASSERT_EQ(dense.voxels_.size(), 343u) << static_cast<unsigned>(type);
        bool anyOccupied = false;
        for (const auto &record : dense.voxels_)
            anyOccupied |= record.color_.alpha_ != 0;
        EXPECT_TRUE(anyOccupied) << static_cast<unsigned>(type);
    }
    for (const auto type :
         {ShapeType::CUSTOM_SDF,
          static_cast<ShapeType>(10),
          static_cast<ShapeType>(std::numeric_limits<std::uint32_t>::max())}) {
        EXPECT_TRUE(bakeSdfGrid(ivec3(7), type, vec4(2, 2, 4, 1), kColor).voxels_.empty());
    }
}

TEST(SdfBake, AssetChunksPreserveBoundsOccupancyAndOrdinaryVoxelFields) {
    const auto dense = bakeSdfGrid(ivec3(7), ShapeType::SPHERE, vec4(1.5f, 0, 0, 0), kColor);
    const auto bounds = IRAsset::makeBoundsChunk(dense.boundsMin_, dense.boundsMax_);
    const auto decodedBounds = IRAsset::readBoundsChunk(bounds.data_);
    ASSERT_TRUE(decodedBounds.ok());
    EXPECT_EQ(decodedBounds.value_.boundsMin_, dense.boundsMin_);
    EXPECT_EQ(decodedBounds.value_.boundsMax_, dense.boundsMax_);
    const auto records = IRAsset::makeVoxelRecordsChunk(dense.voxels_);
    const auto decoded = IRAsset::readVoxelRecordsChunk(records.data_, dense.voxelCount());
    ASSERT_TRUE(decoded.ok());
    ASSERT_EQ(decoded.value_.voxels_.size(), dense.voxels_.size());
    for (std::size_t i = 0; i < dense.voxels_.size(); ++i) {
        const auto &actual = decoded.value_.voxels_[i];
        const auto &expected = dense.voxels_[i];
        EXPECT_EQ(actual.color_.toPackedRGBA(), expected.color_.toPackedRGBA());
        EXPECT_EQ(actual.flags_, expected.flags_);
        EXPECT_EQ(actual.material_id_, expected.material_id_);
        EXPECT_EQ(actual.bone_id_, expected.bone_id_);
        EXPECT_EQ(actual.layer_id_, expected.layer_id_);
        EXPECT_EQ(actual.reserved_, expected.reserved_);
    }
}

class SdfBakePool : public testing::Test {
  protected:
    IREntity::EntityManager m_entityManager;
};

TEST_F(SdfBakePool, HeadlessAssetAttachesWithAnchorsMasksAndAllSixFaceBits) {
    const auto dense = bakeSdfGrid(ivec3(5), ShapeType::BOX, vec4(3, 3, 3, 0), kColor);
    const std::array<ivec3, 6> steps = {
        ivec3(-1, 0, 0),
        ivec3(1, 0, 0),
        ivec3(0, -1, 0),
        ivec3(0, 1, 0),
        ivec3(0, 0, -1),
        ivec3(0, 0, 1)
    };
    const auto inside = [](ivec3 p) {
        return p.x >= 1 && p.x <= 3 && p.y >= 1 && p.y <= 3 && p.z >= 1 && p.z <= 3;
    };
    for (EntityAnchor anchor : {EntityAnchor::CENTER, EntityAnchor::GROUND}) {
        const auto object =
            IREntity::createEntity(IRPrefab::DenseVoxel::toComponent(dense, anchor));
        {
            const auto &staged = IREntity::getComponent<C_VoxelSetNew>(object);
            EXPECT_EQ(staged.numVoxels_, 0);
            EXPECT_EQ(staged.recordCount(), 125u);
            EXPECT_EQ(staged.pendingVoxels_.size(), 125u);
            EXPECT_EQ(staged.canvasEntity_, IREntity::kNullEntity);
        }
        const auto canvas = IREntity::createEntity(C_VoxelPool{ivec3(8)});
        auto &set = IREntity::getComponent<C_VoxelSetNew>(object);
        ASSERT_TRUE(set.attachToCanvas(canvas));
        EXPECT_TRUE(set.pendingVoxels_.empty());
        ASSERT_EQ(set.numVoxels_, 125);
        const auto &pool = IREntity::getComponent<C_VoxelPool>(canvas);
        for (int z = 0; z < 5; ++z)
            for (int y = 0; y < 5; ++y)
                for (int x = 0; x < 5; ++x) {
                    const ivec3 cell(x, y, z);
                    const int flat = index3DtoIndex1D(cell, ivec3(5));
                    const auto slot = set.voxelStartIdx_ + static_cast<std::size_t>(flat);
                    const bool active =
                        (pool.getActiveMask()[slot / IRComponents::kVoxelActiveMaskBits] >>
                         (slot % IRComponents::kVoxelActiveMaskBits)) &
                        1u;
                    EXPECT_EQ(active, inside(cell));
                    std::uint8_t faceBits = 0;
                    if (inside(cell))
                        for (int face = 0; face < 6; ++face)
                            if (inside(cell + steps[face]))
                                faceBits |= 1u << (2 + face);
                    EXPECT_EQ(set.voxels_[flat].flags_ & Flags::kFaceOccludedMask, faceBits);
                    if (active)
                        EXPECT_NE(set.voxels_[flat].flags_ & Flags::kAoContrib, 0);
                    const vec3 expected =
                        vec3(cell) - vec3(2, 2, anchor == EntityAnchor::CENTER ? 2 : 4.5f);
                    EXPECT_EQ(set.positions_[flat].pos_, expected);
                }
        IREntity::destroyEntity(object);
        IREntity::getEntityManager().destroyMarkedEntities();
        IREntity::destroyEntity(canvas);
        IREntity::getEntityManager().destroyMarkedEntities();
    }
}

TEST_F(SdfBakePool, TiltedCylinderAndTorusLeaveARealChannelBetweenTheirShadows) {
    using IRComponents::C_WorldTransform;
    struct Subject {
        ShapeType type_;
        vec4 params_;
        vec3 position_;
        std::size_t occupied_;
    };
    const std::array<Subject, 4> subjects{{
        {ShapeType::SPHERE, vec4(4, 0, 0, 0), vec3(-8.75f, -9.5f, -2), 386},
        {ShapeType::CYLINDER, vec4(4, 0, 8, 0), vec3(9.25f, -9.5f, -2), 625},
        {ShapeType::CONE, vec4(4, 0, 8, 0), vec3(-8.75f, 8.5f, -2), 230},
        {ShapeType::TORUS, vec4(3, 1.5f, 0, 0), vec3(9.25f, 8.5f, -2), 215},
    }};
    const vec4 local = quatAxisAngle(normalize(vec3(1, 2, 0.5f)), kPi / 5.0f);
    std::array<std::vector<vec3>, 5> occupiedCells;
    for (std::size_t shape = 0; shape < subjects.size(); ++shape) {
        const auto &subject = subjects[shape];
        const auto dense = bakeSdfGrid(ivec3(11), subject.type_, subject.params_, kColor);
        const auto canvas = IREntity::createEntity(C_VoxelPool{ivec3(12)});
        const auto entity =
            IREntity::createEntity(IRPrefab::DenseVoxel::toComponent(dense, EntityAnchor::CENTER));
        auto &set = IREntity::getComponent<C_VoxelSetNew>(entity);
        ASSERT_TRUE(set.attachToCanvas(canvas));
        auto &pool = IREntity::getComponent<C_VoxelPool>(canvas);
        C_WorldTransform transform;
        transform.translation_ = subject.position_;
        transform.rotation_ = vec4(-local.y, local.x, local.w, -local.z);
        IRSystem::System<IRSystem::REBUILD_GRID_VOXELS> rebuild;
        rebuild.inverseArm(set, transform, pool);
        ASSERT_EQ(set.unadmittedDestinationCells_, 0);
        for (int i = 0; i < set.allocatedVoxels_; ++i) {
            const auto slot = set.voxelStartIdx_ + static_cast<std::size_t>(i);
            if (pool.getColors()[slot].color_.alpha_ != 0)
                occupiedCells[shape].push_back(pool.getPositionGlobals()[slot].pos_);
        }
        ASSERT_EQ(occupiedCells[shape].size(), subject.occupied_);
    }

    for (const vec3 center :
         {vec3(-5.5f, -0.5f, -8),
          vec3(12.5f, -0.5f, -8),
          vec3(-5.5f, 17.5f, -8),
          vec3(12.5f, 17.5f, -8)}) {
        for (int x = -1; x <= 1; ++x)
            for (float y : {-0.5f, 0.5f})
                occupiedCells[4].push_back(center + vec3(float(x), y, 0));
    }

    // Independent double-precision slabs over the actual rebuilt unit boxes;
    // no sun-map projection, face flags, candidate index or shader predicate.
    const auto blocked = [](vec3 receiver, const std::vector<vec3> &cells) {
        constexpr std::array<double, 3> sun{0.35, 0.85, -0.4};
        for (const auto cell : cells) {
            double near = 0.0;
            double far = std::numeric_limits<double>::infinity();
            for (int axis = 0; axis < 3; ++axis) {
                const double a = (double(cell[axis]) - 0.5 - receiver[axis]) / sun[axis];
                const double b = (double(cell[axis]) + 0.5 - receiver[axis]) / sun[axis];
                near = IRMath::max(near, IRMath::min(a, b));
                far = IRMath::min(far, IRMath::max(a, b));
            }
            if (near <= far)
                return true;
        }
        return false;
    };
    const auto hitMask = [&](vec3 receiver) {
        unsigned result = 0;
        for (std::size_t shape = 0; shape < occupiedCells.size(); ++shape)
            if (blocked(receiver, occupiedCells[shape]))
                result |= 1u << shape;
        return result;
    };
    EXPECT_EQ(hitMask(vec3(2.5f, -14.5f, 5.5f)), 0u);
    EXPECT_EQ(hitMask(vec3(2.4f, -14.5f, 5.5f)), 0u);
    EXPECT_EQ(hitMask(vec3(2.6f, -14.5f, 5.5f)), 0u);
    EXPECT_EQ(hitMask(vec3(3.5f, -14.5f, 5.5f)), 2u); // Cylinder.
    EXPECT_EQ(hitMask(vec3(1.5f, -14.5f, 5.5f)), 8u); // Torus.
    EXPECT_EQ(hitMask(vec3(2.5f, -13.4f, 5.5f)), 8u);
    EXPECT_EQ(hitMask(vec3(2.5f, -15.5f, 5.5f)), 2u);
    occupiedCells[1].push_back(vec3(6, -6, 2));
    EXPECT_EQ(hitMask(vec3(2.5f, -14.5f, 5.5f)), 2u)
        << "An occupied cell across the channel must block the formerly clear ray";
}
} // namespace
