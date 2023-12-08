#ifndef COMMAND_MOVE_CAMERA_H
#define COMMAND_MOVE_CAMERA_H

#include <irreden/ir_command.hpp>
#include <irreden/ir_ecs.hpp>

#include <irreden/render/components/component_camera.hpp>

namespace IRCommand {

    template<>
    struct Command<MOVE_CAMERA_LEFT_START> {
        static auto create() {
            return []() {
                IRECS::getComponent<C_Camera>(
                    IRECS::getEntity("camera")
                ).moveLeftStart();
            };
        }
    };

    template<>
    struct Command<MOVE_CAMERA_RIGHT_START> {
        static auto create() {
            return []() {
                IRECS::getComponent<C_Camera>(
                    IRECS::getEntity("camera")
                ).moveRightStart();
            };
        }
    };

    template<>
    struct Command<MOVE_CAMERA_UP_START> {
        static auto create() {
            return []() {
                IRECS::getComponent<C_Camera>(
                    IRECS::getEntity("camera")
                ).moveUpStart();
            };
        }
    };

    template<>
    struct Command<MOVE_CAMERA_DOWN_START> {
        static auto create() {
            return []() {
                IRECS::getComponent<C_Camera>(
                    IRECS::getEntity("camera")
                ).moveDownStart();
            };
        }
    };

    template<>
    struct Command<MOVE_CAMERA_LEFT_END> {
        static auto create() {
            return []() {
                IRECS::getComponent<C_Camera>(
                    IRECS::getEntity("camera")
                ).moveLeftStop();
            };
        }
    };

    template<>
    struct Command<MOVE_CAMERA_RIGHT_END> {
        static auto create() {
            return []() {
                IRECS::getComponent<C_Camera>(
                    IRECS::getEntity("camera")
                ).moveRightStop();
            };
        }
    };

    template<>
    struct Command<MOVE_CAMERA_UP_END> {
        static auto create() {
            return []() {
                IRECS::getComponent<C_Camera>(
                    IRECS::getEntity("camera")
                ).moveUpStop();
            };
        }
    };

    template<>
    struct Command<MOVE_CAMERA_DOWN_END> {
        static auto create() {
            return []() {
                IRECS::getComponent<C_Camera>(
                    IRECS::getEntity("camera")
                ).moveDownStop();
            };
        }
    };

} // namespace IRCommand

#endif /* COMMAND_MOVE_CAMERA_H */
