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

// AUDIO SYSTEMS
#include <irreden/audio/systems/system_audio_midi_message_in.hpp>
#include <irreden/audio/systems/system_audio_midi_message_out.hpp>

// INPUT SYSTEMS
#include <irreden/input/systems/system_input_key_mouse.hpp>
#include <irreden/input/systems/system_input_gamepad.hpp>

// UPDATE SYSTEMS
#include <irreden/voxel/systems/system_voxel_set_reshaper.hpp>
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

// RENDER SYSTEMS
#include <irreden/render/systems/system_texture_scroll.hpp>
#include <irreden/render/systems/system_single_voxel_to_canvas.hpp>
#include <irreden/render/systems/system_canvas_to_framebuffer.hpp>
#include <irreden/render/systems/system_framebuffers_to_screen.hpp>

// VIDEO SYSTEMS
#include <irreden/video/systems/system_video_encoder.hpp>

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
,   m_renderingResourceManager{}
,   m_renderer{
        m_IRGLFWWindow
    }
,   m_audioManager{}
//         // TODO: Move opening audio interface somewhere else
//         // (component midi device creation perhaps...)
//         std::vector<IRAudio::MidiInInterfaces>{
//             // IRAudio::MidiInInterfaces::MIDI_IN_OP1,
//             IRAudio::MidiInInterfaces::MIDI_IN_UMC

//         },
//         std::vector<IRAudio::MidiOutInterfaces>{
//             // IRAudio::MidiOutInterfaces::MIDI_OUT_OP1,
//             IRAudio::MidiOutInterfaces::MIDI_OUT_UMC
//         }
//     }
,   m_timeManager{}
{
    initEngineSystems();
    initEngineCommands();
    m_renderer.printGLSystemInfo();
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

// void IRWorld::addEntityToScene(
//     EntityHandle entity,
//     EntityHandle parent
// )
// {
//     IRECS::getSystem<UPDATE_VOXEL_SET_CHILDREN>().addEntityToScene(
//         entity,
//         parent
//     );
// }

void IRWorld::input() {
    IRProfile::profileFunction(IR_PROFILER_COLOR_UPDATE);

    // TODO: Make this an event from timeManager after
    // making that more generic.
    IRProfile::engLogDebug("Begin input world.");

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
    IRProfile::profileFunction(IR_PROFILER_COLOR_UPDATE);

    m_audioManager.processMidiMessageQueue(); // this should be somewhere else
    m_commandManager.executeUserKeyboardCommandsAll();
    // m_commandManager.executeDeviceMidiCCCommandsAll();
    // m_commandManager.executeDeviceMidiNoteCommandsAll();

    m_systemManager.executeGroup<SYSTEM_TYPE_UPDATE>();

    // Destroy all marked entities in one step
    m_entityManager.destroyMarkedEntities();
    // TODO: maybe component adds and removes should be done here too
    m_timeManager.endEvent<IRTime::UPDATE>();
}

void IRWorld::render()
{
    // Possible oppertunity for promise style await here...
    m_timeManager.beginEvent<IRTime::RENDER>();
    IRProfile::profileFunction(IR_PROFILER_COLOR_RENDER);

    m_renderer.tick();

    m_timeManager.endEvent<IRTime::RENDER>();
}

void IRWorld::initEngineSystems() {
    // TODO: Have implementer of engine pass in a list of what
    // systems to use and in what order. This can be a mix of their
    // own user systesms and engine systems.
    initIRInputSystems();
    initIRUpdateSystems();
    initIROutputSystems();
    initIRRenderSystems();
}

void IRWorld::initEngineCommands() {
    IRCommand::registerCommand(
        InputTypes::KEY_MOUSE,
        ButtonStatuses::PRESSED,
        KeyMouseButtons::kKeyButtonEscape,
        []() {
            IRECS::getSystem<SystemName::SCREEN_VIEW>().closeWindow();
        }
    );
}

void IRWorld::initIROutputSystems() {
}

void IRWorld::initIRInputSystems() {
    m_systemManager.registerSystem<INPUT_KEY_MOUSE, SYSTEM_TYPE_INPUT>(
        m_IRGLFWWindow
    );
    m_systemManager.registerSystem<INPUT_GAMEPAD, SYSTEM_TYPE_INPUT>(
        m_IRGLFWWindow
    );
    m_systemManager.registerSystem<INPUT_MIDI_MESSAGE_IN, SYSTEM_TYPE_INPUT>();

}

void IRWorld::initIRUpdateSystems() {
    // TODO: make systems more like a pipeline
    // Component adds and removes, and entity creates and destroys
    // will be queued and executed all at once at the end of the pipeline.
    // At the beginning of a pipeline, all entities will be fixed for this
    // frame. They can be sorted and things of that nature to be more efficient.
    // for particular update systems.
    m_systemManager.registerSystem<VOXEL_POOL, SYSTEM_TYPE_UPDATE>(
        kVoxelPoolSize,
        kVoxelPoolPlayerSize
    );
    m_systemManager.registerSystem<SCREEN_VIEW, SYSTEM_TYPE_UPDATE>(
        m_IRGLFWWindow
    );
    m_systemManager.registerSystem<VOXEL_SET_RESHAPER, SYSTEM_TYPE_UPDATE>();
    m_systemManager.registerSystem<PARTICLE_SPAWNER, SYSTEM_TYPE_UPDATE>();
    m_systemManager.registerSystem<VELOCITY_3D, SYSTEM_TYPE_UPDATE>();
    m_systemManager.registerSystem<ACCELERATION_3D, SYSTEM_TYPE_UPDATE>();
    m_systemManager.registerSystem<GRAVITY_3D, SYSTEM_TYPE_UPDATE>();
    m_systemManager.registerSystem<PERIODIC_IDLE, SYSTEM_TYPE_UPDATE>();
    m_systemManager.registerSystem<GOTO_3D, SYSTEM_TYPE_UPDATE>();
    // TODO: This should be an output system but midi message out's get destroyed
    // by lifetime system, so perhaps they should just get consumed by
    // midi out system instead.
    m_systemManager.registerSystem<UPDATE_POSITIONS_GLOBAL, SYSTEM_TYPE_UPDATE>();
    m_systemManager.registerSystem<OUTPUT_MIDI_MESSAGE_OUT, SYSTEM_TYPE_UPDATE>();
    m_systemManager.registerSystem<UPDATE_VOXEL_SET_CHILDREN, SYSTEM_TYPE_UPDATE>();
    m_systemManager.registerSystem<LIFETIME, SYSTEM_TYPE_UPDATE>();
    // m_systemManager.registerSystem<VIDEO_ENCODER, SYSTEM_TYPE_UPDATE>();

}

void IRWorld::initIRRenderSystems() {
    m_systemManager.registerSystem<RENDERING_TEXTURE_SCROLL, SYSTEM_TYPE_RENDER>();
    m_systemManager.registerSystem<
        RENDERING_SINGLE_VOXEL_TO_CANVAS,
        SYSTEM_TYPE_RENDER
    >();
    m_systemManager.registerSystem<
        RENDERING_CANVAS_TO_FRAMEBUFFER,
        SYSTEM_TYPE_RENDER
    >
    (
        IRConstants::kScreenTriangleMaxCanvasSizeWithBuffer
    );
    m_systemManager.registerSystem<
        RENDERING_FRAMEBUFFER_TO_SCREEN,
        SYSTEM_TYPE_RENDER
    >();
}


// This set player stuff is incomplete and should probably
// just be deduced
void IRWorld::setPlayer(const EntityId& player) {
    IRECS::getSystem<RENDERING_SINGLE_VOXEL_TO_CANVAS>().setPlayer(player);
    // getSystem<SCREEN_VIEW>()->setCameraFollowEntity(player);
}

void IRWorld::setCameraPosition3D(const vec3& position) {
    IRECS::getSystem<SCREEN_VIEW>().setCameraPosition3D(position);
}