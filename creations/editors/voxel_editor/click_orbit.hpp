#ifndef IR_VOXEL_EDITOR_CLICK_ORBIT_H
#define IR_VOXEL_EDITOR_CLICK_ORBIT_H

#include "authored_voxel_fingerprint.hpp"
#include "session_builder.hpp"

#include <array>
#include <optional>
#include <span>

namespace IRVoxelEditor {

enum class ClickOrbitPhase { BASELINE, PRESSED, HELD, RELEASED, IDLE };

inline constexpr std::array kClickOrbitYaws{0, 30, 67, 90, 120, 240, 270};
inline constexpr int kClickOrbitPhaseCount = 5;
inline constexpr int kClickOrbitCaseCount = static_cast<int>(kClickOrbitYaws.size()) * 2;
inline constexpr int kClickOrbitShotCount = kClickOrbitCaseCount * kClickOrbitPhaseCount;
inline constexpr IRMath::Color kClickOrbitColor{224, 64, 32, 255};

inline int clickOrbitCase(int shot) {
    return shot / kClickOrbitPhaseCount;
}

inline ClickOrbitPhase clickOrbitPhase(int shot) {
    return static_cast<ClickOrbitPhase>(shot % kClickOrbitPhaseCount);
}

inline int clickOrbitYaw(int shot) {
    return kClickOrbitYaws[static_cast<std::size_t>(clickOrbitCase(shot) / 2)];
}

inline bool clickOrbitFloor(int shot) {
    return clickOrbitCase(shot) % 2 != 0;
}

inline bool clickOrbitReanchors(int shot) {
    return !clickOrbitFloor(shot) && (clickOrbitYaw(shot) == 30 || clickOrbitYaw(shot) == 67);
}

inline bool clickOrbitPansAfterRelease(int shot) {
    return !clickOrbitFloor(shot) && clickOrbitYaw(shot) == 0;
}

inline IRInput::KeyMouseButtons clickOrbitButton(int shot) {
    if (!clickOrbitFloor(shot) && clickOrbitYaw(shot) == 30)
        return IRInput::kMouseButtonLeft;
    if (!clickOrbitFloor(shot) && clickOrbitYaw(shot) == 67)
        return IRInput::kMouseButtonMiddle;
    return IRInput::kMouseButtonRight;
}

inline IRInput::KeyMouseButtons clickOrbitModifier(int shot) {
    if (clickOrbitButton(shot) == IRInput::kMouseButtonLeft)
        return IRInput::kKeyButtonLeftAlt;
    if (clickOrbitButton(shot) == IRInput::kMouseButtonMiddle)
        return IRInput::kKeyButtonLeftControl;
    return IRInput::kNullButton;
}

struct ClickOrbitProbe {
    int shot_ = -1;
    AuthoredVoxelFingerprint baseline_;
    std::optional<IRMath::ivec2> cursor_;
    IRMath::vec3 focus_{0.0f};
    IRMath::vec2 anchorIso_{0.0f};
    IRMath::vec2 effective_{0.0f};
    IRMath::vec2 focusWindow_{0.0f};
    IRMath::vec2 markerCentroid_{0.0f};
    float yaw_ = 0.0f;
    unsigned heldFrames_ = 0;
    bool heldStable_ = true;
    std::vector<std::uint8_t> pixels_;

    // Select an interior displayed pixel, independently of the picking ray and
    // world-to-screen projection. The source colour is absent from the UI.
    bool selectSurfaceCursor(IRMath::ivec2 framebuffer, IRMath::ivec2 window) {
        cursor_.reset();
        const auto matches = [&](int x, int y) {
            if (x < 0 || y < 0 || x >= framebuffer.x || y >= framebuffer.y)
                return false;
            const std::size_t offset = (static_cast<std::size_t>(y) * framebuffer.x + x) * 4;
            return pixels_[offset] == kClickOrbitColor.red_ &&
                   pixels_[offset + 1] == kClickOrbitColor.green_ &&
                   pixels_[offset + 2] == kClickOrbitColor.blue_;
        };
        int bestRadius = 3;
        float bestDistance = 0.0f;
        for (int y = 4; y < framebuffer.y - 4; ++y) {
            for (int x = 4; x < framebuffer.x - 4; ++x) {
                if (!matches(x, y))
                    continue;
                const auto windowPoint = IRMath::roundVec(
                    (IRMath::vec2(x, y) + IRMath::vec2(0.5f)) * IRMath::vec2(window) /
                    IRMath::vec2(framebuffer)
                );
                const auto sample = IRMath::ivec2(
                    IRMath::floor(
                        IRMath::vec2(windowPoint) * IRMath::vec2(framebuffer) / IRMath::vec2(window)
                    )
                );
                int radius = 0;
                for (int candidate = 1; candidate <= 12; ++candidate) {
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
                const float distance =
                    IRMath::length(IRMath::vec2(windowPoint) - IRMath::vec2(window) * 0.5f);
                if (radius < bestRadius || (radius == bestRadius && distance <= bestDistance))
                    continue;
                bestRadius = radius;
                bestDistance = distance;
                cursor_ = windowPoint;
            }
        }
        return cursor_.has_value() && bestDistance > 8.0f;
    }
};

namespace Session {

bool evaluateClickOrbitCheck(const void *context, std::string &actual);

inline Recipe buildClickOrbit(IRMath::ivec3 sceneSize) {
    Recipe recipe;
    recipe.name_ = "click_orbit";
    if (sceneSize.x < 8 || sceneSize.y < 8 || sceneSize.z < 6) {
        recipe.errors_.push_back("click_orbit requires a scene of at least 8 x 8 x 6 cells");
        return recipe;
    }
    static constexpr auto checks = [] {
        std::array<int, kClickOrbitShotCount> result{};
        for (int i = 0; i < kClickOrbitShotCount; ++i)
            result[static_cast<std::size_t>(i)] = i;
        return result;
    }();
    constexpr std::array phases{"baseline", "pressed", "held", "released", "idle"};
    using Type = IRVideo::GuiInputEvent::Type;
    for (const int shot : checks) {
        const auto phase = clickOrbitPhase(shot);
        Segment segment;
        segment.label_ = "click_orbit_yaw" + std::to_string(clickOrbitYaw(shot)) +
                         (clickOrbitFloor(shot) ? "_floor_" : "_solid_") +
                         phases[static_cast<std::size_t>(phase)];
        segment.zoom_ = 4.0f;
        segment.cameraIso_ = IRMath::vec2(3.0f, -2.0f);
        const auto event = [&](int frame, Type type, IRInput::KeyMouseButtons button) {
            IRVideo::GuiInputEvent input{};
            input.frameOffset_ = frame;
            input.type_ = type;
            input.button_ = button;
            segment.events_.push_back(input);
        };
        if (phase == ClickOrbitPhase::BASELINE) {
            event(0, Type::MOVE, IRInput::kMouseButtonNull);
            if (!clickOrbitFloor(shot) && clickOrbitYaw(shot) == 30) {
                event(1, Type::PRESS, IRInput::kKeyButtonSpace);
                event(2, Type::PRESS, IRInput::kMouseButtonLeft);
                event(5, Type::MOVE, IRInput::kMouseButtonNull);
                event(7, Type::RELEASE, IRInput::kMouseButtonLeft);
                event(8, Type::RELEASE, IRInput::kKeyButtonSpace);
            } else if (!clickOrbitFloor(shot) && clickOrbitYaw(shot) == 67) {
                event(3, Type::SCROLL, IRInput::kMouseButtonNull);
                segment.events_.back().scroll_ = IRMath::vec2(0.0f, 1.0f);
            }
        } else if (phase == ClickOrbitPhase::PRESSED) {
            event(0, Type::MOVE, IRInput::kMouseButtonNull);
            if (clickOrbitModifier(shot) != IRInput::kNullButton)
                event(1, Type::PRESS, clickOrbitModifier(shot));
            event(3, Type::PRESS, clickOrbitButton(shot));
        } else if (phase == ClickOrbitPhase::HELD) {
            event(2, Type::MOVE, IRInput::kMouseButtonNull);
            event(5, Type::MOVE, IRInput::kMouseButtonNull);
            event(8, Type::MOVE, IRInput::kMouseButtonNull);
        } else if (phase == ClickOrbitPhase::RELEASED) {
            event(2, Type::RELEASE, clickOrbitButton(shot));
            if (clickOrbitModifier(shot) != IRInput::kNullButton)
                event(4, Type::RELEASE, clickOrbitModifier(shot));
        } else if (clickOrbitPansAfterRelease(shot)) {
            event(0, Type::MOVE, IRInput::kMouseButtonNull);
            event(1, Type::PRESS, IRInput::kKeyButtonSpace);
            event(2, Type::PRESS, IRInput::kMouseButtonLeft);
            event(5, Type::MOVE, IRInput::kMouseButtonNull);
            event(7, Type::RELEASE, IRInput::kMouseButtonLeft);
            event(8, Type::RELEASE, IRInput::kKeyButtonSpace);
        } else if (clickOrbitReanchors(shot)) {
            event(0, Type::MOVE, IRInput::kMouseButtonNull);
            if (clickOrbitModifier(shot) != IRInput::kNullButton)
                event(1, Type::PRESS, clickOrbitModifier(shot));
            event(3, Type::PRESS, clickOrbitButton(shot));
            event(7, Type::RELEASE, clickOrbitButton(shot));
            if (clickOrbitModifier(shot) != IRInput::kNullButton)
                event(9, Type::RELEASE, clickOrbitModifier(shot));
        }
        segment.assertions_.push_back(
            IRPrefab::GuiTest::predicate(
                &evaluateClickOrbitCheck,
                &checks[shot],
                "click_origin_stays_anchored"
            )
        );
        recipe.segments_.push_back(std::move(segment));
    }
    return recipe;
}

} // namespace Session
} // namespace IRVoxelEditor

#endif
