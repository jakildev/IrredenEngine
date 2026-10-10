#include <gtest/gtest.h>

#include <irreden/ir_entity.hpp>
#include <irreden/ir_system.hpp>
#include <irreden/ir_time.hpp>

#include <irreden/common/components/component_local_transform.hpp>
#include <irreden/common/components/component_rotation_mode.hpp>
#include <irreden/common/components/component_world_transform.hpp>
#include <irreden/common/components/entity_anchor.hpp>
#include <irreden/common/rotation_mode.hpp>
#include <irreden/render/active_canvas.hpp>
#include <irreden/render/canvas_part.hpp>
#include <irreden/render/components/component_canvas_fog_of_war.hpp>
#include <irreden/render/components/component_entity_canvas.hpp>
#include <irreden/render/components/component_fog_field.hpp>
#include <irreden/render/components/component_fog_revealed.hpp>
#include <irreden/render/fog_of_war.hpp>
#include <irreden/render/systems/system_fog_subject_adopt.hpp>
#include <irreden/voxel/components/component_voxel_pool.hpp>
#include <irreden/voxel/components/component_voxel_set.hpp>
#include <irreden/voxel/systems/system_rebuild_grid_voxels.hpp>
#include <irreden/voxel/voxel_pool_teardown.hpp>

#include <cstdint>
#include <string_view>

namespace {

using IRComponents::C_CanvasFogOfWar;
using IRComponents::C_EntityCanvas;
using IRComponents::C_FogField;
using IRComponents::C_FogRevealed;
using IRComponents::C_LocalTransform;
using IRComponents::C_RotationMode;
using IRComponents::C_VoxelPool;
using IRComponents::C_VoxelSetNew;
using IRComponents::C_WorldTransform;
using IRComponents::EntityAnchor;
using IRComponents::RotationMode;
using IRMath::Color;
using IRMath::ivec2;
using IRMath::ivec3;
using IRMath::vec3;

constexpr Color kColor{200, 80, 40, 255};
constexpr std::uint8_t kOldFactor = 40;
constexpr std::uint8_t kNewFactor = 200;
constexpr std::uint32_t kCarrierMask = IRComponents::VoxelReserved::kFogCarrierMask;

std::uint32_t bodyCarrier(std::uint8_t factor) {
    return IRComponents::VoxelReserved::kFogBody |
           (static_cast<std::uint32_t>(factor) << IRComponents::VoxelReserved::kFogBodyFactorShift);
}

void expectCarrier(
    C_VoxelPool &pool, const C_VoxelSetNew &set, std::uint32_t expected, std::string_view context
) {
    const auto records = IRPrefab::Fog::poolRecords(pool, set);
    ASSERT_EQ(records.size(), static_cast<std::size_t>(set.numVoxels_)) << context;
    for (const IRComponents::C_Voxel &voxel : records) {
        if (voxel.color_.alpha_ != 0) {
            EXPECT_EQ(voxel.reserved_ & kCarrierMask, expected) << context;
        }
    }
}

class FogCarrierPolicyTest : public testing::Test {
  protected:
    FogCarrierPolicyTest() {
        m_worldCanvas = IREntity::createEntity(
            C_VoxelPool{ivec3(24, 24, 24)},
            C_CanvasFogOfWar{C_CanvasFogOfWar::HeadlessInit{}}
        );
        IRRender::setHeadlessActiveCanvasEntity(m_worldCanvas);
        IREntity::getComponent<C_CanvasFogOfWar>(m_worldCanvas)
            .setCell(0, 0, IRComponents::kFogStateVisible);
        m_systemManager.registerPipeline(
            IRTime::Events::UPDATE,
            {IRSystem::createSystem<IRSystem::FOG_SUBJECT_ADOPT>()}
        );
    }

    ~FogCarrierPolicyTest() override {
        IRRender::setHeadlessActiveCanvasEntity(IREntity::kNullEntity);
    }

    static IREntity::EntityId makeCanvas(bool managed = true) {
        const IREntity::EntityId canvas = IREntity::createEntity(C_VoxelPool{ivec3(16, 16, 16)});
        if (managed) {
            IREntity::getComponent<C_VoxelPool>(canvas).setFogCarrierPolicy(
                C_VoxelPool::FogCarrierPolicy::BODY,
                kOldFactor
            );
        }
        return canvas;
    }

    static IREntity::EntityId makeHost(IREntity::EntityId canvas) {
        return IREntity::createEntity(
            C_LocalTransform{},
            C_RotationMode{RotationMode::DETACHED_REVOXELIZE},
            C_EntityCanvas{canvas, ivec2(64)}
        );
    }

    C_VoxelPool &worldPool() {
        return IREntity::getComponent<C_VoxelPool>(m_worldCanvas);
    }

    static C_VoxelSetNew &setOf(IREntity::EntityId entity) {
        return IREntity::getComponent<C_VoxelSetNew>(entity);
    }

    void runAdopt() {
        m_systemManager.executePipeline(IRTime::Events::UPDATE);
        IREntity::flushStructuralChanges();
    }

    IREntity::EntityManager m_entityManager;
    IRSystem::SystemManager m_systemManager;
    IREntity::EntityId m_worldCanvas = IREntity::kNullEntity;
};

TEST_F(FogCarrierPolicyTest, RebuildWritesTheManagedCarrierOnEveryArm) {
    enum class Arm : int { INVERSE = 0, IDENTITY, FORWARD };
    constexpr Arm arms[]{Arm::INVERSE, Arm::IDENTITY, Arm::FORWARD};
    constexpr C_VoxelPool::FogCarrierPolicy policies[]{
        C_VoxelPool::FogCarrierPolicy::BODY,
        C_VoxelPool::FogCarrierPolicy::FIELD,
    };

    for (const C_VoxelPool::FogCarrierPolicy policy : policies) {
        for (const Arm arm : arms) {
            const IREntity::EntityId canvas = makeCanvas();
            const IREntity::EntityId entity = IREntity::createEntity(
                C_VoxelSetNew{ivec3(3, 3, 3), kColor, EntityAnchor::CENTER, canvas},
                C_RotationMode{RotationMode::GRID}
            );
            C_VoxelSetNew &set = setOf(entity);
            C_VoxelPool &pool = IREntity::getComponent<C_VoxelPool>(canvas);
            IRSystem::System<IRSystem::REBUILD_GRID_VOXELS> rebuild;
            C_WorldTransform rotated{};
            rotated.rotation_ = IRMath::quatAxisAngle(vec3(0.0f, 0.0f, 1.0f), IRMath::kPi / 6.0f);
            const C_RotationMode grid{RotationMode::GRID};

            rebuild.tick(set, rotated, grid);
            ASSERT_FALSE(set.rotationSourceVoxels_.empty());
            pool.setFogCarrierPolicy(policy, kNewFactor);

            C_WorldTransform next = rotated;
            if (arm == Arm::IDENTITY) {
                next = C_WorldTransform{};
            } else if (arm == Arm::FORWARD) {
                next.scale_ = vec3(0.0f);
            }
            rebuild.tick(set, next, grid);

            const std::uint32_t expected =
                policy == C_VoxelPool::FogCarrierPolicy::BODY ? bodyCarrier(kNewFactor) : 0u;
            expectCarrier(pool, set, expected, "rebuild arm must preserve the pool policy");
        }
    }
}

TEST_F(FogCarrierPolicyTest, LeavingABodyHostClearsTheCarrierUntilAdoption) {
    const IREntity::EntityId host = makeHost(makeCanvas());
    const IREntity::EntityId part =
        IRPrefab::CanvasPart::create(host, C_LocalTransform{}, ivec3(2, 2, 2), kColor);

    IRPrefab::RotationMode::setMode(part, RotationMode::GRID);

    expectCarrier(
        worldPool(),
        setOf(part),
        0u,
        "a released part enters an unmanaged pool as FIELD"
    );
    runAdopt();
    EXPECT_TRUE(IREntity::getComponentOptional<C_FogRevealed>(part).has_value());
    expectCarrier(worldPool(), setOf(part), bodyCarrier(255), "adoption owns the new BODY stamp");
}

TEST_F(FogCarrierPolicyTest, FieldPartStaysClearAfterLeavingABodyHost) {
    const IREntity::EntityId host = makeHost(makeCanvas());
    const IREntity::EntityId part =
        IRPrefab::CanvasPart::create(host, C_LocalTransform{}, ivec3(2, 2, 2), kColor);
    IREntity::setComponent(part, C_FogField{});

    IRPrefab::RotationMode::setMode(part, RotationMode::GRID);
    runAdopt();

    EXPECT_FALSE(IREntity::getComponentOptional<C_FogRevealed>(part).has_value());
    expectCarrier(worldPool(), setOf(part), 0u, "FIELD subjects have no adoption stamp");
}

TEST_F(FogCarrierPolicyTest, HostLeavingRevoxelizeClearsReleasedPartCarriers) {
    const IREntity::EntityId host = makeHost(makeCanvas());
    const IREntity::EntityId part =
        IRPrefab::CanvasPart::create(host, C_LocalTransform{}, ivec3(2, 2, 2), kColor);

    IRPrefab::RotationMode::setMode(host, RotationMode::GRID);

    expectCarrier(worldPool(), setOf(part), 0u, "releaseSets must remove the host policy");
}

TEST_F(FogCarrierPolicyTest, UnmanagedSourcesKeepTheirCarrierAcrossRestaging) {
    const IREntity::EntityId sourceCanvas = makeCanvas(false);
    const IREntity::EntityId entity = IREntity::createEntity(
        C_VoxelSetNew{ivec3(2, 2, 2), kColor, EntityAnchor::CENTER, sourceCanvas}
    );
    C_VoxelSetNew &set = setOf(entity);
    C_VoxelPool &sourcePool = IREntity::getComponent<C_VoxelPool>(sourceCanvas);
    IRPrefab::Fog::stampBodyCarrier(sourcePool, set, true, 255);

    IRPrefab::VoxelPool::restageSet(set);
    ASSERT_TRUE(set.attachToCanvas(m_worldCanvas));

    expectCarrier(worldPool(), set, bodyCarrier(255), "unmanaged stamps belong to the set");
}

} // namespace
