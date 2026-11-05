#ifndef IR_VOXEL_EDITOR_MOTION_PREVIEW_PANEL_H
#define IR_VOXEL_EDITOR_MOTION_PREVIEW_PANEL_H

#include <irreden/render/widgets.hpp>

#include <array>
#include <string>
#include <vector>

namespace IRVoxelEditor {

constexpr IRMath::ivec2 kMotionPreviewButtonPos{528, 52};
constexpr IRMath::ivec2 kMotionPreviewButtonSize{100, 18};
constexpr int kMotionPreviewLastTick = 600;
constexpr float kMotionPreviewTicksPerSecond = 60.0f;

enum class MotionPreviewAction { NONE, PLAY_PAUSE, STEP, RESET, DONE };

class MotionPreviewPanel {
  public:
    void create() {
        using namespace IRPrefab::Widget;
        m_widgets.push_back(makePanel({4, 240}, {240, 104}, "MOTION PREVIEW"));
        m_widgets.push_back(makeLabel(
            {10, 263},
            "AUTO SPIN ONLY / 60 TICKS PER SECOND",
            {},
            IRRender::TextSize::SMALL
        ));
        m_slider = makeSlider(
            {10, 280},
            {228, 16},
            "",
            0.0f,
            static_cast<float>(kMotionPreviewLastTick),
            0.0f,
            IRRender::TextSize::SMALL,
            {.step_ = 1.0f}
        );
        m_widgets.push_back(m_slider);
        constexpr std::array<const char *, 4> labels{"PLAY", "STEP", "RESET", "DONE"};
        for (std::size_t i = 0; i < labels.size(); ++i) {
            m_buttons[i] = makeButton(
                {10 + static_cast<int>(i) * 58, 302},
                {54, 18},
                labels[i],
                IRRender::TextSize::SMALL
            );
            m_widgets.push_back(m_buttons[i]);
        }
        m_status =
            makeLabel({10, 326}, "TICK 0 / AUTHORING UNCHANGED", {}, IRRender::TextSize::SMALL);
        m_widgets.push_back(m_status);
        setVisible(false);
    }

    void setVisible(bool visible) const {
        for (const auto id : m_widgets)
            IRPrefab::Widget::setVisible(id, visible);
    }

    MotionPreviewAction poll() const {
        constexpr std::array actions{
            MotionPreviewAction::PLAY_PAUSE,
            MotionPreviewAction::STEP,
            MotionPreviewAction::RESET,
            MotionPreviewAction::DONE
        };
        for (std::size_t i = 0; i < m_buttons.size(); ++i)
            if (IRPrefab::Widget::wasClicked(m_buttons[i]))
                return actions[i];
        return MotionPreviewAction::NONE;
    }

    int requestedTick() const {
        return IRMath::roundHalfUp(IRPrefab::Widget::sliderValue(m_slider));
    }

    bool scrubbing() const {
        return IRPrefab::Widget::isPressed(m_slider);
    }

    void update(int tick, bool playing) const {
        IRPrefab::Widget::setSliderValue(m_slider, static_cast<float>(tick));
        IRPrefab::Widget::setButtonLabel(m_buttons[0], playing ? "PAUSE" : "PLAY");
        IRPrefab::Widget::setLabelText(
            m_status,
            "TICK " + std::to_string(tick) + " / AUTHORING UNCHANGED"
        );
    }

    const std::vector<IREntity::EntityId> &widgets() const {
        return m_widgets;
    }

  private:
    std::vector<IREntity::EntityId> m_widgets;
    std::array<IREntity::EntityId, 4> m_buttons{};
    IREntity::EntityId m_slider = IREntity::kNullEntity;
    IREntity::EntityId m_status = IREntity::kNullEntity;
};

} // namespace IRVoxelEditor

#endif
