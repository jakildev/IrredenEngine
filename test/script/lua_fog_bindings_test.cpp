#include <irreden/ir_entity.hpp>
#include <irreden/ir_system.hpp>
#include <irreden/ir_time.hpp>
#include <irreden/render/components/component_canvas_fog_of_war.hpp>
#include <irreden/render/components/component_fog_revealed.hpp>
#include <irreden/render/systems/system_fog_los_build.hpp>
#include <irreden/script/lua_fog_bindings.hpp>
#include <irreden/script/lua_script.hpp>

#include <gtest/gtest.h>

#include <string>

namespace {

class LuaFogBindingsTest : public testing::Test {
  protected:
    LuaFogBindingsTest() {
        m_lua.bindLuaFog();
    }

    bool scriptSucceeds(const char *source) {
        return m_lua.lua().safe_script(source, sol::script_pass_on_error).valid();
    }

    std::string scriptError(const char *source) {
        sol::protected_function_result result =
            m_lua.lua().safe_script(source, sol::script_pass_on_error);
        EXPECT_FALSE(result.valid());
        if (result.valid()) {
            return {};
        }
        return sol::error(result).what();
    }

    template <std::size_t N> void expectScriptsFail(const char *const (&sources)[N]) {
        for (const char *source : sources) {
            SCOPED_TRACE(source);
            EXPECT_FALSE(scriptError(source).empty());
        }
    }

    void expectScriptFailsWith(const char *source, const char *expected) {
        const std::string error = scriptError(source);
        EXPECT_NE(error.find(expected), std::string::npos);
    }

    IRScript::LuaScript m_lua;
    IREntity::EntityManager m_entityManager;
};

TEST_F(LuaFogBindingsTest, ExposesCompleteSurfaceAndCppStateValues) {
    EXPECT_TRUE(scriptSucceeds(R"lua(
        local names = {
            'setVision', 'addVision', 'setVisionLineOfSight', 'clearVisions', 'evalReveal',
            'lineOfSight',
            'setEntityGoverned', 'getEntityReveal', 'setCell', 'getCell',
            'revealRadius', 'clear'
        }
        for _, name in ipairs(names) do
            assert(type(IRFog[name]) == 'function', name)
        end
        assert(IRFog.State.UNEXPLORED == 0)
        assert(IRFog.State.EXPLORED == 128)
        assert(IRFog.State.VISIBLE == 255)
    )lua"));
}

TEST_F(LuaFogBindingsTest, RegistrationIsOptInExtendingAndRepeatable) {
    IRScript::LuaScript lua;
    lua.lua().safe_script(R"lua(
        legacy = function() return 'legacy' end
        IRFog = { custom = 17, setVision = legacy }
    )lua");

    lua.bindLuaDrivenEcs();
    EXPECT_TRUE(
        lua.lua().safe_script("assert(IRFog.setVision == legacy and IRFog.custom == 17)").valid()
    );

    lua.bindLuaFog();
    lua.bindLuaFog();
    EXPECT_TRUE(lua.lua()
                    .safe_script(R"lua(
        assert(IRFog.setVision ~= legacy)
        assert(IRFog.custom == 17)
        assert(IRFog.State.EXPLORED == 128)
    )lua")
                    .valid());
}

TEST(LuaFogBindingRegistrationTest, NonTableCollisionRaisesWithoutReplacingGlobal) {
    IRScript::LuaScript lua;
    lua.lua()["IRFog"] = 41;
    EXPECT_THROW(lua.bindLuaFog(), std::invalid_argument);
    EXPECT_EQ(lua.lua()["IRFog"].get<int>(), 41);
}

TEST_F(LuaFogBindingsTest, MissingCanvasDefaultsAndOptionalValuesAreAccepted) {
    EXPECT_TRUE(scriptSucceeds(R"lua(
        assert(IRFog.setVision(1, 2, 3, nil, nil, nil, nil, nil) == -1)
        assert(IRFog.addVision(4, 5, 6) == -1)
        IRFog.setVisionLineOfSight(0, 1.5, 1)
        IRFog.clearVisions()
        IRFog.setCell(0, 0, IRFog.State.VISIBLE)
        assert(IRFog.getCell(0, 0) == IRFog.State.UNEXPLORED)
        IRFog.revealRadius(0, 0, 4)
        IRFog.clear()
        assert(IRFog.evalReveal(0, 0, 0) == 1)
        assert(IRFog.lineOfSight(0, 0, 0, 10, 10, 10) == true)
    )lua"));
}

TEST_F(LuaFogBindingsTest, StoredEntityRevealAndUngovernedDefaultAreReturned) {
    const IREntity::EntityId governed = IREntity::createEntity(IRComponents::C_FogRevealed{0.625f});
    const IREntity::EntityId plain = IREntity::createEntity();
    m_lua.lua()["governed"] = static_cast<double>(governed);
    m_lua.lua()["plain"] = static_cast<double>(plain);

    EXPECT_TRUE(scriptSucceeds(R"lua(
        assert(IRFog.getEntityReveal(governed) == 0.625)
        assert(IRFog.getEntityReveal(plain) == 1)
        IRFog.setEntityGoverned(plain)
        IRFog.setEntityGoverned(plain, nil)
        IRFog.setEntityGoverned(plain, false)
    )lua"));
}

TEST_F(LuaFogBindingsTest, RejectsWrongArityTypesAndNonFiniteNumbers) {
    constexpr const char *kBadCalls[] = {
        "IRFog.setVision(1, 2)",
        "IRFog.setVision(1, 2, 3, 4, 5, 6, 7, 8, 9)",
        "IRFog.addVision('1', 2, 3)",
        "IRFog.addVision(1, 2)",
        "IRFog.addVision(1, 2, 3, 4, 5, 6, 7, 8, 9)",
        "IRFog.clearVisions(1)",
        "IRFog.evalReveal(0, 0)",
        "IRFog.evalReveal(0, false, 0)",
        "IRFog.evalReveal(0, 0, 0, 0)",
        "IRFog.lineOfSight(0, 0, 0, 0, 0)",
        "IRFog.lineOfSight(0, 0, 0, 0, 0, '0')",
        "IRFog.lineOfSight(0, 0, 0, 0, 0, 0, 0)",
        "IRFog.setEntityGoverned()",
        "IRFog.setEntityGoverned('0')",
        "IRFog.setEntityGoverned(0, true, false)",
        "IRFog.getEntityReveal()",
        "IRFog.getEntityReveal(0, 0)",
        "IRFog.setCell(0.5, 0, IRFog.State.VISIBLE)",
        "IRFog.setCell(0, 0, 7)",
        "IRFog.setCell(0, 0)",
        "IRFog.setCell(0, 0, IRFog.State.VISIBLE, 0)",
        "IRFog.getCell({}, 0)",
        "IRFog.getCell(0)",
        "IRFog.getCell(0, 0, 0)",
        "IRFog.revealRadius(2147483647, 0, 1)",
        "IRFog.revealRadius(0, 0)",
        "IRFog.revealRadius(0, 0, '1')",
        "IRFog.revealRadius(0, 0, 1, 0)",
        "IRFog.clear(false)",
        "IRFog.setVision(0, 0, 0/0)",
        "IRFog.addVision(0, 0, math.huge)",
        "IRFog.setEntityGoverned(0)",
        "IRFog.getEntityReveal(-1)",
        "IRFog.setVisionLineOfSight(0)",
        "IRFog.setVisionLineOfSight(0, 1, 1, 1)",
        "IRFog.setVisionLineOfSight('0', 1)",
        "IRFog.setVisionLineOfSight(0.5, 1)",
        "IRFog.setVisionLineOfSight(0, 'high')",
        "IRFog.setVisionLineOfSight(0, 1, false)",
    };

    expectScriptsFail(kBadCalls);
    expectScriptFailsWith("IRFog.setCell(0, 0, 7)", "IRFog.setCell argument 3");
    expectScriptFailsWith("IRFog.revealRadius(0, 0, '1')", "IRFog.revealRadius argument 3");
    expectScriptFailsWith("IRFog.evalReveal(0, false, 0)", "IRFog.evalReveal argument 2");
}

TEST_F(LuaFogBindingsTest, RejectsWrongOptionalTypesWithoutMutatingDefaults) {
    constexpr const char *kBadCalls[] = {
        "IRFog.setVision(0, 0, 2, 'edge')",
        "IRFog.setVision(0, 0, 2, nil, true)",
        "IRFog.addVision(0, 0, 2, nil, nil, {})",
        "IRFog.addVision(0, 0, 2, nil, nil, nil, 'down')",
        "IRFog.addVision(0, 0, 2, nil, nil, nil, nil, false)",
    };
    expectScriptsFail(kBadCalls);
}

// The vision entries author a test-owned slot pair through bindFog's resolver,
// so the slot returns and the line-of-sight entry are checked without an
// active canvas.
class LuaFogVisionSlotsTest : public testing::Test {
  protected:
    LuaFogVisionSlotsTest() {
        m_eyes.fill(IRComponents::kFogVisionLosOff);
        IRScript::detail::bindFog(m_lua, [this]() {
            return IRScript::detail::FogVisionSlots{&m_observers, &m_eyes};
        });
    }

    std::string scriptError(const char *source) {
        sol::protected_function_result result =
            m_lua.lua().safe_script(source, sol::script_pass_on_error);
        EXPECT_FALSE(result.valid()) << source;
        return result.valid() ? std::string{} : std::string(sol::error(result).what());
    }

    IRScript::LuaScript m_lua;
    IREntity::EntityManager m_entityManager;
    IRComponents::FrameDataFogObservers m_observers{};
    IRComponents::FogLosEyeHeights m_eyes{};
};

TEST_F(LuaFogVisionSlotsTest, AddAndSetVisionReturnTheSlot) {
    EXPECT_TRUE(m_lua.lua()
                    .safe_script(R"lua(
        for i = 0, 7 do
            assert(IRFog.addVision(i, 0, 5) == i, 'slot ' .. i)
        end
        assert(IRFog.addVision(0, 0, 5) == -1, 'past the cap')
        assert(IRFog.setVision(0, 0, 5) == 0)
        assert(IRFog.addVision(0, 0, 0) == -1, 'zero radius')
        assert(IRFog.addVision(1, 1, 5) == 1)
    )lua")
                    .valid());
    EXPECT_EQ(m_observers.visionCircleCount_, 2);
}

TEST_F(LuaFogVisionSlotsTest, LineOfSightEntrySetsMaskEyeAndSoftness) {
    ASSERT_TRUE(m_lua.lua()
                    .safe_script(R"lua(
        IRFog.addVision(0, 0, 5)
        IRFog.addVision(0, 0, 5)
        IRFog.addVision(0, 0, 5)
        IRFog.setVisionLineOfSight(1, 1.5)
        IRFog.setVisionLineOfSight(2, 0, 0.75)
    )lua")
                    .valid());
    EXPECT_EQ(m_observers.losSourceMask_, (1 << 1) | (1 << 2));
    EXPECT_FLOAT_EQ(m_eyes[1], 1.5f);
    EXPECT_FLOAT_EQ(m_eyes[2], 0.0f);
    EXPECT_FLOAT_EQ(m_observers.losSoftness(1), IRComponents::kFogLosHardGate);
    EXPECT_FLOAT_EQ(m_observers.losSoftness(2), 0.75f);

    ASSERT_TRUE(m_lua.lua().safe_script("IRFog.setVisionLineOfSight(2, -1, 0.75)").valid());
    EXPECT_EQ(m_observers.losSourceMask_, 1 << 1);
    EXPECT_FLOAT_EQ(m_eyes[2], IRComponents::kFogVisionLosOff);
    EXPECT_FLOAT_EQ(m_observers.losSoftness(2), IRComponents::kFogLosHardGate)
        << "a negative eye height must clear the softness with the gate";

    ASSERT_TRUE(m_lua.lua().safe_script("IRFog.clearVisions()").valid());
    EXPECT_EQ(m_observers.losSourceMask_, 0);
    EXPECT_FLOAT_EQ(m_observers.losSoftness(1), IRComponents::kFogLosHardGate);
}

TEST_F(LuaFogVisionSlotsTest, LineOfSightEntryRejectsUnregisteredSlotsWithANamedError) {
    ASSERT_TRUE(m_lua.lua().safe_script("IRFog.addVision(0, 0, 5)").valid());
    for (const char *source :
         {"IRFog.setVisionLineOfSight(1, 1.5)",
          "IRFog.setVisionLineOfSight(-1, 1.5)",
          "IRFog.setVisionLineOfSight(8, 1.5)"}) {
        const std::string error = scriptError(source);
        EXPECT_NE(error.find("IRFog.setVisionLineOfSight argument 1"), std::string::npos)
            << source << ": " << error;
    }
    EXPECT_NE(
        scriptError("IRFog.setVisionLineOfSight(0, 'high')").find("argument 2 must be a number"),
        std::string::npos
    );
    EXPECT_NE(
        scriptError("IRFog.setVisionLineOfSight(0, 1, {})").find("argument 3 must be a number"),
        std::string::npos
    );
    EXPECT_EQ(m_observers.losSourceMask_, 0) << "a rejected call must leave the gates untouched";
}

TEST(LuaFogPipelineTest, FogLosBuildResolvesFromALuaPipeline) {
    IRScript::LuaScript lua;
    IREntity::EntityManager entityManager;
    IRSystem::SystemManager systemManager;
    lua.bindLuaDrivenEcs();
    const IRSystem::SystemId expected = lua.registerPrefabSystem<IRSystem::FOG_LOS_BUILD>();
    sol::protected_function_result result = lua.lua().safe_script(
        R"lua(
        local id = IRSystem.systemId(IRSystem.SystemName.FOG_LOS_BUILD)
        IRSystem.registerPipeline(IRTime.RENDER, { id })
        return id
    )lua",
        sol::script_pass_on_error
    );
    ASSERT_TRUE(result.valid()) << sol::error(result).what();
    EXPECT_EQ(static_cast<IRSystem::SystemId>(result.get<lua_Integer>()), expected);
}

} // namespace
