/*
 * Project: Irreden Engine
 * File: ir_world.cpp
 * Author: Evin Killian jakildev@gmail.com
 * Created Date: October 2023
 * -----
 * Modified By: <your_name> <Month> <YYYY>
 */

#include <irreden/ir_profile.hpp>
#include <irreden/ir_constants.hpp>
#include <irreden/ir_render.hpp>
#include <irreden/ir_system.hpp>

#include <irreden/world.hpp>

#include <irreden/voxel/components/component_voxel_set.hpp>
#include <irreden/update/components/component_goto_easing_3d.hpp>
#include <irreden/update/components/component_velocity_3d.hpp>
#include <irreden/update/components/component_periodic_idle.hpp>


using namespace IRComponents;
using namespace IRConstants;


namespace IREngine {
//TODO: replace initalization constants with config file.

    World::World(const char* configFileName)
    :   m_worldConfig{configFileName}
    ,   m_IRGLFWWindow{
            ivec2(
                m_worldConfig["init_window_width"].get_integer(),
                m_worldConfig["init_window_height"].get_integer()
            ),
            m_worldConfig["fullscreen"].get_boolean()
        }
    ,   m_entityManager{}
    ,   m_commandManager{}
    ,   m_systemManager{}
    ,   m_inputManager{}
    ,   m_renderingResourceManager{}
    ,   m_renderer{
            ivec2(
                m_worldConfig["game_resolution_width"].get_integer(),
                m_worldConfig["game_resolution_height"].get_integer()
            ),
            static_cast<IRRender::FitMode>(m_worldConfig["fit_mode"].get_enum())
    }
    ,   m_audioManager{}
    ,   m_timeManager{}
    ,   m_lua{}
    {
        IRRender::ImageData icon{
            "data/images/irreden_engine_logo_v6_alpha.png"
        };
        GLFWimage iconGlfw{
            icon.width_,
            icon.height_,
            icon.data_
        };
        m_IRGLFWWindow.setWindowIcon(
            &iconGlfw
        );
        m_renderer.printRenderInfo();
        IR_PROFILE_MAIN_THREAD;

        IRE_LOG_INFO("Initalized game world");

    }

    World::~World() {

    }


    void World::setupLuaBindings() {
        // Seperate out into ir_<module_name>_lua.hpp files or something like that.
        // Regular types

        // IRMath ------------------------------
        m_lua.registerType<Color, Color(int, int, int, int)>("Color",
            "r", &Color::red_,
            "g", &Color::green_,
            "b", &Color::blue_,
            "a", &Color::alpha_
        );
        m_lua.registerType<ivec3, ivec3(int, int, int)>("ivec3",
            "x", &ivec3::x,
            "y", &ivec3::y,
            "z", &ivec3::z
        );
        auto vec3_type = m_lua.registerType<vec3, vec3(float, float, float)>("vec3",
            "x", &vec3::x,
            "y", &vec3::y,
            "z", &vec3::z
        );

        // TODO: Make this in engine, not sol
        vec3_type[sol::meta_function::addition] = [](const vec3& a, const vec3& b) {
            return a + b;
        };
        vec3_type[sol::meta_function::subtraction] = [](const vec3& a, const vec3& b) {
            return a - b;
        };

        m_lua.registerEnum<IREasingFunctions>("IREasingFunction",
            {
                {"LINEAR_INTERPOLATION", kLinearInterpolation},
                {"QUADRATIC_EASE_IN", kQuadraticEaseIn},
                {"QUADRATIC_EASE_OUT", kQuadraticEaseOut},
                {"QUADRATIC_EASE_IN_OUT", kQuadraticEaseInOut},
                {"CUBIC_EASE_IN", kCubicEaseIn},
                {"CUBIC_EASE_OUT", kCubicEaseOut},
                {"CUBIC_EASE_IN_OUT", kCubicEaseInOut},
                {"QUARTIC_EASE_IN", kQuarticEaseIn},
                {"QUARTIC_EASE_OUT", kQuarticEaseOut},
                {"QUARTIC_EASE_IN_OUT", kQuarticEaseInOut},
                {"QUINTIC_EASE_IN", kQuinticEaseIn},
                {"QUINTIC_EASE_OUT", kQuinticEaseOut},
                {"QUINTIC_EASE_IN_OUT", kQuinticEaseInOut},
                {"SINE_EASE_IN", kSineEaseIn},
                {"SINE_EASE_OUT", kSineEaseOut},
                {"SINE_EASE_IN_OUT", kSineEaseInOut},
                {"CIRCULAR_EASE_IN", kCircularEaseIn},
                {"CIRCULAR_EASE_OUT", kCircularEaseOut},
                {"CIRCULAR_EASE_IN_OUT", kCircularEaseInOut},
                {"EXPONENTIAL_EASE_IN", kExponentialEaseIn},
                {"EXPONENTIAL_EASE_OUT", kExponentialEaseOut},
                {"EXPONENTIAL_EASE_IN_OUT", kExponentialEaseInOut},
                {"ELASTIC_EASE_IN", kElasticEaseIn},
                {"ELASTIC_EASE_OUT", kElasticEaseOut},
                {"ELASTIC_EASE_IN_OUT", kElasticEaseInOut},
                {"BACK_EASE_IN", kBackEaseIn},
                {"BACK_EASE_OUT", kBackEaseOut},
                {"BACK_EASE_IN_OUT", kBackEaseInOut},
                {"BOUNCE_EASE_IN", kBounceEaseIn},
                {"BOUNCE_EASE_OUT", kBounceEaseOut},
                {"BOUNCE_EASE_IN_OUT", kBounceEaseInOut}
            }
        );


        // Components -----------------------------------------
        m_lua.registerType<
            C_Position3D,
            C_Position3D(float, float, float),
            C_Position3D(vec3)
        >("C_Position3D",
            "x", [](C_Position3D& obj) { return obj.pos_.x; },
            "y", [](C_Position3D& obj) { return obj.pos_.y; },
            "z", [](C_Position3D& obj) { return obj.pos_.z; }
        );
        m_lua.registerType<C_Velocity3D, float, float, float>("C_Velocity3D");
        m_lua.registerType<C_VoxelSetNew, C_VoxelSetNew(ivec3, Color)>("C_VoxelSetNew");

        // TODO: Use optional for arguments with default value
        m_lua.registerType<PeriodStage, PeriodStage(float, float, float, float, IREasingFunctions, bool)>(
            "PeriodStage"
        );
        m_lua.registerType<C_PeriodicIdle, C_PeriodicIdle(float, float, float)>("C_PeriodicIdle",
            "addStageDurationSeconds", &C_PeriodicIdle::addStageDurationSeconds
        );
        m_lua.registerType<IRScript::LuaEntity, IRScript::LuaEntity(EntityId)>("LuaEntity",
            "entity", [](IRScript::LuaEntity& obj) { return obj.entity; }
        );

        // m_lua.registerFunctionEntityBatch<C_Position3D, C_VoxelSetNew>("createEntityBatchVoxelStatic");
        // m_lua.registerType<C_Lifetime, int>("C_Lifetime");
        // m_lua.registerType<C_GotoEasing3D, C_Position3D, C_Position3D, float, IREasingFunctions>("C_GotoEasing3D");

        // IREntity --------------------------------------------------
        m_lua.lua()["IREntity"] = m_lua.lua().create_table(); // should be handled at lua module level
        m_lua.registerType<IREntity::CreateEntityCallbackParams, IREntity::CreateEntityCallbackParams(ivec3, vec3)>(
            "CreateEntityCallbackParams",
            "center", &IREntity::CreateEntityCallbackParams::center,
            "index", &IREntity::CreateEntityCallbackParams::index
        );

        // auto createEntityBatchVoxelStatic = [this](
        //     IRMath::ivec3 partitions,
        //     sol::protected_function posFunc,
        //     sol::protected_function voxelFunc
        // )
        // {
        //     std::vector<EntityId> entities = createEntityBatchWithFunctions(
        //         partitions,
        //         IRScript::wrapLuaFunction<C_Position3D>(posFunc),
        //         IRScript::wrapLuaFunction<C_VoxelSetNew>(voxelFunc)
        //     );
        //     std::vector<IRScript::LuaEntity> luaEntities;
        //     luaEntities.resize(entities.size());
        //     for (int i = 0; i < entities.size(); i++)
        //     {
        //         luaEntities[i].entity = entities[i];
        //     }
        //     return luaEntities;
        // };
        // m_lua.registerCreateEntityBatchFunction<C_Position3D, C_VoxelSetNew>(
        //     "createEntityBatchVoxelStatic"
        // );

        m_lua.registerCreateEntityBatchFunction<
            C_Position3D, C_VoxelSetNew, C_PeriodicIdle>(
                "createEntityBatchVoxelPeriodicIdle"
            );

    }

    void World::runScript(const char* fileName) {
        m_lua.scriptFile(fileName);
    }

    // rename event loop;
    void World::gameLoop() {
        // init();
        start();
        while (!m_IRGLFWWindow.shouldClose())
        {
            m_timeManager.beginMainLoop();

            while (m_timeManager.shouldUpdate())
            {
                m_IRGLFWWindow.pollEvents();
                input();
                update();
                // output();
            }
            render();
            // TODO: Set frame caps
        }
        // cleanup();
        end();
    }

    void World::input() {
        IR_PROFILE_FUNCTION(IR_PROFILER_COLOR_UPDATE);

        // TODO: Make this an event from timeManager after
        // making that more generic.
        m_inputManager.tick();
        m_audioManager.getMidiIn().tick();

        m_systemManager.executePipeline(IRTime::Events::INPUT);
    }

    void World::start() {
        m_timeManager.start();
        IRProfile::CPUProfiler::instance().mainThread();
    }

    void World::end() {

    }

    void World::update()
    {
        m_timeManager.beginEvent<IRTime::UPDATE>();
        IR_PROFILE_FUNCTION(IR_PROFILER_COLOR_UPDATE);

        m_commandManager.executeUserKeyboardCommandsAll();
        m_commandManager.executeDeviceMidiCCCommandsAll();
        m_commandManager.executeDeviceMidiNoteCommandsAll();
        m_systemManager.executePipeline(IRTime::Events::UPDATE);
        // Destroy all marked entities in one step
        m_entityManager.destroyMarkedEntities();
        // TODO: maybe component adds and removes should be done here too
        m_timeManager.endEvent<IRTime::UPDATE>();
    }

    void World::render()
    {
        // Possible oppertunity for promise style await here...
        m_timeManager.beginEvent<IRTime::RENDER>();
        IR_PROFILE_FUNCTION(IR_PROFILER_COLOR_RENDER);

        m_inputManager.tickRender();
        m_renderer.tick();

        m_timeManager.endEvent<IRTime::RENDER>();
    }

} // namespace IREngine