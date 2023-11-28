/*
 * Project: Irreden Engine
 * File: ir_world.cpp
 * Author: Evin Killian jakildev@gmail.com
 * Created Date: October 2023
 * -----
 * Modified By: <your_name> <Month> <YYYY>
 */

#include <irreden/ir_world.hpp>
#include <irreden/ir_profile.hpp>
#include <irreden/ir_constants.hpp>
// SYSTEMS
// -    AUDIO
#include <irreden/audio/systems/system_audio_midi_message_in.hpp>
#include <irreden/audio/systems/system_audio_midi_message_out.hpp>
// -    INPUT
#include <irreden/input/systems/system_input_key_mouse.hpp>
#include <irreden/input/systems/system_input_gamepad.hpp>
// -    UPDATE
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
// -    RENDER
#include <irreden/render/systems/system_texture_scroll.hpp>
#include <irreden/render/systems/system_single_voxel_to_canvas.hpp>
#include <irreden/render/systems/system_canvas_to_framebuffer.hpp>
#include <irreden/render/systems/system_framebuffers_to_screen.hpp>

// -    VIDEO
#include <irreden/video/systems/system_video_encoder.hpp>
// COMMANDS
// INPUT
#include <irreden/input/commands/command_close_window.hpp>

// RENDER COMMANDS
#include <irreden/render/commands/command_zoom_in.hpp>
#include <irreden/render/commands/command_zoom_out.hpp>
#include <irreden/render/commands/command_move_camera.hpp>

using namespace IRComponents;
using namespace IRConstants;
// using namespace IRCommands;
using namespace IRECS;

//TODO: replace initalization constants with config file.

IRWorld::IRWorld(int &argc, char  **argv)
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
    initEngineSystems();
    initEngineCommands();
    m_renderer.printGLSystemInfo();
    IRProfile::profileMainThread();

    IRProfile::engLogInfo("Initalized game world");

}

IRWorld::~IRWorld() {

}

void IRWorld::gameLoop() {

    // init();
    initGameSystems();
    initGameEntities();

    start();
    while (!m_IRGLFWWindow.shouldClose())
    {
        m_timeManager.beginMainLoop();

        while (m_timeManager.shouldUpdate())
        {
            input();
            update();
            // output();
        }
        m_IRGLFWWindow.pollEvents();
        // TODO: Set frame caps
        render(); // ???
    }
    end();
}

void IRWorld::input() {
    IR_PROFILE_FUNCTION(IR_PROFILER_COLOR_UPDATE);

    // TODO: Make this an event from timeManager after
    // making that more generic.
    IRProfile::engLogDebug("Begin input world.");
    m_inputManager.tick();
    m_audioManager.getMidiIn().tick();

    m_systemManager.executePipeline(SYSTEM_TYPE_INPUT);
    m_systemManager.executeGroup<SYSTEM_TYPE_INPUT>();

    IRProfile::engLogDebug("End input world.");
}

void IRWorld::start() {
    m_timeManager.start();
    m_systemManager.executeEvent<IRTime::Events::START>();
}

void IRWorld::end() {
    m_systemManager.executeEvent<IRTime::Events::END>();
}

void IRWorld::update()
{
    m_timeManager.beginEvent<IRTime::UPDATE>();
    IR_PROFILE_FUNCTION(IR_PROFILER_COLOR_UPDATE);

    m_commandManager.executeUserKeyboardCommandsAll();
    m_commandManager.executeDeviceMidiCCCommandsAll();
    m_commandManager.executeDeviceMidiNoteCommandsAll();
    m_systemManager.executeGroup<SYSTEM_TYPE_UPDATE>(); // TODO REMOVE THIS
    m_systemManager.executePipeline(SYSTEM_TYPE_UPDATE);

    // Destroy all marked entities in one step
    m_entityManager.destroyMarkedEntities();
    // TODO: maybe component adds and removes should be done here too
    m_timeManager.endEvent<IRTime::UPDATE>();
}

void IRWorld::render()
{
    // Possible oppertunity for promise style await here...
    m_timeManager.beginEvent<IRTime::RENDER>();
    IR_PROFILE_FUNCTION(IR_PROFILER_COLOR_RENDER);

    m_inputManager.tickRender();
    m_systemManager.executePipeline(SYSTEM_TYPE_RENDER);
    m_renderer.tick();

    m_timeManager.endEvent<IRTime::RENDER>();
}

void IRWorld::initEngineSystems() {
    initIRInputSystems();
    initIRUpdateSystems();
    initIROutputSystems();
    initIRRenderSystems();
}

void IRWorld::initEngineCommands() {
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
}

void IRWorld::initIROutputSystems() {

}

void IRWorld::initIRInputSystems() {
    SystemId systemInputKeyMouse = IRECS::createSystem<INPUT_KEY_MOUSE>();
    SystemId systemInputGamepad = IRECS::createSystem<INPUT_GAMEPAD>();
    SystemId systemInputMidiIn = IRECS::createSystem<INPUT_MIDI_MESSAGE_IN>();
    m_systemManager.registerPipeline(
        SYSTEM_TYPE_INPUT,
        {
            systemInputKeyMouse,
            systemInputGamepad,
            systemInputMidiIn
        }
    );
}

void IRWorld::initIRUpdateSystems() {


    SystemId systemParticleSpawner = IRECS::createSystem<PARTICLE_SPAWNER>();
    SystemId systemVelocity = IRECS::createSystem<VELOCITY_3D>();
    SystemId systemAcceleration = IRECS::createSystem<ACCELERATION_3D>();
    SystemId systemGravity = IRECS::createSystem<GRAVITY_3D>();
    SystemId systemPeriodicIdle = IRECS::createSystem<PERIODIC_IDLE>();
    SystemId systemGoto = IRECS::createSystem<GOTO_3D>();
    SystemId systemGlobalPosition = IRECS::createSystem<GLOBAL_POSITION_3D>();
    // Move to output systems
    SystemId systemMidiMessageOut = IRECS::createSystem<OUTPUT_MIDI_MESSAGE_OUT>();
    SystemId systemUpdateVoxelSetChildren = IRECS::createSystem<UPDATE_VOXEL_SET_CHILDREN>();
    SystemId systemLifetime = IRECS::createSystem<LIFETIME>();
    // m_systemManager.registerEngineSystem<VIDEO_ENCODER, SYSTEM_TYPE_UPDATE>();

    m_systemManager.registerPipeline(
        SYSTEM_TYPE_UPDATE,
        {
            systemParticleSpawner,
            systemVelocity,
            systemAcceleration,
            systemGravity,
            systemPeriodicIdle,
            systemGoto,
            systemGlobalPosition,
            systemMidiMessageOut,
            systemUpdateVoxelSetChildren,
            systemLifetime
        }
    );

}

void IRWorld::initIRRenderSystems() {
    // m_systemManager.registerSystemClass<
    //     RENDERING_SINGLE_VOXEL_TO_CANVAS,
    //     SYSTEM_TYPE_RENDER
    // >();
    // m_systemManager.registerSystemClass<
    //     RENDERING_CANVAS_TO_FRAMEBUFFER,
    //     SYSTEM_TYPE_RENDER
    // >
    // (
    //     IRConstants::kScreenTriangleMaxCanvasSizeWithBuffer
    // );
    m_systemManager.registerSystemClass<
        RENDERING_FRAMEBUFFER_TO_SCREEN,
        SYSTEM_TYPE_RENDER
    >();

    m_systemManager.registerPipeline(
        SYSTEM_TYPE_RENDER,
        {
            IRECS::createSystem<TEXTURE_SCROLL>()
        ,   IRECS::createSystem<RENDERING_SINGLE_VOXEL_TO_CANVAS_FIRST>()
        ,   IRECS::createSystem<RENDERING_SINGLE_VOXEL_TO_CANVAS_SECOND>()
        ,   IRECS::createSystem<RENDERING_CANVAS_TO_FRAMEBUFFER>()
    //         IRECS::createSystem<RENDERING_FRAMEBUFFER_TO_SCREEN>()
        }
    );

}

// void IRWorld::setCameraPosition3D(const vec3& position) {
//     IRECS::getEngineSystem<SCREEN_VIEW>().setCameraPosition3D(position);
// }