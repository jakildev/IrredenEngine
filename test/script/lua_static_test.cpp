#include <gtest/gtest.h>

#include <irreden/ir_entity.hpp>
#include <irreden/ir_math.hpp>
#include <irreden/ir_system.hpp>

#include <irreden/common/components/component_local_transform.hpp>
#include <irreden/common/components/component_local_transform_lua.hpp>
#include <irreden/common/components/component_static_lua.hpp>
#include <irreden/common/static_residents.hpp>
#include <irreden/script/lua_script.hpp>
#include <irreden/system/components/component_static.hpp>
#include <irreden/voxel/components/component_voxel_pool.hpp>
#include <irreden/voxel/components/component_voxel_set.hpp>
#include <irreden/voxel/components/component_voxel_set_lua.hpp>

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

    IREntity::setComponent(entity, C_Settled{});
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
    IREntity::createEntity(C_Static{}, C_Settled{});

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
            C_Static{},
            C_Settled{}
        );
        C_VoxelSetNew &set = IREntity::getComponent<C_VoxelSetNew>(entity);
        set.ownerEntityId_ = entity;
        set.ownerStatic_ = true;
        m_lua.lua()["bandedSet"] = std::ref(set);
        ASSERT_TRUE(runOk("bandedSet." + property + " = 0\nbandedSet = nil"));
        EXPECT_EQ(
            static_cast<int>(property == "lodMin" ? set.lodMin_ : set.lodMax_),
            0
        );
        Static::drain();
        EXPECT_TRUE(Static::isStatic(entity)) << property;
        EXPECT_FALSE(Static::isSettled(entity)) << property;
    }
}

} // namespace
