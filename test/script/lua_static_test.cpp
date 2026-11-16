#include <gtest/gtest.h>

#include <irreden/ir_entity.hpp>
#include <irreden/ir_math.hpp>
#include <irreden/ir_system.hpp>

#include <irreden/common/components/component_local_transform.hpp>
#include <irreden/common/components/component_local_transform_lua.hpp>
#include <irreden/common/components/component_static_lua.hpp>
#include <irreden/common/static_residents.hpp>
#include <irreden/render/components/component_lod_tier_override.hpp>
#include <irreden/render/components/component_lod_tier_override_lua.hpp>
#include <irreden/script/lua_script.hpp>
#include <irreden/system/components/component_static.hpp>
#include <irreden/voxel/components/component_voxel_pool.hpp>
#include <irreden/voxel/components/component_voxel_set.hpp>
#include <irreden/voxel/components/component_voxel_set_lua.hpp>

#include "../common/settle_now.hpp"

#include <functional>
#include <string>

// The Lua surface of static residents (docs/design/static-residents.md):
// the IRStatic table, the `reads` key of IRSystem.registerSystem, and the
// C_VoxelSetNew LOD properties that route through setLodBand.

namespace {

using IRComponents::C_LocalTransform;
using IRComponents::C_Settled;
using IRComponents::C_Static;
using IRComponents::C_VoxelPool;
using IRComponents::C_VoxelSetNew;
using IREntity::EntityId;
namespace Static = IRPrefab::Static;

class LuaStatic : public testing::Test {
  protected:
    LuaStatic() {
        m_lua.bindLuaDrivenEcs();
        m_lua.registerTypesFromTraits<C_Static, C_LocalTransform>();
        m_lua.registerTypeFromTraits<IRComponents::C_LodTierOverride>();
    }

    bool runOk(const std::string &source) {
        const auto result = m_lua.lua().safe_script(source, sol::script_pass_on_error);
        if (!result.valid()) {
            const sol::error error = result;
            ADD_FAILURE() << error.what();
        }
        return result.valid();
    }

    template <typename T> T global(const char *name) {
        return m_lua.lua()[name].get<T>();
    }

    IRScript::LuaScript m_lua;
    IREntity::EntityManager m_entity_manager;
    IRSystem::SystemManager m_system_manager;
};

TEST_F(LuaStatic, IRStaticDrivesTheTransitions) {
    const EntityId entity = IREntity::createEntity();
    m_lua.lua()["e"] = static_cast<lua_Integer>(entity);

    ASSERT_TRUE(runOk("frozen = IRStatic.freeze(e) == IRStatic.FreezeResult.FROZEN"));
    EXPECT_TRUE(global<bool>("frozen"));
    IREntity::flushStructuralChanges();
    ASSERT_TRUE(runOk(
        "isStatic = IRStatic.isStatic(e)\n"
        "isSettled = IRStatic.isSettled(e)\n"
        "again = IRStatic.freeze(e) == IRStatic.FreezeResult.ALREADY_STATIC\n"
        "missing = IRStatic.freeze(987654) == IRStatic.FreezeResult.MISSING\n"
        "staticCount = IRStatic.stats().static\n"
        "settlingCount = IRStatic.stats().settling"
    ));
    EXPECT_TRUE(global<bool>("isStatic"));
    EXPECT_FALSE(global<bool>("isSettled"));
    EXPECT_TRUE(global<bool>("again"));
    EXPECT_TRUE(global<bool>("missing"));
    EXPECT_EQ(global<int>("staticCount"), 1);
    EXPECT_EQ(global<int>("settlingCount"), 1);

    ASSERT_EQ(IRTest::settleNow({entity}), 0u);
    ASSERT_TRUE(runOk("settledNow = IRStatic.isSettled(e)\nIRStatic.resettle(e)"));
    EXPECT_TRUE(global<bool>("settledNow"));
    IREntity::flushStructuralChanges();
    EXPECT_TRUE(Static::isStatic(entity));
    EXPECT_FALSE(Static::isSettled(entity));

    ASSERT_TRUE(runOk("IRStatic.thaw(e)"));
    IREntity::flushStructuralChanges();
    EXPECT_FALSE(Static::isStatic(entity));
}

TEST_F(LuaStatic, AnEvalSystemWritesWhatItListsUnlessItIsUnderReads) {
    IREntity::createEntity();
    IREntity::createEntity(C_Static{});
    ASSERT_EQ(IRTest::settleNow({IREntity::createEntity(C_Static{})}), 0u);

    ASSERT_TRUE(runOk(R"lua(
        writerRows, readerRows, readerWrote, writerWrote = 0, 0, false, false
        writer = IRSystem.registerSystem({
            name = 'LuaWriter',
            components = { IRComponent.C_LocalTransform },
            tick = function(arch)
                writerRows = writerRows + arch.length
                local column = arch.C_LocalTransform
                writerWrote = pcall(function() column:setAt(0, column:at(0)) end)
            end,
        })
        reader = IRSystem.registerSystem({
            name = 'LuaReader',
            components = {},
            reads = { IRComponent.C_LocalTransform },
            tick = function(arch)
                readerRows = readerRows + arch.length
                local column = arch.C_LocalTransform
                if pcall(function() column:setAt(0, column:at(0)) end) then
                    readerWrote = true
                end
            end,
        })
    )lua"));
    m_system_manager.executeSystem(static_cast<IRSystem::SystemId>(global<int>("writer")));
    m_system_manager.executeSystem(static_cast<IRSystem::SystemId>(global<int>("reader")));

    EXPECT_EQ(global<int>("writerRows"), 1) << "a listed pose component counts as written";
    EXPECT_TRUE(global<bool>("writerWrote"));
    EXPECT_EQ(global<int>("readerRows"), 3) << "a reads component matches static entities";
    EXPECT_FALSE(global<bool>("readerWrote")) << "setAt on a reads column raises";
}

TEST_F(LuaStatic, APropertyWriteThroughAReadsColumnNeverReachesTheRow) {
    const EntityId entity = IREntity::createEntity(C_Static{});
    ASSERT_EQ(IRTest::settleNow({entity}), 0u);
    const IRMath::vec3 before = IREntity::getComponent<C_LocalTransform>(entity).translation_;

    ASSERT_TRUE(runOk(R"lua(
        readerRows, copyMoved = 0, false
        reader = IRSystem.registerSystem({
            name = 'LuaPropertyReader',
            components = {},
            reads = { IRComponent.C_LocalTransform },
            tick = function(arch)
                readerRows = readerRows + arch.length
                local row = arch.C_LocalTransform:at(0)
                row.translation = { x = 7, y = 8, z = 9 }
                copyMoved = row.translation.x == 7
            end,
        })
    )lua"));
    m_system_manager.executeSystem(static_cast<IRSystem::SystemId>(global<int>("reader")));

    ASSERT_EQ(global<int>("readerRows"), 1);
    EXPECT_TRUE(global<bool>("copyMoved")) << "the write lands on the copy Lua holds";
    EXPECT_EQ(IREntity::getComponent<C_LocalTransform>(entity).translation_, before);
    Static::drain();
    EXPECT_TRUE(Static::isSettled(entity));
}

TEST_F(LuaStatic, AnEvalSystemsDeclaredStaticExclusionSurvivesReclassification) {
    IREntity::createEntity();
    IREntity::createEntity(C_Static{});
    ASSERT_EQ(IRTest::settleNow({IREntity::createEntity(C_Static{})}), 0u);
    ASSERT_TRUE(runOk(R"lua(
        rows = 0
        excluding = IRSystem.registerSystem({
            name = 'LuaExcluding',
            components = {},
            reads = { IRComponent.C_LocalTransform },
            excludes = { IRComponent.C_Static },
            tick = function(arch) rows = rows + arch.length end,
        })
    )lua"));
    const auto system = static_cast<IRSystem::SystemId>(global<int>("excluding"));
    IRSystem::addSystemExcludeTag<C_Settled>(system);
    m_system_manager.executeSystem(system);
    EXPECT_EQ(global<int>("rows"), 1);
    EXPECT_TRUE(m_system_manager.isDerivingSystem(system));
    EXPECT_EQ(
        m_system_manager.getDeclaredExcludeArchetype(system),
        (IREntity::getArchetype<C_Static, C_Settled>())
    );
}

TEST_F(LuaStatic, LodBandPropertiesResettleASettledOwner) {
    m_lua.registerType<IRMath::Color, IRMath::Color(int, int, int, int)>("Color");
    m_lua.registerType<IRMath::ivec3, IRMath::ivec3(int, int, int)>("ivec3");
    m_lua.registerTypeFromTraits<C_VoxelSetNew>();
    const EntityId canvas = IREntity::createEntity(C_VoxelPool{IRMath::ivec3(16, 16, 16)});

    for (const std::string property : {"lodMin", "lodMax"}) {
        const EntityId entity = IREntity::createEntity(
            C_VoxelSetNew{
                IRMath::ivec3(2),
                IRMath::Color{200, 100, 50, 255},
                IRComponents::EntityAnchor::CORNER,
                canvas
            },
            C_Static{}
        );
        ASSERT_EQ(IRTest::settleNow({entity}), 0u);
        C_VoxelSetNew &set = IREntity::getComponent<C_VoxelSetNew>(entity);
        set.ownerEntityId_ = entity;
        set.ownerStatic_ = true;
        m_lua.lua()["bandedSet"] = std::ref(set);
        ASSERT_TRUE(runOk("bandedSet." + property + " = 0\nbandedSet = nil"));
        EXPECT_EQ(static_cast<int>(property == "lodMin" ? set.lodMin_ : set.lodMax_), 0);
        Static::drain();
        EXPECT_TRUE(Static::isStatic(entity)) << property;
        EXPECT_FALSE(Static::isSettled(entity)) << property;
    }
}

// The engine-owned boundary on the Lua lanes: C_Settled has no binding, and
// a name that resolved anyway would reach addComponentDynamic, which refuses.
TEST_F(LuaStatic, LuaCannotNameSettled) {
    ASSERT_TRUE(runOk("unbound = IRComponent.C_Settled == nil"));
    EXPECT_TRUE(global<bool>("unbound"));
    const EntityId entity = IREntity::createEntity(C_Static{});
    m_lua.lua()["handle"] = IRScript::LuaEntity{entity};
    const auto result = m_lua.lua().safe_script(
        "IREntity.addLuaComponent(handle, IRComponent.C_Settled, {})",
        sol::script_pass_on_error
    );
    EXPECT_FALSE(result.valid());
    EXPECT_FALSE(Static::isSettled(entity));
}

// The Lua C_LodTierOverride binding replaces an attached override through
// setComponent, so it resettles the entity like the C++ overwrite.
TEST_F(LuaStatic, TheLodOverrideBindingResettlesASettledEntity) {
    const EntityId entity = IREntity::createEntity(
        C_Static{},
        IRComponents::C_LodTierOverride{IRRender::LodLevel::LOD_3}
    );
    ASSERT_EQ(IRTest::settleNow({entity}), 0u);
    ASSERT_TRUE(Static::isSettled(entity));
    m_lua.lua()["handle"] = IRScript::LuaEntity{entity};
    ASSERT_TRUE(runOk(
        "IREntity.addLuaComponent(handle, IRComponent.C_LodTierOverride, "
        "{ tier = IRRender.LodLevel.LOD_0 })"
    ));
    EXPECT_FALSE(Static::isSettled(entity));
    EXPECT_TRUE(Static::isStatic(entity));
    EXPECT_EQ(
        IREntity::getComponent<IRComponents::C_LodTierOverride>(entity).tier_,
        IRRender::LodLevel::LOD_0
    );
}

// setLuaField writes a Lua-typed row by id: the raw path, which no engine
// deriving system reads, so the entity stays settled.
TEST_F(LuaStatic, SetLuaFieldOnASettledEntityIsTheRawPath) {
    ASSERT_TRUE(runOk("C_Charge = IRComponent.register('StaticCharge', { level = 1 })"));
    const EntityId entity = IREntity::createEntity(C_Static{});
    m_entity_manager.addComponentDynamic(
        entity,
        m_entity_manager.getComponentTypeByName("StaticCharge")
    );
    ASSERT_EQ(IRTest::settleNow({entity}), 0u);
    m_lua.lua()["handle"] = IRScript::LuaEntity{entity};
    ASSERT_TRUE(runOk(
        "IREntity.setLuaField(handle, C_Charge, C_Charge.fields.level.index, 9)\n"
        "level = IREntity.getLuaField(handle, C_Charge, C_Charge.fields.level.index)"
    ));
    EXPECT_EQ(global<int>("level"), 9);
    EXPECT_TRUE(Static::isSettled(entity));
}

} // namespace
