#ifndef REGION_TOOLS_H
#define REGION_TOOLS_H

#include "sculpt_panel.hpp"
#include "session_builder.hpp"

#include <array>

namespace IRVoxelEditor::Session {

inline constexpr int kRegionToolPhaseCount = 5;
inline constexpr int kRegionToolShotCount = 25;

inline bool regionToolIsLine(int shot) {
    return (shot / kRegionToolPhaseCount) % 2 != 0;
}

inline float regionToolYaw(int shot) {
    return (shot / (2 * kRegionToolPhaseCount) == 0 ? 30.0f : 67.0f) * IRMath::kPi / 180.0f;
}

inline IRMath::ivec3 regionToolStart(IRMath::ivec3 size) {
    return {4, 5, size.z - 1};
}

inline IRMath::ivec3 regionToolEnd(IRMath::ivec3 size) {
    return {9, 8, size.z - 1};
}

bool evaluateRegionToolCheck(const void *context, std::string &actual);

inline Recipe buildRegionTools(IRMath::ivec3 size, IRMath::vec3 origin) {
    Recipe recipe;
    recipe.name_ = "region_tools";
    static constexpr auto checks = [] {
        std::array<int, kRegionToolShotCount> result{};
        for (int i = 0; i < static_cast<int>(result.size()); ++i)
            result[static_cast<std::size_t>(i)] = i;
        return result;
    }();
    constexpr std::array phases{"select_hover", "held", "commit", "undo", "cancel"};
    using Type = IRVideo::GuiInputEvent::Type;
    for (const int shot : checks) {
        Segment segment;
        const int phase = shot % kRegionToolPhaseCount;
        segment.label_ = "region_tools_" + std::to_string(shot / kRegionToolPhaseCount) + "_" +
                         (regionToolIsLine(shot) ? "line_" : "box_") + phases[phase];
        segment.zoom_ = 8.0f;
        const auto event = [&](int frame, Type type, IRInput::KeyMouseButtons button) {
            IRVideo::GuiInputEvent input{};
            input.frameOffset_ = frame;
            input.type_ = type;
            input.button_ = button;
            segment.events_.push_back(input);
        };
        const auto moveWorld = [&](int frame, IRMath::ivec3 cell) {
            segment.aims_.push_back(
                {static_cast<int>(segment.events_.size()),
                 origin + IRMath::vec3(cell) + IRMath::vec3(0, 0, 0.5f)}
            );
            event(frame, Type::MOVE, IRInput::kMouseButtonNull);
        };
        const auto clickTool = [&](int button) {
            segment.guiAims_.push_back(
                {static_cast<int>(segment.events_.size()), sculptButtonCenter(button)}
            );
            event(0, Type::MOVE, IRInput::kMouseButtonNull);
            event(2, Type::PRESS, IRInput::kMouseButtonLeft);
            event(4, Type::RELEASE, IRInput::kMouseButtonLeft);
        };
        if (phase == 0) {
            clickTool(regionToolIsLine(shot) ? 4 : 3);
            moveWorld(7, regionToolStart(size));
        } else if (phase == 1 || phase == 4) {
            moveWorld(0, regionToolStart(size));
            event(2, Type::PRESS, IRInput::kMouseButtonLeft);
            moveWorld(5, regionToolEnd(size));
            if (phase == 4) {
                event(8, Type::PRESS, IRInput::kKeyButtonEscape);
                event(9, Type::RELEASE, IRInput::kKeyButtonEscape);
                event(10, Type::RELEASE, IRInput::kMouseButtonLeft);
            }
        } else if (phase == 2) {
            event(2, Type::RELEASE, IRInput::kMouseButtonLeft);
        } else {
            clickTool(6);
        }
        segment.assertions_.push_back(
            IRPrefab::GuiTest::predicate(
                &evaluateRegionToolCheck,
                &checks[shot],
                "ui_region_preview_commit_undo"
            )
        );
        recipe.segments_.push_back(std::move(segment));
    }
    return recipe;
}

} // namespace IRVoxelEditor::Session

#endif
