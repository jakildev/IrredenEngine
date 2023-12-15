/*
 * Project: Particle Orthogonal Burst
 * File: main.cpp
 * Author: Evin Killian jakildev@gmail.com
 * Created Date: November 2023
 * -----
 * Modified By: <your_name> <Month> <YYYY>
 */

#include <irreden/ir_engine.hpp>

// COMPONENTS
#include <irreden/common/components/component_position_3d.hpp>
#include <irreden/update/components/component_velocity_3d.hpp>
#include <irreden/update/components/component_acceleration_3d.hpp>
#include <irreden/voxel/components/component_voxel_set.hpp>
#include <irreden/update/components/component_goto_easing_3d.hpp>

// SYSTEMS
#include <irreden/update/systems/system_velocity.hpp>
#include <irreden/update/systems/system_goto_3d.hpp>
#include <irreden/update/systems/system_update_positions_global.hpp>
#include <irreden/voxel/systems/system_update_voxel_set_children.hpp>
#include <irreden/update/systems/system_lifetime.hpp>

#include <irreden/input/systems/system_input_key_mouse.hpp>
#include <irreden/input/systems/system_input_gamepad.hpp>

#include <irreden/render/systems/system_render_velocity_2d_iso.hpp>
#include <irreden/render/systems/system_single_voxel_to_canvas.hpp>
#include <irreden/render/systems/system_canvas_to_framebuffer.hpp>
#include <irreden/render/systems/system_framebuffers_to_screen.hpp>

// COMMANDS
#include <irreden/input/commands/command_close_window.hpp>
#include <irreden/render/commands/command_zoom_in.hpp>
#include <irreden/render/commands/command_zoom_out.hpp>
#include <irreden/render/commands/command_move_camera.hpp>

void initSystems();
void initEntities();
void initCommands();

int main(int argc, char **argv) {
    IR_LOG_INFO("Starting creation: default");

    IREngine::init(argc, argv);
    initSystems();
    initCommands();
    initEntities();
    IREngine::gameLoop();

    return 0;
}

void initSystems() {
    IRECS::registerPipeline(
        IRTime::Events::UPDATE,
        {
            IRECS::createSystem<VELOCITY_3D>()
        ,   IRECS::createSystem<GOTO_3D>()
        ,   IRECS::createSystem<GLOBAL_POSITION_3D>()
        ,   IRECS::createSystem<UPDATE_VOXEL_SET_CHILDREN>()
        ,   IRECS::createSystem<LIFETIME>()
        }
    );

    IRECS::registerPipeline(
        IRTime::Events::INPUT,
        {
            IRECS::createSystem<INPUT_KEY_MOUSE>()
        ,   IRECS::createSystem<INPUT_GAMEPAD>()
        }
    );
    IRECS::registerPipeline(
        IRTime::Events::RENDER,
        {
            IRECS::createSystem<RENDERING_VELOCITY_2D_ISO>()
        ,   IRECS::createSystem<RENDERING_SINGLE_VOXEL_TO_CANVAS_FIRST>()
        ,   IRECS::createSystem<RENDERING_SINGLE_VOXEL_TO_CANVAS_SECOND>()
        ,   IRECS::createSystem<RENDERING_CANVAS_TO_FRAMEBUFFER>()
        ,   IRECS::createSystem<RENDERING_FRAMEBUFFER_TO_SCREEN>()
        }
    );

}

void initCommands() {
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

void initEntities() {
    const ivec3 partitions = ivec3(2, 2, 2);
    const ivec3 batchSize = IRConstants::kVoxelPoolMaxAllocationSize;

    for(int x = 0; x < partitions.x; x++) {
        for(int y = 0; y < partitions.y; y++) {
            for(int z = 0; z < partitions.z; z++) {
                EntityId parent = IRECS::createEntity(
                    C_Position3D{
                        static_cast<float>(x * 16),
                        static_cast<float>(y * 16),
                        static_cast<float>(z * 16)
                    }
                );
                auto entities = IRECS::createEntityBatchWithFunctions_Ext(
                    batchSize / partitions,
                    IRECS::CreateEntityExtraParams{
                        .relation = {
                            Relation::CHILD_OF,
                            parent
                        }
                    },
                    [](ivec3 index){
                        return C_Position3D{
                            0, 0, 0
                        };
                    },
                    [batchSize, partitions](ivec3 index) {
                        Color color{
                            roundFloatToByte(static_cast<float>(index.x) / (batchSize.x / partitions.x)),
                            roundFloatToByte(static_cast<float>(index.y) / (batchSize.y / partitions.y)),
                            roundFloatToByte(static_cast<float>(index.z) / (batchSize.z / partitions.z)),
                            255
                        };
                        return C_VoxelSetNew(
                            ivec3(1, 1, 1),
                            // Color{255, 0, 0, 255}
                            color
                        );
                    },
                    [](ivec3 index) {
                        return C_GotoEasing3D{
                            C_Position3D{
                                vec3(0, 0, 0)
                            },
                            C_Position3D{
                                vec3(index.x, index.y, index.z)
                            },
                            (-index.x + -index.y + -index.z + 700) / 100.0f,
                            IREasingFunctions::kBounceEaseOut
                        };
                    }
                ,   [](ivec3 index) {
                        return C_Lifetime{
                            IRMath::randomInt(0, 1000)
                        };
                    }
                ,   [batchSize, partitions](ivec3 index) {
                        int face = (index.x + index.y + index.z) % 3;
                        if(face == 0) {
                            return C_Velocity3D{
                                vec3(
                                    // sumVecComponents(index),
                                    IRMath::randomFloat(-80, 80),
                                    0,
                                    0
                                )
                            };
                        }
                        if(face == 1) {
                            return C_Velocity3D{
                                vec3(
                                    0,
                                    IRMath::randomFloat(-80, 80),
                                    // sumVecComponents(index),
                                    0
                                )
                            };
                        }
                        if(face == 2) {
                            return C_Velocity3D{
                                vec3(
                                    0,
                                    0,
                                    // sumVecComponents(index)
                                    IRMath::randomFloat(-80, 80)
                                    // index.z
                                )
                            };
                        }

                        return C_Velocity3D{vec3(0, 0, 0)};
                    }
                );
            }
        }
    }
}