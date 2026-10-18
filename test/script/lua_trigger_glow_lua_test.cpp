#include <gtest/gtest.h>

#include <irreden/ir_entity.hpp>
#include <irreden/ir_math.hpp>
#include <irreden/ir_system.hpp>
#include <irreden/ir_time.hpp>

#include <irreden/update/components/component_contact_event.hpp>
#include <irreden/update/components/component_trigger_glow.hpp>
#include <irreden/update/components/component_trigger_glow_lua.hpp>
#include <irreden/update/components/component_anim_color_state.hpp>
#include <irreden/update/components/component_anim_color_state_lua.hpp>
#include <irreden/update/systems/system_contact_trigger_glow.hpp>
#include <irreden/voxel/components/component_voxel_pool.hpp>
#include <irreden/voxel/components/component_voxel_set.hpp>

#include <irreden/script/lua_script.hpp>

namespace {

using IRComponents::C_AnimColorState;
using IRComponents::C_ContactEvent;
using IRComponents::C_TriggerGlow;
using IRComponents::C_VoxelPool;
using IRComponents::C_VoxelSetNew;
using IRMath::Color;
using IRMath::ivec3;

// The idle glow repaints its voxels from the base colour it captured on its
// first tick. A script that recolours a glowing entity writes the base colour
// through the binding, or that capture paints the old colour back.
class TriggerGlowLuaTest : public testing::Test {
  protected:
    TriggerGlowLuaTest()
        : m_lua{}
        , m_entity_manager{}
        , m_system_manager{} {
        m_lua.bindLuaDrivenEcs();
        m_lua.registerType<Color, Color(int, int, int, int)>(
            "Color",
            "r",
            &Color::red_,
            "g",
            &Color::green_,
            "b",
            &Color::blue_,
            "a",
            &Color::alpha_
        );
        m_lua.registerTypeFromTraits<C_TriggerGlow>();
        m_lua.registerTypeFromTraits<C_AnimColorState>();
    }

    static bool sameColor(Color a, Color b) {
        return a.red_ == b.red_ && a.green_ == b.green_ && a.blue_ == b.blue_ &&
               a.alpha_ == b.alpha_;
    }

    IRScript::LuaScript m_lua;
    IREntity::EntityManager m_entity_manager;
    IRSystem::SystemManager m_system_manager;
};

TEST_F(TriggerGlowLuaTest, LuaWrittenBaseColorIsWhatTheIdleGlowPaints) {
    auto &lua = m_lua.lua();
    const Color red{200, 40, 40, 255};
    const Color teal{10, 120, 130, 255};

    const auto canvas = IREntity::createEntity(C_VoxelPool{ivec3(16, 16, 16)});
    const auto id = IREntity::createEntity(
        C_ContactEvent{},
        C_TriggerGlow{
            Color{255, 255, 255, 255},
            0.03f,
            0.2f,
            IRMath::IREasingFunctions::kCubicEaseOut
        },
        C_VoxelSetNew{ivec3(2, 2, 2), red, false, canvas}
    );

    // Reads the base before (when asked) writing a new one; runs before the
    // glow system, so a read sees the previous tick's capture.
    lua["baseR"] = -1;
    lua["readR"] = -1;
    lua["readInit"] = false;
    auto registerResult = lua.safe_script(
        R"(
        return IRSystem.registerSystem({
            name = 'RecolorGlowBase',
            components = { IRComponent.C_TriggerGlow },
            tick = function(arch)
                for i = 0, arch.length - 1 do
                    local glow = arch.C_TriggerGlow:at(i)
                    readR, readInit = glow.baseColor.r, glow.baseColorInitialized
                    if baseR >= 0 then
                        glow.baseColor = Color.new(baseR, 120, 130, 255)
                        glow.baseColorInitialized = true
                    end
                end
            end,
        })
    )",
        sol::script_pass_on_error
    );
    ASSERT_TRUE(registerResult.valid()) << sol::error{registerResult}.what();
    const IRSystem::SystemId luaSysId = registerResult.get<lua_Integer>();
    m_system_manager.registerPipeline(
        IRTime::Events::UPDATE,
        {luaSysId, IRSystem::createSystem<IRSystem::CONTACT_TRIGGER_GLOW>()}
    );

    // The first tick captures the voxels' colour as the base.
    m_system_manager.executePipeline(IRTime::Events::UPDATE);
    const auto &glow = IREntity::getComponent<C_TriggerGlow>(id);
    EXPECT_TRUE(glow.baseColorInitialized_);
    EXPECT_TRUE(sameColor(glow.baseColor_, red));

    // Recolouring the voxels alone does not hold: the idle glow paints the
    // captured base back on its next tick. Lua reads that capture.
    IREntity::getComponent<C_VoxelSetNew>(id).changeVoxelColorAll(teal);
    m_system_manager.executePipeline(IRTime::Events::UPDATE);
    EXPECT_TRUE(sameColor(IREntity::getComponent<C_VoxelSetNew>(id).voxels_[0].color_, red));
    EXPECT_EQ(lua["readR"].get<int>(), 200);
    EXPECT_TRUE(lua["readInit"].get<bool>());

    // Written through the binding, the new base is what the glow paints.
    lua["baseR"] = 10;
    m_system_manager.executePipeline(IRTime::Events::UPDATE);
    EXPECT_TRUE(sameColor(glow.baseColor_, teal));
    EXPECT_TRUE(sameColor(IREntity::getComponent<C_VoxelSetNew>(id).voxels_[0].color_, teal));
}

// The same cache on the colour state the spring and clip colour systems
// paint from: a script sets the base (and the current colour) through the
// binding, and the capture flag, when it recolours the voxels underneath.
TEST_F(TriggerGlowLuaTest, LuaWritesTheColorStateBaseAndCurrent) {
    auto &lua = m_lua.lua();
    const auto id =
        IREntity::createEntity(C_AnimColorState{IRComponents::ANIM_COLOR_BLEND_REPLACE});

    auto registerResult = lua.safe_script(
        R"(
        return IRSystem.registerSystem({
            name = 'RecolorState',
            components = { IRComponent.C_AnimColorState },
            tick = function(arch)
                for i = 0, arch.length - 1 do
                    local st = arch.C_AnimColorState:at(i)
                    readInit = st.baseInitialized
                    st.baseColor = Color.new(10, 120, 130, 255)
                    st.currentColor = Color.new(10, 120, 130, 255)
                    st.baseInitialized = true
                end
            end,
        })
    )",
        sol::script_pass_on_error
    );
    ASSERT_TRUE(registerResult.valid()) << sol::error{registerResult}.what();
    m_system_manager.registerPipeline(IRTime::Events::UPDATE, {registerResult.get<lua_Integer>()});
    m_system_manager.executePipeline(IRTime::Events::UPDATE);

    const auto &state = IREntity::getComponent<C_AnimColorState>(id);
    EXPECT_FALSE(lua["readInit"].get<bool>());
    EXPECT_TRUE(state.baseInitialized_);
    EXPECT_TRUE(sameColor(state.baseColor_, Color{10, 120, 130, 255}));
    EXPECT_TRUE(sameColor(state.currentColor_, Color{10, 120, 130, 255}));
}

} // namespace
