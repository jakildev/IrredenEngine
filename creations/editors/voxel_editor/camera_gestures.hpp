#ifndef IR_VOXEL_EDITOR_CAMERA_GESTURES_H
#define IR_VOXEL_EDITOR_CAMERA_GESTURES_H

#include "session_builder.hpp"

#include <algorithm>
#include <array>

namespace IRVoxelEditor::Session {

enum class CameraGestureTool { BRUSH, PAINT, BOX, LINE };

struct CameraGestureCase {
    const char *name_;
    IRInput::KeyMouseButtons cameraButton_;
    bool control_ = false;
    bool shift_ = false;
    bool interrupt_ = false;
    CameraGestureTool tool_ = CameraGestureTool::BRUSH;
};

inline constexpr std::array<CameraGestureCase, 11> kCameraGestureCases{{
    {"alt_orbit", IRInput::kKeyButtonLeftAlt},
    {"right_orbit", IRInput::kMouseButtonRight},
    {"middle_orbit", IRInput::kMouseButtonMiddle, true},
    {"cursor_orbit", IRInput::kMouseButtonMiddle, true, true},
    {"q_snap", IRInput::kKeyButtonQ},
    {"e_snap", IRInput::kKeyButtonE},
    {"space_pan", IRInput::kKeyButtonSpace},
    {"brush_interrupt", IRInput::kMouseButtonRight, false, false, true},
    {"box_interrupt", IRInput::kMouseButtonRight, false, false, true, CameraGestureTool::BOX},
    {"line_interrupt", IRInput::kMouseButtonRight, false, false, true, CameraGestureTool::LINE},
    {"paint_orbit", IRInput::kKeyButtonLeftAlt, false, false, false, CameraGestureTool::PAINT},
}};

bool evaluateCameraGestureCheck(const void *context, std::string &actual);

inline Recipe buildCameraGestures() {
    Recipe recipe;
    recipe.name_ = "camera_gestures";
    static constexpr auto checks = [] {
        std::array<int, kCameraGestureCases.size() * 4> result{};
        for (int i = 0; i < static_cast<int>(result.size()); ++i)
            result[static_cast<std::size_t>(i)] = i;
        return result;
    }();
    constexpr std::array phases{"held", "release", "fresh_click", "undo"};
    using Type = IRVideo::GuiInputEvent::Type;
    for (const int shot : checks) {
        const auto &test = kCameraGestureCases[shot / 4];
        const int phase = shot % 4;
        Segment segment;
        segment.label_ = std::string("camera_gestures_") + test.name_ + "_" + phases[phase];
        segment.zoom_ = 8.0f;
        const auto event = [&](int frame, Type type, IRInput::KeyMouseButtons button) {
            IRVideo::GuiInputEvent input{};
            input.frameOffset_ = frame;
            input.type_ = type;
            input.button_ = button;
            segment.events_.push_back(input);
        };
        if (phase == 0) {
            event(0, Type::MOVE, IRInput::kMouseButtonNull);
            if (test.control_)
                event(1, Type::PRESS, IRInput::kKeyButtonLeftControl);
            if (test.shift_)
                event(1, Type::PRESS, IRInput::kKeyButtonLeftShift);
            event(test.interrupt_ ? 5 : 2, Type::PRESS, test.cameraButton_);
            event(3, Type::PRESS, IRInput::kMouseButtonLeft);
            event(8, Type::MOVE, IRInput::kMouseButtonNull);
        } else if (phase == 1) {
            // The camera modifier/button is released before the painting button.
            event(1, Type::RELEASE, test.cameraButton_);
            if (test.control_)
                event(1, Type::RELEASE, IRInput::kKeyButtonLeftControl);
            if (test.shift_)
                event(1, Type::RELEASE, IRInput::kKeyButtonLeftShift);
            event(3, Type::MOVE, IRInput::kMouseButtonNull);
            event(6, Type::RELEASE, IRInput::kMouseButtonLeft);
        } else if (phase == 2) {
            event(0, Type::MOVE, IRInput::kMouseButtonNull);
            event(3, Type::PRESS, IRInput::kMouseButtonLeft);
            event(5, Type::RELEASE, IRInput::kMouseButtonLeft);
        } else {
            event(0, Type::PRESS, IRInput::kKeyButtonLeftControl);
            event(2, Type::PRESS, IRInput::kKeyButtonZ);
            event(4, Type::RELEASE, IRInput::kKeyButtonZ);
            event(6, Type::RELEASE, IRInput::kKeyButtonLeftControl);
        }
        std::stable_sort(
            segment.events_.begin(),
            segment.events_.end(),
            [](const auto &a, const auto &b) { return a.frameOffset_ < b.frameOffset_; }
        );
        segment.assertions_.push_back(
            IRPrefab::GuiTest::predicate(
                &evaluateCameraGestureCheck,
                &checks[shot],
                "camera_owns_complete_gesture"
            )
        );
        recipe.segments_.push_back(std::move(segment));
    }
    return recipe;
}

} // namespace IRVoxelEditor::Session

#endif
