#include <irreden/ir_entity.hpp>
#include <irreden/render/active_canvas.hpp>
#include <irreden/render/components/component_canvas_fog_of_war.hpp>
#include <irreden/render/components/component_fog_field.hpp>
#include <irreden/render/components/component_fog_revealed.hpp>
#include <irreden/render/fog_of_war.hpp>
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
            'setVision', 'addVision', 'clearVisions', 'evalReveal', 'lineOfSight',
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
        IRFog.setVision(1, 2, 3, nil, nil, nil, nil, nil)
        IRFog.addVision(4, 5, 6)
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

// `setEntityGoverned(e, false)` compares against the FIELD class, not
// against BODY state: on an entity that was never adopted it tags FIELD
// instead of returning early, and the tag is idempotent.
TEST_F(LuaFogBindingsTest, UngovernedCallOnAFreshEntityTagsField) {
    const IREntity::EntityId fresh = IREntity::createEntity();
    m_lua.lua()["fresh"] = static_cast<double>(fresh);
    ASSERT_EQ(IRPrefab::Fog::subjectClass(fresh), IRPrefab::Fog::FogSubjectClass::BODY);

    EXPECT_TRUE(scriptSucceeds("IRFog.setEntityGoverned(fresh, false)"));

    EXPECT_EQ(IRPrefab::Fog::subjectClass(fresh), IRPrefab::Fog::FogSubjectClass::FIELD);
    EXPECT_TRUE(IREntity::getComponentOptional<IRComponents::C_FogField>(fresh).has_value());
    EXPECT_TRUE(scriptSucceeds("IRFog.setEntityGoverned(fresh, false)"));
    EXPECT_EQ(IRPrefab::Fog::subjectClass(fresh), IRPrefab::Fog::FogSubjectClass::FIELD);
}

// With a fogged canvas active (headless seam, textureless fog), the service's
// oracle is the BODY verdict: a VISIBLE grid cell reveals with no circle at
// all, an EXPLORED or UNEXPLORED cell does not, and a circle still reveals on
// its own. The circles-only forward reads 0 on the VISIBLE cell.
class LuaFogBindingsActiveCanvasTest : public LuaFogBindingsTest {
  protected:
    LuaFogBindingsActiveCanvasTest() {
        m_canvas = IREntity::createEntity(
            IRComponents::C_CanvasFogOfWar{IRComponents::C_CanvasFogOfWar::HeadlessInit{}}
        );
        IRRender::setHeadlessActiveCanvasEntity(m_canvas);
    }
    ~LuaFogBindingsActiveCanvasTest() override {
        IRRender::setHeadlessActiveCanvasEntity(IREntity::kNullEntity);
    }

    IREntity::EntityId m_canvas = IREntity::kNullEntity;
};

TEST_F(LuaFogBindingsActiveCanvasTest, EvalRevealReadsAVisibleCellWithNoCircles) {
    EXPECT_TRUE(scriptSucceeds(R"lua(
        IRFog.clearVisions()
        assert(IRFog.evalReveal(3, 4, 0) == 0)
        IRFog.setCell(3, 4, IRFog.State.VISIBLE)
        assert(IRFog.evalReveal(3, 4, 0) == 1)
        IRFog.setCell(3, 4, IRFog.State.EXPLORED)
        assert(IRFog.evalReveal(3, 4, 0) == 0)
        IRFog.setCell(3, 4, IRFog.State.UNEXPLORED)
        IRFog.addVision(3, 4, 6)
        assert(IRFog.evalReveal(3, 4, 0) == 1)
        assert(IRFog.evalReveal(30, 40, 0) == 0)
    )lua"));
}

} // namespace
