#include <irreden/ir_entity.hpp>
#include <irreden/ir_system.hpp>
#include <irreden/ir_time.hpp>
#include <irreden/render/active_canvas.hpp>
#include <irreden/render/components/component_canvas_fog_of_war.hpp>
#include <irreden/render/components/component_fog_field.hpp>
#include <irreden/render/components/component_fog_revealed.hpp>
#include <irreden/render/fog_of_war.hpp>
#include <irreden/render/fog_reveal_systems.hpp>
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
            'revealRadius', 'exploreRadius', 'setCellChannels', 'getCellChannels',
            'setExploredPolicy', 'getExploredPolicy', 'setExploredTimeMs',
            'getExploredTimeMs', 'clear', 'setVisionCeiling', 'getVisionCeiling',
            'setRevealSurfaceTreatment', 'getRevealSurfaceTreatment',
            'clearRevealSurfaceTreatment'
        }
        for _, name in ipairs(names) do
            assert(type(IRFog[name]) == 'function', name)
        end
        assert(IRFog.State.UNEXPLORED == 0)
        assert(IRFog.State.EXPLORED == 128)
        assert(IRFog.State.VISIBLE == 255)
        assert(IRFog.ExploredPolicy.PERSISTENT == 0)
        assert(IRFog.ExploredPolicy.DECAY == 1)
        assert(IRFog.Channel.DEFAULT == 1)
    )lua"));
    EXPECT_EQ(
        static_cast<int>(IRPrefab::Fog::ExploredPolicy::PERSISTENT),
        m_lua.lua()["IRFog"]["ExploredPolicy"]["PERSISTENT"].get<int>()
    );
    EXPECT_EQ(
        static_cast<int>(IRPrefab::Fog::ExploredPolicy::DECAY),
        m_lua.lua()["IRFog"]["ExploredPolicy"]["DECAY"].get<int>()
    );
    EXPECT_EQ(
        IRComponents::kFogChannelDefault,
        m_lua.lua()["IRFog"]["Channel"]["DEFAULT"].get<std::uint32_t>()
    );
}

// Without an active canvas the new services validate and then take the
// existing no-op / default convention.
TEST_F(LuaFogBindingsTest, ExploredPolicyServicesDefaultWithoutACanvas) {
    EXPECT_TRUE(scriptSucceeds(R"lua(
        IRFog.setExploredPolicy(IRFog.ExploredPolicy.DECAY, 5000)
        IRFog.setExploredPolicy(IRFog.ExploredPolicy.PERSISTENT, 0, 3)
        local policy, duration, channels = IRFog.getExploredPolicy()
        assert(policy == IRFog.ExploredPolicy.PERSISTENT and duration == 0 and channels == 1)
        IRFog.setExploredTimeMs(2^53 - 1)
        assert(IRFog.getExploredTimeMs() == 0)
        IRFog.setCellChannels(1, 2, 2^32 - 1)
        assert(IRFog.getCellChannels(1, 2) == IRFog.Channel.DEFAULT)
        assert(IRFog.exploreRadius(0, 0, 3) == 0)
        assert(IRFog.exploreRadius(0, 0, 3, 2) == 0)
        IRFog.revealRadius(0, 0, 3, 2)
    )lua"));
}

TEST_F(LuaFogBindingsTest, ExploredPolicyServicesRejectInvalidArgumentsWithNamedErrors) {
    constexpr const char *kBadCalls[] = {
        "IRFog.setExploredPolicy()",
        "IRFog.setExploredPolicy(IRFog.ExploredPolicy.DECAY)",
        "IRFog.setExploredPolicy(IRFog.ExploredPolicy.DECAY, 1, 1, 1)",
        "IRFog.setExploredPolicy('DECAY', 1000)",
        "IRFog.setExploredPolicy(2, 1000)",
        "IRFog.setExploredPolicy(0.5, 1000)",
        "IRFog.setExploredPolicy(IRFog.ExploredPolicy.DECAY, 0)",
        "IRFog.setExploredPolicy(IRFog.ExploredPolicy.DECAY, -1)",
        "IRFog.setExploredPolicy(IRFog.ExploredPolicy.DECAY, 1.5)",
        "IRFog.setExploredPolicy(IRFog.ExploredPolicy.DECAY, 0/0)",
        "IRFog.setExploredPolicy(IRFog.ExploredPolicy.DECAY, math.huge)",
        "IRFog.setExploredPolicy(IRFog.ExploredPolicy.DECAY, 2^53)",
        "IRFog.setExploredPolicy(IRFog.ExploredPolicy.DECAY, 1000, -1)",
        "IRFog.setExploredPolicy(IRFog.ExploredPolicy.DECAY, 1000, 2^32)",
        "IRFog.setExploredPolicy(IRFog.ExploredPolicy.DECAY, 1000, 'all')",
        "IRFog.getExploredPolicy(1)",
        "IRFog.setExploredTimeMs()",
        "IRFog.setExploredTimeMs(1, 2)",
        "IRFog.setExploredTimeMs(-1)",
        "IRFog.setExploredTimeMs(0.25)",
        "IRFog.setExploredTimeMs(0/0)",
        "IRFog.setExploredTimeMs(-math.huge)",
        "IRFog.setExploredTimeMs(2^53)",
        "IRFog.setExploredTimeMs('now')",
        "IRFog.getExploredTimeMs(0)",
        "IRFog.setCellChannels(0, 0)",
        "IRFog.setCellChannels(0, 0, 1, 1)",
        "IRFog.setCellChannels(0, 0, -1)",
        "IRFog.setCellChannels(0, 0, 2^32)",
        "IRFog.setCellChannels(0, 0, 1.5)",
        "IRFog.setCellChannels(0.5, 0, 1)",
        "IRFog.getCellChannels(0)",
        "IRFog.getCellChannels(0, 0, 0)",
        "IRFog.getCellChannels('0', 0)",
        "IRFog.exploreRadius(0, 0)",
        "IRFog.exploreRadius(0, 0, 1, 1, 1)",
        "IRFog.exploreRadius(2147483647, 0, 1)",
        "IRFog.exploreRadius(0, 0, 1, -1)",
        "IRFog.exploreRadius(0, 0, 1, 2^32)",
        "IRFog.exploreRadius(0, 0, '1')",
    };
    expectScriptsFail(kBadCalls);
    expectScriptFailsWith("IRFog.setExploredPolicy(2, 1000)", "IRFog.setExploredPolicy argument 1");
    expectScriptFailsWith(
        "IRFog.setExploredPolicy(IRFog.ExploredPolicy.DECAY, 2^53)",
        "IRFog.setExploredPolicy argument 2"
    );
    expectScriptFailsWith(
        "IRFog.setExploredPolicy(IRFog.ExploredPolicy.DECAY, 0)",
        "IRFog.setExploredPolicy argument 2 must be positive for DECAY"
    );
    expectScriptFailsWith("IRFog.setExploredTimeMs(0.25)", "IRFog.setExploredTimeMs argument 1");
    expectScriptFailsWith("IRFog.setCellChannels(0, 0, 2^32)", "IRFog.setCellChannels argument 3");
    expectScriptFailsWith(
        "IRFog.exploreRadius(2147483647, 0, 1)",
        "IRFog.exploreRadius arguments overflow"
    );
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

// Without an active canvas the ceiling and treatment entries validate, then
// no-op; the getters read the disabled defaults.
TEST_F(LuaFogBindingsTest, CeilingAndTreatmentEntriesDefaultWithoutACanvas) {
    EXPECT_TRUE(scriptSucceeds(R"lua(
        IRFog.setVisionCeiling(0, 2, 1)
        local height, fade = IRFog.getVisionCeiling(0)
        assert(height == -1 and fade == 0)
        IRFog.setRevealSurfaceTreatment(0.5, 0.5)
        IRFog.clearRevealSurfaceTreatment()
        local on, density, tone = IRFog.getRevealSurfaceTreatment()
        assert(on == false and density == 0 and math.abs(tone - 0.85) < 1e-6)
    )lua"));
    expectScriptFailsWith(
        "IRFog.setVisionCeiling(0, 1, -1)",
        "IRFog.setVisionCeiling argument 3 must not be negative"
    );
    expectScriptFailsWith(
        "IRFog.setRevealSurfaceTreatment(2)",
        "IRFog.setRevealSurfaceTreatment argument 1 must be in [0, 1]"
    );
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
        "IRFog.setVision(1, 2, 3, 4, 5, 6, 7, 8, 9, 10)",
        "IRFog.addVision('1', 2, 3)",
        "IRFog.addVision(1, 2)",
        "IRFog.addVision(1, 2, 3, 4, 5, 6, 7, 8, 9, 10)",
        "IRFog.clearVisions(1)",
        "IRFog.evalReveal(0, 0)",
        "IRFog.evalReveal(0, false, 0)",
        "IRFog.evalReveal(0, 0, 0, 0, 0)",
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
        "IRFog.revealRadius(0, 0, 1, 0, 0)",
        "IRFog.revealRadius(0, 0, 1, -1)",
        "IRFog.revealRadius(0, 0, 1, 1.5)",
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
        "IRFog.addVision(0, 0, 2, nil, nil, nil, nil, nil, 'party')",
        "IRFog.addVision(0, 0, 2, nil, nil, nil, nil, nil, -1)",
        "IRFog.evalReveal(0, 0, 0, 1.5)",
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

TEST_F(LuaFogVisionSlotsTest, VisionAndVerdictChannelArgumentsReachTheOracle) {
    ASSERT_TRUE(m_lua.lua()
                    .safe_script(R"lua(
        assert(IRFog.addVision(3, 4, 6, nil, nil, nil, nil, nil, 2) == 0)
    )lua")
                    .valid());
    EXPECT_EQ(m_observers.channels(0), 2u);
    EXPECT_FLOAT_EQ(
        IRPrefab::Fog::evalReveal(
            m_observers,
            {},
            IRComponents::kFogStateUnexplored,
            IRMath::vec3(3.0f, 4.0f, 0.0f),
            1u
        ),
        0.0f
    );
    EXPECT_FLOAT_EQ(
        IRPrefab::Fog::evalReveal(
            m_observers,
            {},
            IRComponents::kFogStateUnexplored,
            IRMath::vec3(3.0f, 4.0f, 0.0f),
            3u
        ),
        1.0f
    );
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

TEST_F(LuaFogVisionSlotsTest, CeilingEntrySetsAndReadsTheSlot) {
    ASSERT_TRUE(m_lua.lua()
                    .safe_script(R"lua(
        IRFog.addVision(0, 0, 5)
        IRFog.addVision(0, 0, 5)
        IRFog.setVisionCeiling(1, 3.5, 1.25)
        local height, fade = IRFog.getVisionCeiling(1)
        assert(height == 3.5 and fade == 1.25)
        local height0, fade0 = IRFog.getVisionCeiling(0)
        assert(height0 == -1 and fade0 == 0, 'a fresh slot starts disabled')
        IRFog.setVisionCeiling(0, 2)
        assert(select(2, IRFog.getVisionCeiling(0)) == 0, 'the fade defaults to a hard plane')
    )lua")
                    .valid());
    EXPECT_EQ(m_observers.visionCircleCeilings_[1], IRMath::vec4(3.5f, 1.25f, 0.0f, 0.0f));
    EXPECT_EQ(m_observers.visionCircleCeilings_[0], IRMath::vec4(2.0f, 0.0f, 0.0f, 0.0f));

    ASSERT_TRUE(m_lua.lua().safe_script("IRFog.setVisionCeiling(1, -1)").valid());
    EXPECT_FALSE(m_observers.ceilingEnabled(1)) << "a negative height disables the ceiling";
    EXPECT_FLOAT_EQ(m_observers.fadeHeight(1), 0.0f);

    ASSERT_TRUE(m_lua.lua().safe_script("IRFog.clearVisions()").valid());
    EXPECT_FALSE(m_observers.ceilingEnabled(0)) << "clearing the sources resets every ceiling";
}

TEST_F(LuaFogVisionSlotsTest, CeilingEntryRejectsInvalidArgumentsWithoutMutating) {
    ASSERT_TRUE(m_lua.lua()
                    .safe_script(R"lua(
        IRFog.addVision(0, 0, 5)
        IRFog.setVisionCeiling(0, 3, 1)
    )lua")
                    .valid());
    for (const auto &[source, expected] :
         {std::pair{"IRFog.setVisionCeiling(1, 1)", "IRFog.setVisionCeiling argument 1"},
          std::pair{"IRFog.setVisionCeiling(-1, 1)", "IRFog.setVisionCeiling argument 1"},
          std::pair{"IRFog.setVisionCeiling(0, 'high')", "argument 2 must be a number"},
          std::pair{"IRFog.setVisionCeiling(0, 0/0)", "argument 2 must be finite"},
          std::pair{"IRFog.setVisionCeiling(0, math.huge)", "argument 2 must be finite"},
          std::pair{"IRFog.setVisionCeiling(0, 2, -0.5)", "argument 3 must not be negative"},
          std::pair{"IRFog.setVisionCeiling(0, 2, math.huge)", "argument 3 must be finite"},
          std::pair{"IRFog.setVisionCeiling(0)", "expects 2 to 3 arguments"},
          std::pair{"IRFog.getVisionCeiling(3)", "IRFog.getVisionCeiling argument 1"},
          std::pair{"IRFog.getVisionCeiling()", "expects 1 arguments"}}) {
        const std::string error = scriptError(source);
        EXPECT_NE(error.find(expected), std::string::npos) << source << ": " << error;
    }
    EXPECT_EQ(m_observers.visionCircleCeilings_[0], IRMath::vec4(3.0f, 1.0f, 0.0f, 0.0f))
        << "a rejected call must leave the ceiling untouched";
}

TEST_F(LuaFogVisionSlotsTest, TreatmentEntriesRoundTripClearAndReject) {
    ASSERT_TRUE(m_lua.lua()
                    .safe_script(R"lua(
        local on, density, tone = IRFog.getRevealSurfaceTreatment()
        assert(on == false and density == 0 and math.abs(tone - 0.85) < 1e-6)
        IRFog.setRevealSurfaceTreatment(0.5, 0.6)
        on, density, tone = IRFog.getRevealSurfaceTreatment()
        assert(on == true and density == 0.5 and math.abs(tone - 0.6) < 1e-6)
        IRFog.clearRevealSurfaceTreatment()
        on, density, tone = IRFog.getRevealSurfaceTreatment()
        assert(on == false and density == 0.5, 'clearing keeps the stored style')
        IRFog.setRevealSurfaceTreatment(0.25)
        on, density, tone = IRFog.getRevealSurfaceTreatment()
        assert(on == true and density == 0.25 and math.abs(tone - 0.85) < 1e-6)
    )lua")
                    .valid());
    EXPECT_EQ(
        m_observers.revealSurfaceTreatment_,
        IRMath::vec4(1.0f, 0.25f, IRComponents::kFogCutTone, 0.0f)
    );
    for (const auto &[source, expected] :
         {std::pair{
              "IRFog.setRevealSurfaceTreatment(1.5)",
              "IRFog.setRevealSurfaceTreatment argument 1 must be in [0, 1]"
          },
          std::pair{"IRFog.setRevealSurfaceTreatment(-0.1)", "argument 1 must be in [0, 1]"},
          std::pair{"IRFog.setRevealSurfaceTreatment(0.5, 2)", "argument 2 must be in [0, 1]"},
          std::pair{"IRFog.setRevealSurfaceTreatment(0/0)", "argument 1 must be finite"},
          std::pair{"IRFog.setRevealSurfaceTreatment('x')", "argument 1 must be a number"},
          std::pair{"IRFog.setRevealSurfaceTreatment(0.5, 'x')", "argument 2 must be a number"},
          std::pair{"IRFog.setRevealSurfaceTreatment()", "expects 1 to 2 arguments"},
          std::pair{"IRFog.getRevealSurfaceTreatment(1)", "expects 0 arguments"},
          std::pair{"IRFog.clearRevealSurfaceTreatment(1)", "expects 0 arguments"}}) {
        const std::string error = scriptError(source);
        EXPECT_NE(error.find(expected), std::string::npos) << source << ": " << error;
    }
    EXPECT_EQ(
        m_observers.revealSurfaceTreatment_,
        IRMath::vec4(1.0f, 0.25f, IRComponents::kFogCutTone, 0.0f)
    ) << "a rejected call must leave the treatment untouched";
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

TEST(LuaFogPipelineTest, RevealSystemsResolveFromALuaPipeline) {
    IRScript::LuaScript lua;
    IREntity::EntityManager entityManager;
    IRSystem::SystemManager systemManager;
    lua.bindLuaDrivenEcs();
    lua.registerPrefabSystems<
        IRSystem::FOG_SUBJECT_EXEMPT,
        IRSystem::FOG_SUBJECT_EXEMPT_SHAPE,
        IRSystem::FOG_SUBJECT_EXEMPT_CANVAS,
        IRSystem::FOG_SUBJECT_ADOPT,
        IRSystem::FOG_SUBJECT_ADOPT_SHAPE,
        IRSystem::FOG_SUBJECT_ADOPT_CANVAS,
        IRSystem::FOG_REVEAL_EVAL,
        IRSystem::FOG_REVEAL_EVAL_SHAPE,
        IRSystem::FOG_REVEAL_EVAL_CANVAS>();
    sol::protected_function_result result = lua.lua().safe_script(
        R"lua(
        local systems = {
            IRSystem.systemId(IRSystem.SystemName.FOG_SUBJECT_EXEMPT),
            IRSystem.systemId(IRSystem.SystemName.FOG_SUBJECT_EXEMPT_SHAPE),
            IRSystem.systemId(IRSystem.SystemName.FOG_SUBJECT_EXEMPT_CANVAS),
            IRSystem.systemId(IRSystem.SystemName.FOG_SUBJECT_ADOPT),
            IRSystem.systemId(IRSystem.SystemName.FOG_SUBJECT_ADOPT_SHAPE),
            IRSystem.systemId(IRSystem.SystemName.FOG_SUBJECT_ADOPT_CANVAS),
            IRSystem.systemId(IRSystem.SystemName.FOG_REVEAL_EVAL),
            IRSystem.systemId(IRSystem.SystemName.FOG_REVEAL_EVAL_SHAPE),
            IRSystem.systemId(IRSystem.SystemName.FOG_REVEAL_EVAL_CANVAS),
        }
        IRSystem.registerPipeline(IRTime.UPDATE, systems)
        return #systems
    )lua",
        sol::script_pass_on_error
    );
    ASSERT_TRUE(result.valid()) << sol::error{result}.what();
    EXPECT_EQ(result.get<lua_Integer>(), 9);
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

// The new services drive a real headless field: exploration, expiry on the
// Lua-advanced clock, the bit-31 mask round trip, and the admission masks on
// the disc services, read back through both Lua and C++.
TEST_F(LuaFogBindingsActiveCanvasTest, ExploredPolicyServicesDriveTheField) {
    EXPECT_TRUE(scriptSucceeds(R"lua(
        local policy, duration, channels = IRFog.getExploredPolicy()
        assert(policy == IRFog.ExploredPolicy.PERSISTENT and duration == 0 and channels == 1)
        IRFog.setExploredPolicy(IRFog.ExploredPolicy.DECAY, 1000, 3)
        policy, duration, channels = IRFog.getExploredPolicy()
        assert(policy == IRFog.ExploredPolicy.DECAY and duration == 1000 and channels == 3)
        assert(IRFog.getExploredTimeMs() == 0)
        IRFog.setExploredTimeMs(250)
        assert(IRFog.getExploredTimeMs() == 250)

        IRFog.setCell(5, 5, IRFog.State.EXPLORED)
        IRFog.setCellChannels(6, 6, 2^31)
        assert(IRFog.getCellChannels(6, 6) == 2^31)
        assert(IRFog.getCellChannels(7, 7) == IRFog.Channel.DEFAULT)
        IRFog.setCellChannels(8, 8, 2)
        IRFog.setCell(8, 8, IRFog.State.EXPLORED)
        IRFog.setCellChannels(9, 9, 4)
        IRFog.setCell(9, 9, IRFog.State.EXPLORED)
        -- A disjoint source skips the default cell; a matching one admits it.
        IRFog.revealRadius(20, 20, 0, 2)
        assert(IRFog.getCell(20, 20) == IRFog.State.UNEXPLORED)
        IRFog.revealRadius(20, 20, 0)
        assert(IRFog.getCell(20, 20) == IRFog.State.VISIBLE)
        assert(IRFog.exploreRadius(30, 30, 1) == 5)
        assert(IRFog.exploreRadius(30, 30, 1) == 0)
        assert(IRFog.getCell(30, 30) == IRFog.State.EXPLORED)

        IRFog.setExploredTimeMs(1249)
        assert(IRFog.getCell(5, 5) == IRFog.State.EXPLORED)
        assert(IRFog.getCell(30, 30) == IRFog.State.EXPLORED, 'explored at 250, due at 1250')
        IRFog.setExploredTimeMs(1250)
        assert(IRFog.getCell(5, 5) == IRFog.State.UNEXPLORED)
        assert(IRFog.getCell(8, 8) == IRFog.State.UNEXPLORED, 'bit 1 is in the policy mask')
        assert(IRFog.getCell(9, 9) == IRFog.State.EXPLORED, 'bit 2 is not')
        assert(IRFog.getCell(30, 30) == IRFog.State.UNEXPLORED)

        -- Rejected calls leave the field untouched.
        assert(not pcall(IRFog.setExploredTimeMs, 1000))
        assert(IRFog.getExploredTimeMs() == 1250)
        assert(not pcall(IRFog.setExploredPolicy, IRFog.ExploredPolicy.PERSISTENT, 0))
        assert(not pcall(IRFog.setExploredPolicy, IRFog.ExploredPolicy.DECAY, 2000, 3))
        policy, duration, channels = IRFog.getExploredPolicy()
        assert(policy == IRFog.ExploredPolicy.DECAY and duration == 1000 and channels == 3)
        IRFog.setExploredPolicy(IRFog.ExploredPolicy.DECAY, 1000, 3)
        IRFog.setExploredTimeMs(1250)
    )lua"));
    const auto &fog = IREntity::getComponent<IRComponents::C_CanvasFogOfWar>(m_canvas);
    EXPECT_EQ(fog.getExploredTimeMs(), 1250u);
    EXPECT_EQ(fog.getExploredPolicy().policy_, IRPrefab::Fog::ExploredPolicy::DECAY);
    EXPECT_EQ(fog.getExploredPolicy().durationMs_, 1000u);
    EXPECT_EQ(fog.getExploredPolicy().channels_, 3u);
    EXPECT_EQ(fog.getCellChannels(6, 6), 1u << 31u);
    EXPECT_EQ(fog.getCell(5, 5), IRComponents::kFogStateUnexplored);
    EXPECT_EQ(fog.getCell(9, 9), IRComponents::kFogStateExplored);
    EXPECT_EQ(fog.getCell(30, 30), IRComponents::kFogStateUnexplored);
    EXPECT_EQ(fog.getCell(20, 20), IRComponents::kFogStateVisible);
    EXPECT_EQ(fog.field_->stats().expired_, 7) << "(5,5), (8,8) and the five-cell disc";
}

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
        IRFog.setVision(3, 4, 6, nil, nil, nil, nil, nil, 2)
        assert(IRFog.evalReveal(3, 4, 0, 1) == 0)
        assert(IRFog.evalReveal(3, 4, 0, 3) == 1)
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
