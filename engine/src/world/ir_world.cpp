/*
 * Project: Irreden Engine
 * File: \irreden-engine\engine\src\world\ir_world.cpp
 * Author: Evin Killian jakildev@gmail.com
 * Created Date: October 2023
 * -----
 * Modified By: <your_name> <Month> <YYYY>
 */

#include "ir_world.hpp"
#include "global.hpp"

// OTHER PACKAGES THAT SHOULD MOVE TO RESPECTIVE PACKAGES
#include "../profiling/cpu_profiler.hpp" // ir_profiling
#include "../profiling/logger_spd.hpp" // ir_profiling
#include "../profiling/debug_helper.hpp" // ir_debugging
#include "../ecs/entity_handle.hpp" // ir_ecs
#include "../rendering/assimp_demo.hpp" // ir_rendering
#include "../rendering/rendering_rm.hpp" // ir_rendering
#include "../rendering/texture.hpp" // ir_rendering
#include "../world/ir_constants.hpp" // ir_constants

#include "../commands/ir_commands.hpp"

// AUDIO SYSTEMS

// INPUT SYSTEMS
#include "../systems/system_input_key_mouse.hpp"
#include "../systems/system_input_gamepad.hpp"
#include "../systems/system_input_midi_message_in.hpp"
#include "../systems/system_input_midi_message_out.hpp"

// UPDATE SYSTEMS
#include "../systems/system_voxel_set_reshaper.hpp"
#include "../systems/system_voxel_pool.hpp"
#include "../systems/system_update_screen_view.hpp"
#include "../systems/system_velocity.hpp"
#include "../systems/system_acceleration.hpp"
#include "../systems/system_gravity.hpp"
#include "../systems/system_periodic_idle.hpp"
#include "../systems/system_goto_3d.hpp"
#include "../systems/system_update_voxel_set_children.hpp"
#include "../systems/system_lifetime.hpp"
#include "../systems/system_particle_spawner.hpp"

// RENDER SYSTEMS
// #include "../systems/system_tile_selector.hpp"
#include "../systems/system_rendering_texture_scroll.hpp"
#include "../systems/system_rendering_single_voxel_to_canvas.hpp"
#include "../systems/system_rendering_canvas_to_framebuffer.hpp"
#include "../systems/system_rendering_framebuffers_to_screen.hpp"

#include "../systems/system_video_encoder.hpp"

// global state definition
Global global{};

using namespace IRComponents;
using namespace IRConstants;
using namespace IRCommands;
using namespace IRECS;

//TODO: replace initalization constants with config file.

IRWorld::IRWorld(int &argc, char **argv)
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
,   m_audioManager{
        // TODO: Move opening audio interface somewhere else
        // (component midi device creation perhaps...)
        std::vector<IRAudio::MidiInInterface>{
            // IRAudio::MidiInInterface::MIDI_IN_OP1,
            IRAudio::MidiInInterface::MIDI_IN_UMC

        },
        std::vector<IRAudio::MidiOutInterfaces>{
            // IRAudio::MidiOutInterfaces::MIDI_OUT_OP1,
            IRAudio::MidiOutInterfaces::MIDI_OUT_UMC
        }
    }
,   m_timeManager{}
{
    IRDebugging::printSystemInfo();
    initEngineSystems();

    ENG_LOG_INFO("Initalized game world");

    global.world_ = this;
}

IRWorld::~IRWorld() {

}

void IRWorld::gameLoop() {
    IRProfiling::ProfilerCPU profiler{};

    // init();
    initGameSystems();
    initGameEntities();

    /* Noclip console test */
    // noclip::console c{};

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

void IRWorld::addEntityToScene(
    EntityHandle entity,
    EntityHandle parent
)
{
    getSystem<UPDATE_VOXEL_SET_CHILDREN>()->addEntityToScene(
        entity,
        parent
    );
}

void IRWorld::input() {
    EASY_FUNCTION(IR_PROFILER_COLOR_UPDATE);

    // TODO: Make this an event from timeManager after
    // making that more generic.
    ENG_LOG_DEBUG("Begin input world.");

    m_systemManager.executeGroup<SYSTEM_TYPE_INPUT>();

    ENG_LOG_DEBUG("End input world.");

}

void IRWorld::start() {
        m_timeManager.start();

    m_systemManager.executeEvent<IREvents::START>();
}

void IRWorld::end() {
    m_systemManager.executeEvent<IREvents::END>();
}

void IRWorld::update()
{
    m_timeManager.beginEvent<IRTime::UPDATE>();
    EASY_FUNCTION(IR_PROFILER_COLOR_UPDATE);

    m_audioManager.processMidiMessageQueue(); // this should be somewhere else
    m_commandManager.executeUserKeyboardCommandsAll();
    m_commandManager.executeDeviceMidiCCCommandsAll();
    m_commandManager.executeDeviceMidiNoteCommandsAll();

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
    EASY_FUNCTION(IR_PROFILER_COLOR_RENDER);

    m_renderer.tick();

    m_timeManager.endEvent<IRTime::RENDER>();
}

void IRWorld::initEngineSystems() {
    // TODO: Have implementer of engine pass in a list of what
    // systems to use and in what order. This can be a mix of their
    // own user systesms and engine systems.
    initIRInputSystems();
    initIRUpdateSystems();
    initIRRenderSystems();
}

void IRWorld::initIRInputSystems() {
    m_systemManager.registerSystem<INPUT_KEY_MOUSE, SYSTEM_TYPE_INPUT>(
        m_IRGLFWWindow
    );
    m_systemManager.registerSystem<INPUT_GAMEPAD, SYSTEM_TYPE_INPUT>(
        m_IRGLFWWindow
    );
    m_systemManager.registerSystem<INPUT_MIDI_MESSAGE_IN, SYSTEM_TYPE_UPDATE>(
        m_audioManager.getMidiIn()
    );

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
    m_systemManager.registerSystem<OUTPUT_MIDI_MESSAGE_OUT, SYSTEM_TYPE_UPDATE>(
        m_audioManager.getMidiOut()
    );
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

void IRWorld::initEngineCommands() {
    // TODO: maybe move all commands here instead of inside systems
}

// This set player stuff is incomplete and should probably
// just be deduced
void IRWorld::setPlayer(const EntityId& player) {
    getSystem<RENDERING_SINGLE_VOXEL_TO_CANVAS>()->setPlayer(player);
    // getSystem<SCREEN_VIEW>()->setCameraFollowEntity(player);
}

void IRWorld::setCameraPosition3D(const vec3& position) {
    getSystem<SCREEN_VIEW>()->setCameraPosition3D(position);
}