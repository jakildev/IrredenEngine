#ifndef COMMAND_MOVE_CAMERA_H
#define COMMAND_MOVE_CAMERA_H

#include <irreden/ir_command.hpp>
#include <irreden/ir_ecs.hpp>

#include <irreden/render/components/component_camera.hpp>
#include <irreden/update/components/component_velocity_2d_iso.hpp>

namespace IRCommand {

    static constexpr float kCameraMoveSpeed = 30.0f;

    template<>
    struct Command<MOVE_CAMERA_LEFT_START> {
        static auto create() {
            return []() {
                IRECS::getComponent<C_Velocity2DIso>("camera")
                    .velocity_.x += kCameraMoveSpeed;
            };
        }
    };

    template<>
    struct Command<MOVE_CAMERA_RIGHT_START> {
        static auto create() {
            return []() {
                IRECS::getComponent<C_Velocity2DIso>("camera")
                    .velocity_.x -= kCameraMoveSpeed;
            };
        }
    };

    template<>
    struct Command<MOVE_CAMERA_UP_START> {
        static auto create() {
            return []() {
                IRECS::getComponent<C_Velocity2DIso>("camera")
                    .velocity_.y += kCameraMoveSpeed;
            };
        }
    };

    template<>
    struct Command<MOVE_CAMERA_DOWN_START> {
        static auto create() {
            return []() {
                IRECS::getComponent<C_Velocity2DIso>("camera")
                    .velocity_.y -= kCameraMoveSpeed;
            };
        }
    };

    template<>
    struct Command<MOVE_CAMERA_LEFT_END> {
        static auto create() {
            return []() {
                IRECS::getComponent<C_Velocity2DIso>("camera")
                    .velocity_.x -= kCameraMoveSpeed;
            };
        }
    };

    template<>
    struct Command<MOVE_CAMERA_RIGHT_END> {
        static auto create() {
            return []() {
                IRECS::getComponent<C_Velocity2DIso>("camera")
                    .velocity_.x += kCameraMoveSpeed;
            };
        }
    };

    template<>
    struct Command<MOVE_CAMERA_UP_END> {
        static auto create() {
            return []() {
                IRECS::getComponent<C_Velocity2DIso>("camera")
                    .velocity_.y -= kCameraMoveSpeed;
            };
        }
    };

    template<>
    struct Command<MOVE_CAMERA_DOWN_END> {
        static auto create() {
            return []() {
                IRECS::getComponent<C_Velocity2DIso>("camera")
                    .velocity_.y += kCameraMoveSpeed;
            };
        }
    };

} // namespace IRCommand

#endif /* COMMAND_MOVE_CAMERA_H */
