#ifndef IR_VOXEL_EDITOR_ARRAY_WORKFLOW_H
#define IR_VOXEL_EDITOR_ARRAY_WORKFLOW_H

#include "array_panel.hpp"
#include "array_preview.hpp"
#include "authored_voxel_fingerprint.hpp"
#include "session_builder.hpp"

#include <array>

namespace IRVoxelEditor::Session {

inline constexpr int kArrayWorkflowPhaseCount = 5;
inline constexpr int kArrayWorkflowShotCount = 10;

inline float arrayWorkflowYaw(int shot) {
    return (shot < kArrayWorkflowPhaseCount ? 30.0f : 67.0f) * IRMath::kPi / 180.0f;
}

class ArrayWorkflowProbe {
  public:
    bool evaluate(
        int shot,
        const EntityScene &scene,
        const ArrayPreview &preview,
        bool enabled,
        int count,
        float distance,
        bool canUndo,
        std::string &actual
    ) {
        const int phase = shot % kArrayWorkflowPhaseCount;
        if (scene.parts().empty()) {
            actual = "missing source part";
            return false;
        }
        const auto &source =
            IREntity::getComponent<IRComponents::C_VoxelSetNew>(scene.parts()[0].entity_);
        const auto fingerprint = authoredVoxelFingerprint(source);
        const auto collect = [&](const ArrayPreview &proposal) {
            std::vector<IRPrefab::WorldPrimitiveVertex> triangles, lines;
            proposal.append(arrayWorkflowYaw(shot), triangles, lines);
            return triangles;
        };
        bool valid = false;
        if (phase == 0) {
            m_source = fingerprint;
            m_initial.clear();
            m_proposed.clear();
            valid = scene.parts().size() == 1 && m_source.active_ == 2 && !enabled && !canUndo;
        } else if (phase == 1 || phase == 2) {
            auto vertices = collect(preview);
            valid = scene.parts().size() == 1 && fingerprint == m_source && enabled && !canUndo &&
                    preview.copyCount() == count && !vertices.empty();
            if (phase == 1) {
                m_initial = std::move(vertices);
                valid = valid && IRMath::abs(distance - 4.0f) < 0.35f;
            } else {
                valid = valid && IRMath::abs(distance - 8.0f) < 0.35f &&
                        !sameSurface(vertices, m_initial);
                m_proposed = std::move(vertices);
            }
        } else if (phase == 3) {
            std::vector<IRPrefab::WorldPrimitiveVertex> committed, lines;
            valid = fingerprint == m_source && !enabled && canUndo &&
                    scene.parts().size() == static_cast<std::size_t>(count + 1);
            for (std::size_t i = 1; i < scene.parts().size(); ++i) {
                const auto &part = scene.parts()[i];
                const auto &set = IREntity::getComponent<IRComponents::C_VoxelSetNew>(part.entity_);
                const auto local =
                    IREntity::getComponent<IRComponents::C_LocalTransform>(part.entity_);
                const auto parentId =
                    part.parentId_.empty() ? scene.root() : scene.findPart(part.parentId_)->entity_;
                const auto parent =
                    IREntity::getComponent<IRComponents::C_WorldTransform>(parentId);
                ArrayPreview oneCopy;
                const auto result = oneCopy.rebuildVoxels(
                    set.authoredRecords(),
                    set.size_,
                    set.anchor_,
                    local,
                    parent,
                    std::vector<IRComponents::C_LocalTransform>{IRComponents::C_LocalTransform{}},
                    part.mode_,
                    false,
                    0,
                    voxelSetLocalOrigin(set)
                );
                valid = valid && result.ok_ && authoredVoxelFingerprint(set) == m_source;
                oneCopy.append(arrayWorkflowYaw(shot), committed, lines);
            }
            valid = valid && !m_proposed.empty() && sameSurface(committed, m_proposed);
        } else {
            valid = scene.parts().size() == 1 && fingerprint == m_source && !enabled && !canUndo;
        }
        actual = "parts=" + std::to_string(scene.parts().size()) +
                 " preview=" + std::to_string(preview.copyCount()) +
                 " source_cells=" + std::to_string(fingerprint.active_) +
                 " distance=" + std::to_string(distance) +
                 " surface_match=" + std::to_string(valid);
        return valid;
    }

  private:
    AuthoredVoxelFingerprint m_source;
    std::vector<IRPrefab::WorldPrimitiveVertex> m_initial;
    std::vector<IRPrefab::WorldPrimitiveVertex> m_proposed;

    static bool sameSurface(
        const std::vector<IRPrefab::WorldPrimitiveVertex> &a,
        const std::vector<IRPrefab::WorldPrimitiveVertex> &b
    ) {
        if (a.size() != b.size())
            return false;
        for (std::size_t i = 0; i < a.size(); ++i)
            if (IRMath::length(a[i].position_ - b[i].position_) > 0.001f ||
                IRMath::length(a[i].color_ - b[i].color_) > 0.001f)
                return false;
        return true;
    }
};

bool evaluateArrayWorkflowCheck(const void *context, std::string &actual);

inline Recipe buildArrayWorkflow() {
    Recipe recipe;
    recipe.name_ = "array_workflow";
    static constexpr auto checks = [] {
        std::array<int, kArrayWorkflowShotCount> result{};
        for (int i = 0; i < static_cast<int>(result.size()); ++i)
            result[static_cast<std::size_t>(i)] = i;
        return result;
    }();
    constexpr std::array phases{"baseline", "preview", "resize", "apply", "undo"};
    using Type = IRVideo::GuiInputEvent::Type;
    for (const int shot : checks) {
        Segment segment;
        const int phase = shot % kArrayWorkflowPhaseCount;
        segment.label_ = std::string(shot < 5 ? "radial_" : "linear_") + phases[phase];
        segment.zoom_ = 8.0f;
        const auto event = [&](int frame, Type type, IRInput::KeyMouseButtons button) {
            IRVideo::GuiInputEvent input{};
            input.frameOffset_ = frame;
            input.type_ = type;
            input.button_ = button;
            segment.events_.push_back(input);
        };
        const auto move = [&](int frame, IRMath::vec2 point) {
            segment.guiAims_.push_back({static_cast<int>(segment.events_.size()), point});
            event(frame, Type::MOVE, IRInput::kMouseButtonNull);
        };
        const auto click = [&](IRMath::vec2 point) {
            move(0, point);
            event(2, Type::PRESS, IRInput::kMouseButtonLeft);
            event(4, Type::RELEASE, IRInput::kMouseButtonLeft);
        };
        if (phase == 0)
            click(IRMath::vec2(kArrayPanelPos) + IRMath::vec2(60, shot < 5 ? 24 : 37));
        else if (phase == 1)
            click(IRMath::vec2(kArrayPanelPos) + IRMath::vec2(60, 150));
        else if (phase == 2) {
            move(
                0,
                sliderValueGuiTrixel(
                    kArrayRadiusSliderGeometry,
                    kArrayRadiusSliderMin,
                    kArrayRadiusSliderMax,
                    4
                )
            );
            event(2, Type::PRESS, IRInput::kMouseButtonLeft);
            move(
                5,
                sliderValueGuiTrixel(
                    kArrayRadiusSliderGeometry,
                    kArrayRadiusSliderMin,
                    kArrayRadiusSliderMax,
                    8
                )
            );
            event(7, Type::RELEASE, IRInput::kMouseButtonLeft);
        } else if (phase == 3)
            click(kArrayApplyCenter);
        else {
            event(0, Type::PRESS, IRInput::kKeyButtonLeftControl);
            event(2, Type::PRESS, IRInput::kKeyButtonZ);
            event(4, Type::RELEASE, IRInput::kKeyButtonZ);
            event(6, Type::RELEASE, IRInput::kKeyButtonLeftControl);
        }
        segment.assertions_.push_back(
            IRPrefab::GuiTest::predicate(
                &evaluateArrayWorkflowCheck,
                &checks[shot],
                "array_preview_updates_apply_matches_undo"
            )
        );
        recipe.segments_.push_back(std::move(segment));
    }
    return recipe;
}

} // namespace IRVoxelEditor::Session

#endif
