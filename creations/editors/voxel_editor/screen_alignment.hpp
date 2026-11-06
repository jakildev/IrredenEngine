#ifndef IR_VOXEL_EDITOR_SCREEN_ALIGNMENT_H
#define IR_VOXEL_EDITOR_SCREEN_ALIGNMENT_H

#include "session_builder.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace IRVoxelEditor {

enum class ScreenAlignmentPhase {
    GRID_OFF,
    GRID_ON,
    NORMALS,
    HOVER,
    COMMIT,
    OCCLUDER_BASE,
    OCCLUDED_GHOST,
    GHOST_ONLY,
    FLOOR_HOVER,
    FLOOR_COMMIT,
};

inline constexpr int kScreenAlignmentPhaseCount = 10;
inline constexpr int kScreenAlignmentCaseCount = 7;
inline constexpr std::array<std::uint8_t, 3> kScreenAlignmentSourceColor{224, 64, 32};
inline constexpr std::array<std::uint8_t, 3> kScreenAlignmentPaintColor{60, 200, 120};

struct ScreenAlignmentCase {
    int yawDegrees_;
    float zoom_;
    IRMath::vec2 pan_;
    IRMath::ivec3 normal_;
};

inline const std::array<ScreenAlignmentCase, kScreenAlignmentCaseCount> kScreenAlignmentCases{{
    {0, 8.0f, {0.0f, 0.0f}, {0, -1, 0}},
    {90, 4.0f, {3.0f, -2.0f}, {1, 0, 0}},
    {180, 8.0f, {-2.0f, 3.0f}, {0, 1, 0}},
    {270, 4.0f, {2.0f, 1.0f}, {-1, 0, 0}},
    {45, 8.0f, {-3.0f, -2.0f}, {0, 0, -1}},
    {30, 4.0f, {1.0f, -3.0f}, {0, 0, -1}},
    {67, 8.0f, {-1.0f, 2.0f}, {0, 0, -1}},
}};

inline const ScreenAlignmentCase &screenAlignmentCase(int shot) {
    return kScreenAlignmentCases[static_cast<std::size_t>(shot / kScreenAlignmentPhaseCount)];
}

inline ScreenAlignmentPhase screenAlignmentPhase(int shot) {
    return static_cast<ScreenAlignmentPhase>(shot % kScreenAlignmentPhaseCount);
}

inline const char *screenAlignmentPhaseName(ScreenAlignmentPhase phase) {
    constexpr std::array names{
        "grid_off",
        "grid_on",
        "normals",
        "hover",
        "commit",
        "occluder_base",
        "occluded_ghost",
        "ghost_only",
        "floor_hover",
        "floor_commit"
    };
    return names[static_cast<std::size_t>(phase)];
}

// Images have the same top-down RGBA layout as readDefaultFramebuffer and PNG output.
// Cursor selection uses observed albedo/normal pixels, never a world projection or pick ray.
class ScreenAlignmentProbe {
  public:
    bool capture(
        int shot,
        std::span<const std::uint8_t> rgba,
        IRMath::ivec2 framebufferSize,
        IRMath::ivec2 windowSize
    ) {
        m_error.clear();
        if (framebufferSize.x <= 0 || framebufferSize.y <= 0 || windowSize.x <= 0 ||
            windowSize.y <= 0 ||
            rgba.size() != static_cast<std::size_t>(framebufferSize.x) * framebufferSize.y * 4) {
            m_error = "invalid framebuffer/window dimensions or RGBA byte count";
            return false;
        }
        const int index = shot / kScreenAlignmentPhaseCount;
        const auto phase = screenAlignmentPhase(shot);
        if (phase == ScreenAlignmentPhase::GRID_OFF) {
            m_baseline.assign(rgba.begin(), rgba.end());
            m_baselineSize = framebufferSize;
            m_baselineCase = index;
            m_cursor[static_cast<std::size_t>(index)].reset();
            int left = framebufferSize.x;
            int right = -1;
            int leftBottom = -1;
            int rightBottom = -1;
            for (int y = 0; y < framebufferSize.y; ++y) {
                for (int x = 0; x < framebufferSize.x; ++x) {
                    const std::size_t offset =
                        (static_cast<std::size_t>(y) * framebufferSize.x + x) * 4;
                    if (rgba[offset] != kScreenAlignmentSourceColor[0] ||
                        rgba[offset + 1] != kScreenAlignmentSourceColor[1] ||
                        rgba[offset + 2] != kScreenAlignmentSourceColor[2])
                        continue;
                    if (x < left) {
                        left = x;
                        leftBottom = y;
                    } else if (x == left) {
                        leftBottom = y;
                    }
                    if (x > right) {
                        right = x;
                        rightBottom = y;
                    } else if (x == right) {
                        rightBottom = y;
                    }
                }
            }
            if (right <= left || leftBottom < 0 || rightBottom < 0) {
                m_error = "albedo baseline has no isolated source voxel silhouette";
                return false;
            }
            const auto slot = static_cast<std::size_t>(index);
            const IRMath::vec2 supportCenter(
                (static_cast<float>(left + right) + 1.0f) * 0.5f,
                (static_cast<float>(leftBottom + rightBottom) + 1.0f) * 0.5f
            );
            m_floorCursor[slot] = IRMath::roundVec(
                supportCenter * IRMath::vec2(windowSize) / IRMath::vec2(framebufferSize)
            );
            return true;
        }
        if (phase != ScreenAlignmentPhase::NORMALS)
            return true;
        if (m_baselineCase != index || m_baselineSize != framebufferSize) {
            m_error = "NORMALS capture has no matching albedo baseline";
            return false;
        }
        const auto &normal = screenAlignmentCase(shot).normal_;
        const std::array<std::uint8_t, 3> normalRgb{
            static_cast<std::uint8_t>(
                normal.x < 0   ? 0
                : normal.x > 0 ? 255
                               : 128
            ),
            static_cast<std::uint8_t>(
                normal.y < 0   ? 0
                : normal.y > 0 ? 255
                               : 128
            ),
            static_cast<std::uint8_t>(
                normal.z < 0   ? 0
                : normal.z > 0 ? 255
                               : 128
            )
        };
        const auto matches = [&](int x, int y) {
            if (x < 0 || y < 0 || x >= framebufferSize.x || y >= framebufferSize.y)
                return false;
            const std::size_t offset = (static_cast<std::size_t>(y) * framebufferSize.x + x) * 4;
            for (std::size_t channel = 0; channel < 3; ++channel)
                if (m_baseline[offset + channel] != kScreenAlignmentSourceColor[channel] ||
                    rgba[offset + channel] != normalRgb[channel])
                    return false;
            return true;
        };
        int bestRadius = 1;
        IRMath::ivec2 bestFramebuffer(0);
        std::optional<IRMath::ivec2> bestWindow;
        std::array<IRMath::ivec2, 4> extremeWindow{};
        std::array<IRMath::ivec2, 4> extremeFramebuffer{};
        bool haveExtremes = false;
        for (int y = 2; y < framebufferSize.y - 2; ++y) {
            for (int x = 2; x < framebufferSize.x - 2; ++x) {
                if (!matches(x, y))
                    continue;
                const IRMath::ivec2 windowPoint = IRMath::roundVec(
                    (IRMath::vec2(x, y) + IRMath::vec2(0.5f)) * IRMath::vec2(windowSize) /
                    IRMath::vec2(framebufferSize)
                );
                const IRMath::ivec2 sample = IRMath::ivec2(
                    IRMath::floor(
                        IRMath::vec2(windowPoint) * IRMath::vec2(framebufferSize) /
                        IRMath::vec2(windowSize)
                    )
                );
                if (!matches(sample.x, sample.y))
                    continue;
                int radius = 0;
                for (int candidate = 1; candidate <= 8; ++candidate) {
                    bool inside = true;
                    for (int offset = -candidate; offset <= candidate && inside; ++offset)
                        inside = matches(sample.x + offset, sample.y - candidate) &&
                                 matches(sample.x + offset, sample.y + candidate) &&
                                 matches(sample.x - candidate, sample.y + offset) &&
                                 matches(sample.x + candidate, sample.y + offset);
                    if (!inside)
                        break;
                    radius = candidate;
                }
                if (radius >= 2) {
                    if (!haveExtremes) {
                        extremeWindow.fill(windowPoint);
                        extremeFramebuffer.fill(sample);
                        haveExtremes = true;
                    }
                    const std::array farther{
                        sample.x<extremeFramebuffer[0].x, sample.x> extremeFramebuffer[1].x,
                        sample.y<extremeFramebuffer[2].y, sample.y> extremeFramebuffer[3].y
                    };
                    for (std::size_t edge = 0; edge < farther.size(); ++edge) {
                        if (!farther[edge])
                            continue;
                        extremeWindow[edge] = windowPoint;
                        extremeFramebuffer[edge] = sample;
                    }
                }
                if (radius <= bestRadius)
                    continue;
                bestRadius = radius;
                bestFramebuffer = sample;
                bestWindow = windowPoint;
            }
        }
        if (!bestWindow) {
            m_error = "no source face has a two-framebuffer-pixel interior at a window point";
            return false;
        }
        const auto slot = static_cast<std::size_t>(index);
        m_cursor[slot] = bestWindow;
        m_hoverCursors[slot].fill(*bestWindow);
        std::size_t uniqueEdges = 0;
        for (const auto candidate : extremeWindow) {
            if (candidate == *bestWindow)
                continue;
            bool duplicate = false;
            for (std::size_t previous = 0; previous < uniqueEdges; ++previous)
                duplicate = duplicate || m_hoverCursors[slot][previous] == candidate;
            if (!duplicate)
                m_hoverCursors[slot][uniqueEdges++] = candidate;
        }
        m_sample[slot] = bestFramebuffer;
        m_framebufferSizes[slot] = framebufferSize;
        m_windowSizes[slot] = windowSize;
        return true;
    }

    std::optional<IRMath::ivec2> cursorWindowPoints(int caseIndex) const {
        return m_cursor[static_cast<std::size_t>(caseIndex)];
    }

    std::span<const IRMath::ivec2> hoverCursorWindowPoints(int caseIndex) const {
        const auto slot = static_cast<std::size_t>(caseIndex);
        return m_cursor[slot] ? std::span<const IRMath::ivec2>(m_hoverCursors[slot])
                              : std::span<const IRMath::ivec2>();
    }

    std::optional<IRMath::ivec2> floorCursorWindowPoints(int caseIndex) const {
        return m_floorCursor[static_cast<std::size_t>(caseIndex)];
    }

    std::string result(int caseIndex) const {
        if (!m_error.empty())
            return m_error;
        const auto slot = static_cast<std::size_t>(caseIndex);
        if (!m_cursor[slot])
            return "awaiting rendered face sample";
        const auto pair = [](IRMath::ivec2 value) {
            return std::to_string(value.x) + "," + std::to_string(value.y);
        };
        return "cursor=" + pair(*m_cursor[slot]) + " sample=" + pair(m_sample[slot]) +
               " framebuffer=" + pair(m_framebufferSizes[slot]) +
               " window=" + pair(m_windowSizes[slot]) +
               " floor_cursor=" + pair(*m_floorCursor[slot]);
    }

  private:
    std::vector<std::uint8_t> m_baseline;
    IRMath::ivec2 m_baselineSize{0};
    int m_baselineCase = -1;
    std::array<std::optional<IRMath::ivec2>, kScreenAlignmentCaseCount> m_cursor;
    std::array<std::optional<IRMath::ivec2>, kScreenAlignmentCaseCount> m_floorCursor;
    std::array<std::array<IRMath::ivec2, 5>, kScreenAlignmentCaseCount> m_hoverCursors{};
    std::array<IRMath::ivec2, kScreenAlignmentCaseCount> m_sample{};
    std::array<IRMath::ivec2, kScreenAlignmentCaseCount> m_framebufferSizes{};
    std::array<IRMath::ivec2, kScreenAlignmentCaseCount> m_windowSizes{};
    std::string m_error;
};

namespace Session {

bool evaluateScreenAlignmentCheck(const void *context, std::string &actual);

inline Recipe buildScreenAlignment() {
    Recipe recipe;
    recipe.name_ = "screen_alignment";
    static constexpr auto checks = [] {
        std::array<int, kScreenAlignmentCaseCount * kScreenAlignmentPhaseCount> result{};
        for (int i = 0; i < static_cast<int>(result.size()); ++i)
            result[static_cast<std::size_t>(i)] = i;
        return result;
    }();
    for (int shot : checks) {
        const auto &test = screenAlignmentCase(shot);
        const auto phase = screenAlignmentPhase(shot);
        Segment segment;
        segment.label_ = "screen_alignment_yaw" + std::to_string(test.yawDegrees_) + "_" +
                         screenAlignmentPhaseName(phase);
        segment.zoom_ = test.zoom_;
        segment.cameraIso_ = test.pan_;
        IRVideo::GuiInputEvent move{};
        move.type_ = IRVideo::GuiInputEvent::Type::MOVE;
        move.frameOffset_ = 2;
        move.screenPx_ = IRMath::ivec2(0);
        segment.events_.push_back(move);
        if (phase == ScreenAlignmentPhase::HOVER) {
            for (int sample = 1; sample < 5; ++sample) {
                move.frameOffset_ = 2 + sample * 2;
                segment.events_.push_back(move);
            }
        }
        if (phase == ScreenAlignmentPhase::COMMIT || phase == ScreenAlignmentPhase::FLOOR_COMMIT) {
            IRVideo::GuiInputEvent event{};
            event.type_ = IRVideo::GuiInputEvent::Type::PRESS;
            event.frameOffset_ = 4;
            event.button_ = IRInput::kMouseButtonLeft;
            segment.events_.push_back(event);
            event.type_ = IRVideo::GuiInputEvent::Type::RELEASE;
            event.frameOffset_ = 6;
            segment.events_.push_back(event);
            if (shot / kScreenAlignmentPhaseCount == 0 && phase == ScreenAlignmentPhase::COMMIT) {
                event.type_ = IRVideo::GuiInputEvent::Type::PRESS;
                event.frameOffset_ = 8;
                event.button_ = IRInput::kKeyButtonF12;
                segment.events_.push_back(event);
                event.type_ = IRVideo::GuiInputEvent::Type::RELEASE;
                event.frameOffset_ = 10;
                segment.events_.push_back(event);
            }
        }
        segment.assertions_.push_back(
            IRPrefab::GuiTest::predicate(
                &evaluateScreenAlignmentCheck,
                &checks[static_cast<std::size_t>(shot)],
                "rendered_pixel_drives_editor"
            )
        );
        recipe.segments_.push_back(std::move(segment));
    }
    return recipe;
}

} // namespace Session
} // namespace IRVoxelEditor

#endif /* IR_VOXEL_EDITOR_SCREEN_ALIGNMENT_H */
