// The LOD tier surface a creation's Lua policy drives: the active-tier read
// (`IRRender.getActiveLodTier`), the per-entity pin (`C_LodTierOverride`,
// attached and detached from Lua), and the DENSE voxel-set band the gate system
// enforces. Every case runs the zoom -> tier write LOD_UPDATE performs, so the
// tiers asserted here are the ones a live camera produces.

#include <gtest/gtest.h>

#include <irreden/ir_entity.hpp>
#include <irreden/ir_math.hpp>
#include <irreden/ir_system.hpp>
#include <irreden/ir_time.hpp>
#include <irreden/render/components/component_lod_tier_override.hpp>
#include <irreden/render/components/component_lod_tier_override_lua.hpp>
#include <irreden/render/lod_tier_snapshot.hpp>
#include <irreden/render/lod_utils.hpp>
#include <irreden/render/systems/system_gate_voxel_sets_by_lod.hpp>
#include <irreden/render/systems/system_lod_update.hpp>
#include <irreden/script/lua_script.hpp>
#include <irreden/voxel/components/component_voxel_pool.hpp>
#include <irreden/voxel/components/component_voxel_set.hpp>
#include <irreden/voxel/components/component_voxel_set_lua.hpp>

#include <stdexcept>
#include <string>
#include <tuple>

namespace {

using IRComponents::C_LodTierOverride;
using IRComponents::C_VoxelPool;
using IRComponents::C_VoxelSetNew;
using IRComponents::EntityAnchor;
using IRComponents::kVoxelActiveMaskBits;
using IRMath::Color;
using IRMath::ivec3;
using IRMath::vec2;
using IRRender::LodLevel;

// LuaScript first so its sol::state outlives the managers.
class LodTierLua : public testing::Test {
  protected:
    LodTierLua()
        : m_lua{}
        , m_entityManager{}
        , m_systemManager{} {
        m_lua.bindLuaDrivenEcs();
        m_lua.registerTypeFromTraits<C_LodTierOverride>();
        m_lua.lua()["testEntity"] = [this]() { return IRScript::LuaEntity{m_pinned}; };
    }

    static void setZoom(float zoom) {
        IRSystem::System<IRSystem::LOD_UPDATE>::writeActiveTier(vec2(zoom));
    }

    lua_Integer activeTierFromLua() {
        auto result = m_lua.lua().safe_script(
            "return IRRender.getActiveLodTier()",
            sol::script_pass_on_error
        );
        EXPECT_TRUE(result.valid()) << sol::error{result}.what();
        return result.valid() ? result.get<lua_Integer>() : -1;
    }

    void runLua(const std::string &source) {
        auto result = m_lua.lua().safe_script(source, sol::script_pass_on_error);
        ASSERT_TRUE(result.valid()) << sol::error{result}.what();
    }

    LodLevel resolved(IREntity::EntityId entity) {
        IRPrefab::Lod::TierSnapshot snapshot;
        snapshot.capture();
        return snapshot.resolve(entity);
    }

    // True when every authored-opaque voxel of `set` is live in the pool mask,
    // false when none is; a mixed mask fails the test.
    static bool drawn(const C_VoxelSetNew &set, const C_VoxelPool &pool) {
        int live = 0;
        for (int i = 0; i < set.numVoxels_; ++i) {
            const std::size_t slot = set.voxelStartIdx_ + static_cast<std::size_t>(i);
            live += (pool.getActiveMask()[slot / kVoxelActiveMaskBits] >>
                     (slot % kVoxelActiveMaskBits)) &
                    1u;
        }
        EXPECT_TRUE(live == 0 || live == set.numVoxels_) << "partial mask: " << live;
        return live == set.numVoxels_;
    }

    IRScript::LuaScript m_lua;
    IREntity::EntityManager m_entityManager;
    IRSystem::SystemManager m_systemManager;
    IREntity::EntityId m_pinned = IREntity::kNullEntity;
};

TEST_F(LodTierLua, ActiveTierTracksZoom) {
    // No LOD_UPDATE write yet: the coarsest tier, which culls nothing.
    EXPECT_EQ(activeTierFromLua(), static_cast<lua_Integer>(LodLevel::LOD_4));

    setZoom(1.0f);
    EXPECT_EQ(activeTierFromLua(), 4);
    setZoom(4.0f);
    EXPECT_EQ(activeTierFromLua(), 2);
    setZoom(16.0f);
    EXPECT_EQ(activeTierFromLua(), 0);

    auto result = m_lua.lua().safe_script(
        "local L = IRRender.LodLevel\n"
        "return L.LOD_0, L.LOD_1, L.LOD_2, L.LOD_3, L.LOD_4",
        sol::script_pass_on_error
    );
    ASSERT_TRUE(result.valid()) << sol::error{result}.what();
    std::tuple<lua_Integer, lua_Integer, lua_Integer, lua_Integer, lua_Integer> tiers = result;
    EXPECT_EQ(std::get<0>(tiers), static_cast<lua_Integer>(LodLevel::LOD_0));
    EXPECT_EQ(std::get<1>(tiers), static_cast<lua_Integer>(LodLevel::LOD_1));
    EXPECT_EQ(std::get<2>(tiers), static_cast<lua_Integer>(LodLevel::LOD_2));
    EXPECT_EQ(std::get<3>(tiers), static_cast<lua_Integer>(LodLevel::LOD_3));
    EXPECT_EQ(std::get<4>(tiers), static_cast<lua_Integer>(LodLevel::LOD_4));
}

TEST_F(LodTierLua, OverridePinsTier) {
    m_pinned = IREntity::createEntity();
    const IREntity::EntityId free = IREntity::createEntity();
    setZoom(1.0f);
    EXPECT_EQ(resolved(m_pinned), LodLevel::LOD_4);

    runLua(
        "IREntity.addLuaComponent(testEntity(), IRComponent.C_LodTierOverride, "
        "{ tier = IRRender.LodLevel.LOD_0 })"
    );
    EXPECT_EQ(resolved(m_pinned), LodLevel::LOD_0);
    EXPECT_EQ(resolved(free), LodLevel::LOD_4) << "a pin is per entity";

    // The pin holds while the camera moves through every tier.
    setZoom(4.0f);
    EXPECT_EQ(resolved(m_pinned), LodLevel::LOD_0);
    EXPECT_EQ(resolved(free), LodLevel::LOD_2);
    setZoom(1.0f);

    runLua("IREntity.removeLuaComponent(testEntity(), IRComponent.C_LodTierOverride)");
    EXPECT_EQ(resolved(m_pinned), LodLevel::LOD_4);

    const C_LodTierOverride pin{LodLevel::LOD_1};
    EXPECT_EQ(IRRender::resolveEntityLod(LodLevel::LOD_4, &pin), LodLevel::LOD_1);
    EXPECT_EQ(IRRender::resolveEntityLod(LodLevel::LOD_4, nullptr), LodLevel::LOD_4);
}

TEST_F(LodTierLua, OverrideRejectsOutOfRangeTier) {
    m_pinned = IREntity::createEntity();
    auto result = m_lua.lua().safe_script(
        "IREntity.addLuaComponent(testEntity(), IRComponent.C_LodTierOverride, { tier = 5 })",
        sol::script_pass_on_error
    );
    EXPECT_FALSE(result.valid());
    EXPECT_FALSE(IREntity::getComponentOptional<C_LodTierOverride>(m_pinned).has_value());
}

TEST_F(LodTierLua, OverrideCtorRejectsOutOfRangeTier) {
    EXPECT_THROW(C_LodTierOverride{static_cast<LodLevel>(5)}, std::runtime_error);
    EXPECT_EQ(C_LodTierOverride{LodLevel::LOD_4}.tier_, LodLevel::LOD_4);
}

TEST_F(LodTierLua, DenseBandGatesVisibility) {
    m_lua.registerType<Color, Color(int, int, int, int)>("Color");
    m_lua.registerType<ivec3, ivec3(int, int, int)>("ivec3");
    m_lua.registerTypeFromTraits<C_VoxelSetNew>();

    const IREntity::EntityId canvas = IREntity::createEntity(C_VoxelPool{ivec3(16, 16, 16)});
    const Color color{200, 100, 50, 255};
    m_pinned = IREntity::createEntity(C_VoxelSetNew{ivec3(2), color, EntityAnchor::CORNER, canvas});
    const IREntity::EntityId unbanded =
        IREntity::createEntity(C_VoxelSetNew{ivec3(2), color, EntityAnchor::CORNER, canvas});

    // The band is authored through the Lua field surface.
    m_lua.lua()["bandedSet"] = std::ref(IREntity::getComponent<C_VoxelSetNew>(m_pinned));
    runLua(
        "bandedSet.lodMin = IRRender.LodLevel.LOD_2\n"
        "bandedSet.lodMax = IRRender.LodLevel.LOD_2\n"
        "bandedSet = nil"
    );

    m_systemManager.registerPipeline(
        IRTime::Events::UPDATE,
        {IRSystem::createSystem<IRSystem::GATE_VOXEL_SETS_BY_LOD>()}
    );
    const auto tick = [this]() { m_systemManager.executePipeline(IRTime::Events::UPDATE); };
    const auto &pool = IREntity::getComponent<C_VoxelPool>(canvas);
    const auto banded = [this]() -> C_VoxelSetNew & {
        return IREntity::getComponent<C_VoxelSetNew>(m_pinned);
    };
    const std::size_t span = banded().voxelStartIdx_;

    setZoom(16.0f); // tier 0, finer than the band
    tick();
    EXPECT_TRUE(banded().lodCulled_);
    EXPECT_FALSE(drawn(banded(), pool));

    setZoom(4.0f); // tier 2, inside the band
    tick();
    EXPECT_FALSE(banded().lodCulled_);
    EXPECT_TRUE(drawn(banded(), pool));

    setZoom(1.0f); // tier 4, coarser than the band
    tick();
    EXPECT_TRUE(banded().lodCulled_);
    EXPECT_FALSE(drawn(banded(), pool));

    // Gating never touches the allocation or the authored records.
    EXPECT_EQ(banded().voxelStartIdx_, span);
    EXPECT_EQ(banded().numVoxels_, 8);
    EXPECT_EQ(banded().voxels_[0].color_.alpha_, 255);
    EXPECT_TRUE(drawn(IREntity::getComponent<C_VoxelSetNew>(unbanded), pool))
        << "a default-band set is never gated";

    // An edit made while gated stays masked off until the band admits the set.
    banded().changeVoxelColorAll(Color{10, 20, 30, 255});
    EXPECT_FALSE(drawn(banded(), pool));

    // A pin resolves the gate the same way the camera tier does.
    runLua(
        "IREntity.addLuaComponent(testEntity(), IRComponent.C_LodTierOverride, "
        "{ tier = IRRender.LodLevel.LOD_2 })"
    );
    tick();
    EXPECT_TRUE(drawn(banded(), pool));

    // The other render gate still wins while the band admits the set.
    banded().visible_ = false;
    banded().syncActiveMask();
    banded().setLodCulled(true);
    banded().setLodCulled(false);
    EXPECT_FALSE(drawn(banded(), pool));
}

} // namespace
