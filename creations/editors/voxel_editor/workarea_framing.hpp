#ifndef IR_VOXEL_EDITOR_WORKAREA_FRAMING_H
#define IR_VOXEL_EDITOR_WORKAREA_FRAMING_H

#include <irreden/ir_render.hpp>
#include <irreden/render/layout.hpp>

#include <algorithm>
#include <optional>
#include <span>
#include <vector>

namespace IRVoxelEditor {

constexpr IRMath::ivec2 kFocusButtonPos{862, 60};
constexpr IRMath::ivec2 kFocusButtonSize{96, 18};
constexpr float kWorkareaTop = 106.0f;
constexpr float kWorkareaGutter = 12.0f;
constexpr float kWorkareaFitFraction = 0.85f;

struct EditorViewRect {
    IRMath::vec2 min_{};
    IRMath::vec2 max_{};

    IRMath::vec2 size() const {
        return max_ - min_;
    }
    IRMath::vec2 center() const {
        return (min_ + max_) * 0.5f;
    }
};

// The widest useful area need not be to the right of every dock: on narrow
// windows a module column can occupy that edge while space remains below it.
// This one-shot search considers every horizontal dock boundary and merges
// vertical obstruction intervals within each candidate strip.
inline std::optional<EditorViewRect>
editorWorkarea(IRMath::vec2 canvasSize, std::span<const EditorViewRect> panels) {
    const EditorViewRect viewport{
        {kWorkareaGutter, kWorkareaTop},
        canvasSize - IRMath::vec2(kWorkareaGutter)
    };
    if (viewport.size().x <= 0.0f || viewport.size().y <= 0.0f)
        return std::nullopt;
    std::vector<EditorViewRect> obstacles;
    std::vector<float> edges{viewport.min_.x, viewport.max_.x};
    for (const auto &panel : panels) {
        const EditorViewRect clipped{
            IRMath::max(panel.min_ - IRMath::vec2(kWorkareaGutter), viewport.min_),
            IRMath::min(panel.max_ + IRMath::vec2(kWorkareaGutter), viewport.max_)
        };
        if (clipped.size().x <= 0.0f || clipped.size().y <= 0.0f)
            continue;
        obstacles.push_back(clipped);
        edges.push_back(clipped.min_.x);
        edges.push_back(clipped.max_.x);
    }
    std::sort(edges.begin(), edges.end());
    edges.erase(std::unique(edges.begin(), edges.end()), edges.end());
    std::optional<EditorViewRect> best;
    float bestArea = 0.0f;
    std::vector<IRMath::vec2> intervals;
    intervals.reserve(obstacles.size());
    const auto consider = [&](float left, float right, float top, float bottom) {
        const float area = (right - left) * (bottom - top);
        if (area > bestArea) {
            bestArea = area;
            best = EditorViewRect{{left, top}, {right, bottom}};
        }
    };
    for (std::size_t left = 0; left < edges.size(); ++left) {
        for (std::size_t right = left + 1; right < edges.size(); ++right) {
            intervals.clear();
            for (const auto &obstacle : obstacles) {
                if (obstacle.min_.x < edges[right] && obstacle.max_.x > edges[left])
                    intervals.emplace_back(obstacle.min_.y, obstacle.max_.y);
            }
            std::sort(intervals.begin(), intervals.end(), [](auto first, auto second) {
                return first.x < second.x;
            });
            float top = viewport.min_.y;
            for (const auto &interval : intervals) {
                consider(edges[left], edges[right], top, interval.x);
                top = IRMath::max(top, interval.y);
            }
            consider(edges[left], edges[right], top, viewport.max_.y);
        }
    }
    return best;
}

inline std::optional<EditorViewRect> visibleEditorWorkarea() {
    std::vector<EditorViewRect> panels;
    IREntity::forEachComponent<IRComponents::C_Widget>([&](IREntity::EntityId entity,
                                                           const IRComponents::C_Widget &widget) {
        if (!widget.visible_ || widget.kind_ != IRComponents::WidgetKind::PANEL)
            return;
        const auto position = IREntity::getComponentOptional<IRComponents::C_GuiPosition>(entity);
        if (position) {
            const IRMath::vec2 origin((*position)->pos_);
            panels.push_back({origin, origin + IRMath::vec2(widget.size_)});
        }
    });
    return editorWorkarea(IRMath::vec2(IRPrefab::Layout::guiCanvasSize()), panels);
}

inline float
editorFitZoom(float currentZoom, IRMath::vec2 projectedSize, IRMath::vec2 availableSize) {
    const IRMath::vec2 extent = IRMath::max(projectedSize, IRMath::vec2(1.0f));
    const IRMath::vec2 ratio = availableSize * kWorkareaFitFraction / extent;
    return currentZoom * IRMath::min(ratio.x, ratio.y);
}

inline void moveEditorViewBy(IRMath::vec2 delta) {
    const IRMath::vec2 framebufferDelta = IRRender::windowPointsToFramebufferPx(delta) -
                                          IRRender::windowPointsToFramebufferPx(IRMath::vec2(0.0f));
    IRRender::setCameraPosition2DIso(
        IRRender::getCameraPosition2DIso() +
        IRMath::cameraMoveRelativeToYaw(
            framebufferDelta / IRRender::getTriangleStepSizeScreen(),
            IRRender::getCameraPanYaw()
        )
    );
}

// Points are the world-space corners of the selected volume or preview copies.
// Projection and DPI/letterbox mapping remain owned by the renderer. The camera
// yaw and orbit pivot stay unchanged; only pan and zoom change.
inline bool focusEditorPoints(std::span<const IRMath::vec3> points) {
    if (points.empty())
        return false;
    const auto workarea = visibleEditorWorkarea();
    if (!workarea)
        return false;
    const EditorViewRect screenArea{
        IRMath::vec2(IRRender::guiTrixelToScreenPx(workarea->min_)),
        IRMath::vec2(IRRender::guiTrixelToScreenPx(workarea->max_))
    };
    const auto projectedBounds = [&]() {
        const IRMath::vec2 first(IRRender::worldPos3DToMouseScreenPxExact(points.front()));
        EditorViewRect result{first, first};
        for (const auto &point : points.subspan(1)) {
            const IRMath::vec2 projected(IRRender::worldPos3DToMouseScreenPxExact(point));
            result.min_ = IRMath::min(result.min_, projected);
            result.max_ = IRMath::max(result.max_, projected);
        }
        return result;
    };
    const float requestedZoom =
        editorFitZoom(IRRender::getCameraZoom().x, projectedBounds().size(), screenArea.size());
    IRRender::setCameraZoom(requestedZoom);
    // Discrete camera zoom rounds to the nearest power of two. If it rounded
    // upward, use the lower step so fitting cannot spill behind a panel.
    if (IRRender::getCameraZoom().x > requestedZoom)
        IRRender::setCameraZoom(IRRender::getCameraZoom().x * 0.5f);
    moveEditorViewBy(screenArea.center() - projectedBounds().center());
    return true;
}

} // namespace IRVoxelEditor

#endif
