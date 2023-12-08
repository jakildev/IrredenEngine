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

#include <irreden/world.hpp>
#include <irreden/audio/systems/system_audio_midi_message_in.hpp>
#include <irreden/audio/systems/system_audio_midi_message_out.hpp>

#include <irreden/input/systems/system_input_key_mouse.hpp>
#include <irreden/input/systems/system_input_gamepad.hpp>

#include <irreden/voxel/systems/system_voxel_pool.hpp>
#include <irreden/update/systems/system_update_screen_view.hpp>
#include <irreden/update/systems/system_velocity.hpp>
#include <irreden/update/systems/system_acceleration.hpp>
#include <irreden/update/systems/system_gravity.hpp>
#include <irreden/update/systems/system_periodic_idle.hpp>
#include <irreden/update/systems/system_goto_3d.hpp>
#include <irreden/voxel/systems/system_update_voxel_set_children.hpp>
#include <irreden/update/systems/system_lifetime.hpp>
#include <irreden/update/systems/system_particle_spawner.hpp>
#include <irreden/update/systems/system_update_positions_global.hpp>

#include <irreden/render/systems/system_texture_scroll.hpp>
#include <irreden/render/systems/system_single_voxel_to_canvas.hpp>
#include <irreden/render/systems/system_canvas_to_framebuffer.hpp>
#include <irreden/render/systems/system_framebuffers_to_screen.hpp>

#include <irreden/video/systems/system_video_encoder.hpp>

#include <irreden/input/commands/command_close_window.hpp>

#include <irreden/render/commands/command_zoom_in.hpp>
#include <irreden/render/commands/command_zoom_out.hpp>
#include <irreden/render/commands/command_move_camera.hpp>
#include <irreden/voxel/commands/command_spawn_particle_mouse_position.hpp>

using namespace IRComponents;
using namespace IRConstants;
using namespace IRECS;

namespace IREngine {
//TODO: replace initalization constants with config file.

    World::World(int &argc, char  **argv)
    :   m_IRGLFWWindow{
            IRConstants::kInitWindowSize
        }
    ,   m_entityManager{}
    ,   m_commandManager{}
    ,   m_systemManager{}
    ,   m_inputManager{
            m_IRGLFWWindow
        }
    ,   m_renderingResourceManager{}
    ,   m_renderer{
            m_IRGLFWWindow
        }
    ,   m_audioManager{}
    ,   m_timeManager{}
    {
        ImageData icon{
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
        initEngineSystems();
        initEngineCommands();
        m_renderer.printGLSystemInfo();
        IR_PROFILE_MAIN_THREAD;

        IRE_LOG_INFO("Initalized game world");

    }

    World::~World() {

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
                input();
                update();
                m_IRGLFWWindow.pollEvents();
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

        m_systemManager.executePipeline(SYSTEM_TYPE_INPUT);
    }

    void World::start() {
        m_timeManager.start();
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
        m_systemManager.executePipeline(SYSTEM_TYPE_UPDATE);

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

    void World::initEngineSystems() {
        initIRInputSystems();
        initIRUpdateSystems();
        initIROutputSystems();
        initIRRenderSystems();
    }

    void World::initEngineCommands() {
        IRCommand::createCommand<IRCommand::CLOSE_WINDOW>(
            InputTypes::KEY_MOUSE,
            ButtonStatuses::PRESSED,
            KeyMouseButtons::kKeyButtonEscape
        );
        IRCommand::createCommand<IRCommand::ZOOM_IN>(
            InputTypes::KEY_MOUSE,
            ButtonStatuses::PRESSED,
            KeyMouseButtons::kKeyButtonEqual
        );
        IRCommand::createCommand<IRCommand::ZOOM_OUT>(
            InputTypes::KEY_MOUSE,
            ButtonStatuses::PRESSED,
            KeyMouseButtons::kKeyButtonMinus
        );
        IRCommand::createCommand<IRCommand::MOVE_CAMERA_DOWN_START>(
            InputTypes::KEY_MOUSE,
            ButtonStatuses::PRESSED,
            KeyMouseButtons::kKeyButtonS
        );
        IRCommand::createCommand<IRCommand::MOVE_CAMERA_UP_START>(
            InputTypes::KEY_MOUSE,
            ButtonStatuses::PRESSED,
            KeyMouseButtons::kKeyButtonW
        );
        IRCommand::createCommand<IRCommand::MOVE_CAMERA_RIGHT_START>(
            InputTypes::KEY_MOUSE,
            ButtonStatuses::PRESSED,
            KeyMouseButtons::kKeyButtonD
        );
        IRCommand::createCommand<IRCommand::MOVE_CAMERA_LEFT_START>(
            InputTypes::KEY_MOUSE,
            ButtonStatuses::PRESSED,
            KeyMouseButtons::kKeyButtonA
        );
        IRCommand::createCommand<IRCommand::MOVE_CAMERA_DOWN_END>(
            InputTypes::KEY_MOUSE,
            ButtonStatuses::RELEASED,
            KeyMouseButtons::kKeyButtonS
        );
        IRCommand::createCommand<IRCommand::MOVE_CAMERA_UP_END>(
            InputTypes::KEY_MOUSE,
            ButtonStatuses::RELEASED,
            KeyMouseButtons::kKeyButtonW
        );
        IRCommand::createCommand<IRCommand::MOVE_CAMERA_RIGHT_END>(
            InputTypes::KEY_MOUSE,
            ButtonStatuses::RELEASED,
            KeyMouseButtons::kKeyButtonD
        );
        IRCommand::createCommand<IRCommand::MOVE_CAMERA_LEFT_END>(
            InputTypes::KEY_MOUSE,
            ButtonStatuses::RELEASED,
            KeyMouseButtons::kKeyButtonA
        );
        IRCommand::createCommand<IRCommand::SPAWN_PARTICLE_MOUSE_POSITION>(
            InputTypes::KEY_MOUSE,
            ButtonStatuses::HELD,
            KeyMouseButtons::kMouseButtonLeft
        );

    }

    void World::initIROutputSystems() {

    }

    void World::initIRInputSystems() {
        m_systemManager.registerPipeline(
            SYSTEM_TYPE_INPUT,
            {
                IRECS::createSystem<INPUT_KEY_MOUSE>()
            ,   IRECS::createSystem<INPUT_GAMEPAD>()
            ,   IRECS::createSystem<INPUT_MIDI_MESSAGE_IN>()

            }
        );
    }

    void World::initIRUpdateSystems() {




        m_systemManager.registerPipeline(
            SYSTEM_TYPE_UPDATE,
            {
                IRECS::createSystem<PARTICLE_SPAWNER>()
            ,   IRECS::createSystem<VELOCITY_3D>()
            ,   IRECS::createSystem<ACCELERATION_3D>()
            ,   IRECS::createSystem<GRAVITY_3D>()
            ,   IRECS::createSystem<PERIODIC_IDLE>()
            ,   IRECS::createSystem<GOTO_3D>()
            ,   IRECS::createSystem<GLOBAL_POSITION_3D>()
            // Move to output systems
            ,   IRECS::createSystem<OUTPUT_MIDI_MESSAGE_OUT>()
            ,   IRECS::createSystem<UPDATE_VOXEL_SET_CHILDREN>()
            ,   IRECS::createSystem<LIFETIME>()
            // ,    m_systemManager.registerEngineSystem<VIDEO_ENCODER, SYSTEM_TYPE_UPDATE>();
            }
        );

    }

    void World::initIRRenderSystems() {
        // m_systemManager.registerSystemClass<
        //     RENDERING_FRAMEBUFFER_TO_SCREEN,
        //     SYSTEM_TYPE_RENDER
        // >();

        m_systemManager.registerPipeline(
            SYSTEM_TYPE_RENDER,
            {
                IRECS::createSystem<TEXTURE_SCROLL>()
            ,   IRECS::createSystem<RENDERING_SINGLE_VOXEL_TO_CANVAS_FIRST>()
            ,   IRECS::createSystem<RENDERING_SINGLE_VOXEL_TO_CANVAS_SECOND>()
            ,   IRECS::createSystem<RENDERING_CANVAS_TO_FRAMEBUFFER>()
            ,   IRECS::createSystem<RENDERING_FRAMEBUFFER_TO_SCREEN>()
            }
        );

    }

    // void World::setCameraPosition3D(const vec3& position) {
    //     IRECS::getEngineSystem<SCREEN_VIEW>().setCameraPosition3D(position);
    // }

} // namespace IREngine