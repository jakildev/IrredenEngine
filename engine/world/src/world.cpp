/*
 * Project: Irreden Engine
 * File: ir_world.cpp
 * Author: Evin Killian jakildev@gmail.com
 * Created Date: October 2023
 * -----
 * Modified By: <your_name> <Month> <YYYY>
 */

#include <irreden/ir_profile.hpp>
#include <irreden/ir_render.hpp>
#include <irreden/ir_system.hpp>

#include <irreden/world.hpp>

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


    void World::setupLuaBindings(
        const std::vector<LuaBindingRegistration>& bindings
    ) {
        for(const auto& bind : bindings) {
            bind(m_lua);
        }
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