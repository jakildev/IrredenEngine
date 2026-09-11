#include <gtest/gtest.h>

#include <irreden/ir_entity.hpp>
#include <irreden/ir_system.hpp>
#include <irreden/ir_time.hpp>
#include <irreden/render/components/component_fog_revealed.hpp>
#include <irreden/render/components/component_fog_revealed_lua.hpp>
#include <irreden/script/lua_script.hpp>

namespace {

class LuaFogRevealedTest : public testing::Test {
  protected:
    LuaFogRevealedTest()
        : m_lua{}
        , m_entityManager{}
        , m_systemManager{} {
        m_lua.bindLuaDrivenEcs();
        m_lua.registerTypeFromTraits<IRComponents::C_FogRevealed>();
    }

    IRScript::LuaScript m_lua;
    IREntity::EntityManager m_entityManager;
    IRSystem::SystemManager m_systemManager;
};

TEST_F(LuaFogRevealedTest, ArchetypeColumnReadsCppRevealFactor) {
    IREntity::createEntity(IRComponents::C_FogRevealed{0.625f, true});
    auto result = m_lua.lua().safe_script(
        R"lua(
        observedReveal = nil
        return IRSystem.registerSystem({
            name = 'ReadFogReveal',
            components = { IRComponent.C_FogRevealed },
            tick = function(arch)
                for i = 0, arch.length - 1 do
                    observedReveal = arch.C_FogRevealed:at(i).revealFactor
                end
            end,
        })
    )lua",
        sol::script_pass_on_error
    );
    ASSERT_TRUE(result.valid()) << sol::error{result}.what();
    const IRSystem::SystemId systemId = result.get<lua_Integer>();
    m_systemManager.registerPipeline(IRTime::Events::UPDATE, {systemId});
    m_systemManager.executePipeline(IRTime::Events::UPDATE);
    EXPECT_FLOAT_EQ(m_lua.lua()["observedReveal"].get<float>(), 0.625f);
}

} // namespace
