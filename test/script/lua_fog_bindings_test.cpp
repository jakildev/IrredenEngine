#include <irreden/ir_entity.hpp>
#include <irreden/ir_system.hpp>
#include <irreden/ir_time.hpp>
#include <irreden/render/active_canvas.hpp>
#include <irreden/render/components/component_canvas_fog_of_war.hpp>
#include <irreden/render/components/component_fog_field.hpp>
#include <irreden/render/components/component_fog_revealed.hpp>
#include <irreden/render/fog_of_war.hpp>
#include <irreden/render/systems/system_fog_los_build.hpp>
#include <irreden/script/lua_fog_bindings.hpp>
#include <irreden/script/lua_script.hpp>

#include <gtest/gtest.h>

#include <memory>
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
            'lineOfSight', 'captureLineOfSight', 'lineOfSightCaptured',
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
        IRFog.captureLineOfSight()
        local verdicts = IRFog.lineOfSightCaptured(0, 0, 0, {1, 2, 3, 4, 5, 6})
        assert(#verdicts == 2 and verdicts[1] == true and verdicts[2] == true)
        assert(#IRFog.lineOfSightCaptured(0, 0, 0, {}) == 0)
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
        "IRFog.captureLineOfSight(0)",
        "IRFog.lineOfSightCaptured(0, 0, 0)",
        "IRFog.lineOfSightCaptured(0, 0, 0, {}, 0)",
        "IRFog.lineOfSightCaptured('0', 0, 0, {})",
        "IRFog.lineOfSightCaptured(0, 0, 0/0, {})",
        "IRFog.lineOfSightCaptured(0, 0, 0, 1)",
        "IRFog.lineOfSightCaptured(0, 0, 0, {1, 2})",
        "IRFog.lineOfSightCaptured(0, 0, 0, {1, 2, '3'})",
        "IRFog.lineOfSightCaptured(0, 0, 0, {1, 2, math.huge})",
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
    expectScriptFailsWith(
        "IRFog.lineOfSightCaptured(0, 0, 0, {1, 2})",
        "IRFog.lineOfSightCaptured argument 4 length must be a multiple of 3"
    );
    expectScriptFailsWith(
        "IRFog.lineOfSightCaptured(0, 0, 0, {1, 2, 3, 4, false, 6})",
        "IRFog.lineOfSightCaptured argument 4 element 5 must be a number"
    );
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

// The vision entries author a test-owned payload and field through bindFog's
// resolver, so the slot returns, the field tier and the line-of-sight entry
// are checked without an active canvas.
class LuaFogVisionSlotsTest : public testing::Test {
  protected:
    LuaFogVisionSlotsTest() {
        IRScript::detail::bindFog(m_lua, [this]() {
            return IRScript::detail::FogVisionTarget{&m_observers, &m_field};
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
    IRPrefab::Fog::WorldField m_field;
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

TEST_F(LuaFogVisionSlotsTest, SourcesPastTheCapReachTheFieldAndClearWithTheSet) {
    ASSERT_TRUE(m_lua.lua()
                    .safe_script(R"lua(
        for i = 1, 9 do
            IRFog.addVision(i * 20, 0, 2)
        end
    )lua")
                    .valid());
    EXPECT_EQ(m_observers.visionCircleCount_, IRComponents::kMaxFogVisionCircles);
    EXPECT_EQ(m_field.getCell({180, 0}), IRComponents::kFogStateVisible);
    EXPECT_EQ(m_field.getCell({160, 0}), IRComponents::kFogStateUnexplored)
        << "an analytic source must not stamp the field";

    ASSERT_TRUE(m_lua.lua().safe_script("IRFog.clearVisions()").valid());
    EXPECT_EQ(m_observers.visionCircleCount_, 0);
    EXPECT_EQ(m_field.getCell({180, 0}), IRComponents::kFogStateUnexplored);

    ASSERT_TRUE(m_lua.lua()
                    .safe_script(R"lua(
        for i = 1, 9 do
            IRFog.addVision(i * 20, 0, 2)
        end
        IRFog.setVision(0, 0, 2)
    )lua")
                    .valid());
    EXPECT_EQ(m_observers.visionCircleCount_, 1);
    EXPECT_EQ(m_field.getCell({180, 0}), IRComponents::kFogStateUnexplored)
        << "setVision must clear the field tier with the analytic slots";
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
    EXPECT_FLOAT_EQ(m_observers.losEyeHeight(1), 1.5f);
    EXPECT_FLOAT_EQ(m_observers.losEyeHeight(2), 0.0f);
    EXPECT_FLOAT_EQ(m_observers.losSoftness(1), IRComponents::kFogLosHardGate);
    EXPECT_FLOAT_EQ(m_observers.losSoftness(2), 0.75f);

    ASSERT_TRUE(m_lua.lua().safe_script("IRFog.setVisionLineOfSight(2, -1, 0.75)").valid());
    EXPECT_EQ(m_observers.losSourceMask_, 1 << 1);
    EXPECT_FLOAT_EQ(m_observers.losEyeHeight(2), IRComponents::kFogVisionLosOff);
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

// The `__gc` metatable on the functor userdata sol2 holds as upvalue 2 of a
// bound stateful entry, or null when the entry carries no such userdata.
const void *boundFunctorMetatable(sol::state &lua, const char *entry) {
    lua_State *state = lua.lua_state();
    const int top = lua_gettop(state);
    sol::function function = lua["IRFog"][entry];
    function.push(state);
    const void *metatable = nullptr;
    if (lua_getupvalue(state, -1, 2) != nullptr && lua_type(state, -1) == LUA_TUSERDATA &&
        lua_getmetatable(state, -1) != 0) {
        metatable = lua_topointer(state, -1);
    }
    lua_settop(state, top);
    return metatable;
}

TEST(LuaFogBindingTeardownTest, DefaultResolverBindingClosesTheState) {
    auto lua = std::make_unique<IRScript::LuaScript>();
    lua->bindLuaFog();
    lua.reset();
}

TEST(LuaFogBindingTeardownTest, CapturingResolverIsReleasedWithTheScript) {
    auto sentinel = std::make_shared<int>(0);
    const std::weak_ptr<int> watch = sentinel;
    auto lua = std::make_unique<IRScript::LuaScript>();
    IRScript::detail::bindFog(*lua, [sentinel]() { return IRScript::detail::FogVisionTarget{}; });
    sentinel.reset();
    ASSERT_FALSE(watch.expired()) << "the bound vision entries own the resolver";
    lua.reset();
    EXPECT_TRUE(watch.expired());
}

// sol2 finds a functor's finalizer by demangled type name, and GCC names both
// raw `(sol::variadic_args)` lambdas here identically, so `captureLineOfSight`'s
// `shared_ptr` capture would be destroyed as `setVision`'s `std::function`. The
// pair discriminates because their wrapped types differ (`int` vs `void`
// return); entries that wrap to one `std::function` type share a metatable.
TEST(LuaFogBindingTeardownTest, DifferentlyTypedStatefulEntriesHaveDistinctFinalizers) {
    IRScript::LuaScript lua;
    lua.bindLuaFog();
    const void *setVision = boundFunctorMetatable(lua.lua(), "setVision");
    const void *captureLineOfSight = boundFunctorMetatable(lua.lua(), "captureLineOfSight");
    ASSERT_NE(setVision, nullptr);
    ASSERT_NE(captureLineOfSight, nullptr);
    EXPECT_NE(setVision, captureLineOfSight);
}

} // namespace
