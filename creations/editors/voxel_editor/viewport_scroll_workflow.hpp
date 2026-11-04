#ifndef VIEWPORT_SCROLL_WORKFLOW_H
#define VIEWPORT_SCROLL_WORKFLOW_H

#include "palette.hpp"
#include "session_builder.hpp"

#include <irreden/render/camera.hpp>
#include <irreden/render/components/component_screen_space_marker.hpp>
#include <irreden/render/widgets.hpp>

#include <array>

namespace IRVoxelEditor::Session {

inline constexpr int kViewportScrollShotCount = 12;

inline IRMath::vec2 viewportScrollAim(int shot) {
    return shot < 6 ? IRMath::vec2(800, 650) : IRMath::vec2(835, 670);
}

inline float viewportScrollYaw(int shot) {
    return (shot < 6 ? 30.0f : 67.0f) * IRMath::kPi / 180.0f;
}

inline bool evaluateViewportScroll(const void *context, std::string &actual) {
    const int shot = *static_cast<const int *>(context);
    const int phase = shot % 6;
    const float yaw = IRPrefab::Camera::getYaw();
    const float zoom = IRRender::getCameraZoom().x;
    const bool widgetHovered = IRPrefab::Widget::hoveredWidget() != IREntity::kNullEntity;
    const float expectedYaw = viewportScrollYaw(shot) + (phase == 0   ? .08f
                                                         : phase == 1 ? -.02f
                                                                      : 0.0f);
    const float expectedZoom = phase == 3 ? 8.0f : phase == 4 ? 2.0f : 4.0f;
    const IRMath::vec2 firstAim(IRRender::guiTrixelToScreenPx(viewportScrollAim(shot)));
    bool retainedFirstAim = false;
    bool markerVisible = false;
    IREntity::forEachComponent<IRComponents::C_ScreenSpaceMarker>(
        [&](IREntity::EntityId, const IRComponents::C_ScreenSpaceMarker &marker) {
            retainedFirstAim =
                retainedFirstAim || IRMath::length(marker.windowPoint_ - firstAim) < .001f;
            markerVisible = markerVisible || marker.visible_;
        }
    );
    const bool markerMatches = phase == 0 ? retainedFirstAim && IRRender::hasRotationPivotFocus()
                                          : phase != 2 || !markerVisible;
    actual = "yaw=" + std::to_string(yaw) + " expectedYaw=" + std::to_string(expectedYaw) +
             " zoom=" + std::to_string(zoom) + " expectedZoom=" + std::to_string(expectedZoom) +
             " widgetHovered=" + std::to_string(widgetHovered) +
             " firstAim=" + std::to_string(retainedFirstAim) +
             " markerVisible=" + std::to_string(markerVisible);
    return IRMath::abs(IRMath::wrapAnglePi(yaw - expectedYaw)) < .0001f &&
           IRMath::abs(zoom - expectedZoom) < .0001f && widgetHovered == (phase == 5) &&
           markerMatches;
}

inline Recipe buildViewportScroll() {
    Recipe recipe;
    recipe.name_ = "viewport_scroll";
    static constexpr std::array checks{0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11};
    constexpr std::array labels{
        "horizontal_jitter",
        "reverse",
        "vertical_partial",
        "vertical_threshold",
        "vertical_reverse",
        "palette_isolation"
    };
    const auto palette = paletteSwatchCenterGuiTrixel(4);
    for (const int shot : checks) {
        const int phase = shot % 6;
        const auto viewport = viewportScrollAim(shot);
        Segment segment;
        segment.zoom_ = 4;
        segment.label_ = "scroll_" + std::to_string(shot < 6 ? 30 : 67) + "_" + labels[phase];
        const auto move = [&](int frame, IRMath::vec2 point) {
            IRVideo::GuiInputEvent event{};
            event.frameOffset_ = frame;
            event.type_ = IRVideo::GuiInputEvent::Type::MOVE;
            segment.guiAims_.push_back({static_cast<int>(segment.events_.size()), point});
            segment.events_.push_back(event);
        };
        const auto scroll = [&](int frame, float x, float y) {
            IRVideo::GuiInputEvent event{};
            event.frameOffset_ = frame;
            event.type_ = IRVideo::GuiInputEvent::Type::SCROLL;
            event.scroll_ = {x, y};
            segment.events_.push_back(event);
        };
        const auto button =
            [&](int frame, IRVideo::GuiInputEvent::Type type, IRInput::KeyMouseButtons key) {
                IRVideo::GuiInputEvent event{};
                event.frameOffset_ = frame;
                event.type_ = type;
                event.button_ = key;
                segment.events_.push_back(event);
            };
        move(0, phase == 3 || phase == 5 ? palette : viewport);
        if (phase == 0) {
            // Each horizontal movement carries vertical noise; the sum of that
            // noise alone exceeds a zoom step if the axes are not exclusive.
            scroll(2, .5f, .375f);
            move(3, viewport + IRMath::vec2(44, -25));
            scroll(4, .5f, .375f);
            move(5, viewport + IRMath::vec2(75, 30));
            scroll(6, .5f, .375f);
            scroll(8, .5f, .375f);
        } else if (phase == 1) {
            scroll(2, .75f, -.125f);
            scroll(4, -.5f, .125f);
            scroll(6, -.75f, .125f);
            using Type = IRVideo::GuiInputEvent::Type;
            button(8, Type::PRESS, IRInput::kKeyButtonLeftAlt);
            button(9, Type::PRESS, IRInput::kMouseButtonLeft);
            scroll(11, .125f, 1);
            button(12, Type::RELEASE, IRInput::kKeyButtonLeftAlt);
            scroll(14, .125f, 1);
            button(16, Type::RELEASE, IRInput::kMouseButtonLeft);
        } else if (phase == 2) {
            scroll(2, .25f, 0);
            scroll(3, -.25f, 0);
            for (int frame : {4, 6, 8})
                scroll(frame, .0625f, .25f);
        } else if (phase == 3) {
            move(2, viewport);
            for (int frame : {4, 6, 8, 10})
                scroll(frame, .0625f, .25f);
        } else if (phase == 4) {
            for (int frame : {2, 4, 6, 8})
                scroll(frame, .0625f, -.25f);
        } else {
            scroll(2, 2, .1f);
            scroll(4, .1f, 2);
            scroll(6, 0, .75f);
            move(8, viewport);
            scroll(10, 0, .25f);
            scroll(12, 0, -.25f);
            move(14, palette);
        }
        segment.assertions_.push_back(
            IRPrefab::GuiTest::predicate(
                &evaluateViewportScroll,
                &checks[shot],
                "scroll_axis_yaw_zoom_and_hover"
            )
        );
        recipe.segments_.push_back(std::move(segment));
    }
    return recipe;
}

} // namespace IRVoxelEditor::Session

#endif
