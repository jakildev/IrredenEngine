#ifndef SYSTEM_UNIT_SELECTION_H
#define SYSTEM_UNIT_SELECTION_H

#include <irreden/ir_entity.hpp>
#include <irreden/ir_input.hpp>
#include <irreden/ir_render.hpp>
#include <irreden/ir_system.hpp>

#include <irreden/common/components/component_controllable_unit.hpp>
#include <irreden/common/components/component_position_global_3d.hpp>
#include <irreden/common/components/component_selected.hpp>
#include <irreden/render/systems/system_debug_overlay.hpp>
#include <irreden/update/components/component_collider_circle.hpp>

#include <algorithm>
#include <vector>

namespace IRSystem {

struct C_UnitSelectionTag {};

namespace UnitSelection {

using namespace IRMath;

enum class SelectionMode {
    ScreenRectangle,
    IsoRectangle,
};

enum class DragPhase {
    Idle,
    Pending,
    Dragging,
};

struct SelectionState {
    SelectionMode mode_ = SelectionMode::ScreenRectangle;
    DragPhase phase_ = DragPhase::Idle;
    bool debugEnabled_ = false;
    vec2 startScreen_ = vec2(0.0f);
    vec2 currentScreen_ = vec2(0.0f);
    vec2 startIso_ = vec2(0.0f);
    vec2 currentIso_ = vec2(0.0f);
    IREntity::EntityId pressedEntity_ = IREntity::kNullEntity;
    ivec3 lastMoveTargetCell_ = ivec3(0);
    int lastMoveRecipientCount_ = 0;
    bool lastMoveFellBackToAll_ = false;
    float dragThresholdPixels_ = 6.0f;
};

inline SelectionState &getState() {
    static SelectionState state;
    return state;
}

inline vec2 mousePositionScreenOverlay() {
    vec2 mouse = IRInput::getMousePositionRender();
    mouse.y = static_cast<float>(IRRender::getViewport().y) - mouse.y;
    return mouse;
}

inline vec3 isoToWorld(const vec2 iso, float zLevel = 0.0f) {
    const float zShift = 2.0f * zLevel;
    const float x = -0.5f * (iso.x + iso.y - zShift);
    const float y = 0.5f * (iso.x - iso.y + zShift);
    return vec3(x, y, zLevel);
}

inline bool isDebugEnabled() {
    return getState().debugEnabled_;
}

inline int countSelectedUnits() {
    int count = 0;
    auto nodes = IREntity::queryArchetypeNodesSimple(
        IREntity::getArchetype<IRComponents::C_Selected, IRComponents::C_ControllableUnit>()
    );
    for (auto *node : nodes) {
        count += node->length_;
    }
    return count;
}

inline void recordSelectionEvent(const char *reason, const int count) {
    if (!isDebugEnabled()) {
        return;
    }

    IRE_LOG_INFO("[UnitSelection][{}] selected={}", reason, count);
}

inline void toggleDebugEnabled() {
    auto &state = getState();
    state.debugEnabled_ = !state.debugEnabled_;
    IRE_LOG_INFO(
        "[UnitSelection] debug {}",
        state.debugEnabled_ ? "enabled" : "disabled"
    );
    if (state.debugEnabled_) {
        IRE_LOG_INFO(
            "[UnitSelection][state] mode={}, selected={}",
            state.mode_ == SelectionMode::ScreenRectangle ? "screen" : "iso",
            countSelectedUnits()
        );
    }
}

inline void recordMoveOrderDebug(
    const ivec3 targetCell,
    const int requestedSelectedCount,
    const int recipientCount,
    const bool fellBackToAll
) {
    auto &state = getState();
    state.lastMoveTargetCell_ = targetCell;
    state.lastMoveRecipientCount_ = recipientCount;
    state.lastMoveFellBackToAll_ = fellBackToAll;

    if (!state.debugEnabled_) {
        return;
    }

    IRE_LOG_INFO(
        "[UnitSelection][move] target=({}, {}, {}), selected={}, recipients={}, fallback_all={}",
        targetCell.x,
        targetCell.y,
        targetCell.z,
        requestedSelectedCount,
        recipientCount,
        fellBackToAll ? 1 : 0
    );
}

constexpr float kDefaultIndicatorRadius = 0.75f;
constexpr float kMinIndicatorRadius = 0.35f;
constexpr float kIndicatorRadiusScale = 0.3f;

inline void deselectAllUnits() {
    IREntity::removeComponentsSimple<IRComponents::C_Selected>(
        IREntity::getArchetype<IRComponents::C_Selected, IRComponents::C_ControllableUnit>()
    );
}

inline void selectUnits(const std::vector<IREntity::EntityId> &entityIds) {
    deselectAllUnits();
    for (auto entityId : entityIds) {
        IREntity::setComponent(entityId, IRComponents::C_Selected{});
    }
    recordSelectionEvent("select", static_cast<int>(entityIds.size()));
}

inline void selectSingleUnit(IREntity::EntityId entityId) {
    if (!IREntity::getComponentOptional<IRComponents::C_ControllableUnit>(entityId).has_value()) {
        deselectAllUnits();
        recordSelectionEvent("deselect", 0);
        return;
    }

    selectUnits({entityId});
}

inline std::vector<IREntity::EntityId> gatherUnitsInScreenRect(const vec2 start, const vec2 end) {
    const vec2 minCorner(std::min(start.x, end.x), std::min(start.y, end.y));
    const vec2 maxCorner(std::max(start.x, end.x), std::max(start.y, end.y));

    std::vector<IREntity::EntityId> selectedIds;
    auto nodes = IREntity::queryArchetypeNodesSimple(
        IREntity::getArchetype<IRComponents::C_ControllableUnit, IRComponents::C_PositionGlobal3D>()
    );
    for (auto *node : nodes) {
        auto &positions = IREntity::getComponentData<IRComponents::C_PositionGlobal3D>(node);
        for (int i = 0; i < node->length_; ++i) {
            const vec2 screenPos = IRDebug::worldToScreen(positions[i].pos_);
            if (screenPos.x >= minCorner.x && screenPos.x <= maxCorner.x &&
                screenPos.y >= minCorner.y && screenPos.y <= maxCorner.y) {
                selectedIds.push_back(node->entities_[i]);
            }
        }
    }

    return selectedIds;
}

inline std::vector<IREntity::EntityId> gatherUnitsInIsoRect(const vec2 start, const vec2 end) {
    const vec2 minCorner(std::min(start.x, end.x), std::min(start.y, end.y));
    const vec2 maxCorner(std::max(start.x, end.x), std::max(start.y, end.y));

    std::vector<IREntity::EntityId> selectedIds;
    auto nodes = IREntity::queryArchetypeNodesSimple(
        IREntity::getArchetype<IRComponents::C_ControllableUnit, IRComponents::C_PositionGlobal3D>()
    );
    for (auto *node : nodes) {
        auto &positions = IREntity::getComponentData<IRComponents::C_PositionGlobal3D>(node);
        for (int i = 0; i < node->length_; ++i) {
            const vec2 isoPos = IRMath::pos3DtoPos2DIso(positions[i].pos_);
            if (isoPos.x >= minCorner.x && isoPos.x <= maxCorner.x &&
                isoPos.y >= minCorner.y && isoPos.y <= maxCorner.y) {
                selectedIds.push_back(node->entities_[i]);
            }
        }
    }

    return selectedIds;
}

inline void drawIsoSelectionRect(const vec2 start, const vec2 end) {
    const vec2 minCorner(std::min(start.x, end.x), std::min(start.y, end.y));
    const vec2 maxCorner(std::max(start.x, end.x), std::max(start.y, end.y));
    const vec4 fillColor(0.2f, 0.8f, 0.2f, 0.15f);
    const vec4 borderColor(0.2f, 0.9f, 0.2f, 0.8f);

    const vec3 bottomLeft = isoToWorld(vec2(minCorner.x, minCorner.y));
    const vec3 bottomRight = isoToWorld(vec2(maxCorner.x, minCorner.y));
    const vec3 topRight = isoToWorld(vec2(maxCorner.x, maxCorner.y));
    const vec3 topLeft = isoToWorld(vec2(minCorner.x, maxCorner.y));

    IRDebug::drawTriangle3D(
        bottomLeft, bottomRight, topRight,
        fillColor.r, fillColor.g, fillColor.b, fillColor.a
    );
    IRDebug::drawTriangle3D(
        bottomLeft, topRight, topLeft,
        fillColor.r, fillColor.g, fillColor.b, fillColor.a
    );

    IRDebug::drawLine3D(bottomLeft, bottomRight, borderColor.r, borderColor.g, borderColor.b, borderColor.a);
    IRDebug::drawLine3D(bottomRight, topRight, borderColor.r, borderColor.g, borderColor.b, borderColor.a);
    IRDebug::drawLine3D(topRight, topLeft, borderColor.r, borderColor.g, borderColor.b, borderColor.a);
    IRDebug::drawLine3D(topLeft, bottomLeft, borderColor.r, borderColor.g, borderColor.b, borderColor.a);
}

inline void drawSelectionRect(const SelectionState &state) {
    if (state.phase_ != DragPhase::Dragging) {
        return;
    }

    if (state.mode_ == SelectionMode::ScreenRectangle) {
        const vec2 minCorner(
            std::min(state.startScreen_.x, state.currentScreen_.x),
            std::min(state.startScreen_.y, state.currentScreen_.y)
        );
        const vec2 maxCorner(
            std::max(state.startScreen_.x, state.currentScreen_.x),
            std::max(state.startScreen_.y, state.currentScreen_.y)
        );

        IRDebug::drawRectScreen(
            minCorner,
            maxCorner,
            vec4(0.2f, 0.8f, 0.2f, 0.15f),
            vec4(0.2f, 0.9f, 0.2f, 0.8f)
        );
        return;
    }

    drawIsoSelectionRect(state.startIso_, state.currentIso_);
}

inline void drawSelectedUnitIndicators() {
    auto nodes = IREntity::queryArchetypeNodesSimple(
        IREntity::getArchetype<
            IRComponents::C_Selected,
            IRComponents::C_ControllableUnit,
            IRComponents::C_PositionGlobal3D>()
    );

    const auto colliderId = IREntity::getComponentType<IRComponents::C_ColliderCircle>();

    for (auto *node : nodes) {
        auto &positions = IREntity::getComponentData<IRComponents::C_PositionGlobal3D>(node);
        const bool hasCollider = node->type_.count(colliderId) > 0;

        if (hasCollider) {
            auto &colliders = IREntity::getComponentData<IRComponents::C_ColliderCircle>(node);
            for (int i = 0; i < node->length_; ++i) {
                const float radius = std::max(kMinIndicatorRadius, colliders[i].radius_ * kIndicatorRadiusScale);
                const vec3 center = positions[i].pos_ + vec3(0.0f, 0.0f, 0.25f);
                IRDebug::drawDiamond3D(center, radius, 1.0f, 1.0f, 0.35f, 0.28f);
            }
        } else {
            for (int i = 0; i < node->length_; ++i) {
                const vec3 center = positions[i].pos_ + vec3(0.0f, 0.0f, 0.25f);
                IRDebug::drawDiamond3D(center, kDefaultIndicatorRadius, 1.0f, 1.0f, 0.35f, 0.28f);
            }
        }
    }
}

} // namespace UnitSelection

template <> struct System<UNIT_SELECTION> {
    static SystemId create() {
        using namespace IRComponents;

        return createSystem<C_UnitSelectionTag>(
            "UnitSelection",
            [](C_UnitSelectionTag &) {},
            nullptr,
            []() {
                auto &state = UnitSelection::getState();

                state.currentScreen_ = UnitSelection::mousePositionScreenOverlay();
                state.currentIso_ = IRRender::mousePosition2DIsoWorldRender();

                if (IRInput::checkKeyMouseButton(IRInput::kKeyButtonTab, IRInput::PRESSED)) {
                    state.mode_ = state.mode_ == UnitSelection::SelectionMode::ScreenRectangle
                                      ? UnitSelection::SelectionMode::IsoRectangle
                                      : UnitSelection::SelectionMode::ScreenRectangle;
                    IRE_LOG_INFO(
                        "[UnitSelection] Mode: {}",
                        state.mode_ == UnitSelection::SelectionMode::ScreenRectangle ? "screen rectangle"
                                                                                     : "iso rectangle"
                    );
                }

                if (IRInput::checkKeyMouseButton(IRInput::kKeyButtonF6, IRInput::PRESSED)) {
                    UnitSelection::toggleDebugEnabled();
                }

                const bool leftPressed = IRInput::checkKeyMouseButton(
                    IRInput::kMouseButtonLeft,
                    IRInput::PRESSED
                );
                const bool leftDown = IRInput::checkKeyMouseButton(
                    IRInput::kMouseButtonLeft,
                    IRInput::HELD
                );
                const bool leftReleased = IRInput::checkKeyMouseButton(
                    IRInput::kMouseButtonLeft,
                    IRInput::RELEASED
                );

                if (leftPressed) {
                    state.phase_ = UnitSelection::DragPhase::Pending;
                    state.startScreen_ = state.currentScreen_;
                    state.startIso_ = state.currentIso_;
                    state.pressedEntity_ = IRRender::getEntityIdAtMouseTrixel();
                }

                if ((leftPressed || leftDown) && state.phase_ == UnitSelection::DragPhase::Pending) {
                    const vec2 delta = state.currentScreen_ - state.startScreen_;
                    const float distanceSquared = delta.x * delta.x + delta.y * delta.y;
                    const float threshold = state.dragThresholdPixels_ * state.dragThresholdPixels_;
                    if (distanceSquared >= threshold) {
                        state.phase_ = UnitSelection::DragPhase::Dragging;
                    }
                }

                if (leftReleased) {
                    if (state.phase_ == UnitSelection::DragPhase::Dragging) {
                        const std::vector<IREntity::EntityId> selectedIds =
                            state.mode_ == UnitSelection::SelectionMode::ScreenRectangle
                                ? UnitSelection::gatherUnitsInScreenRect(state.startScreen_, state.currentScreen_)
                                : UnitSelection::gatherUnitsInIsoRect(state.startIso_, state.currentIso_);
                        UnitSelection::selectUnits(selectedIds);
                    } else if (state.phase_ == UnitSelection::DragPhase::Pending) {
                        UnitSelection::selectSingleUnit(state.pressedEntity_);
                    }

                    state.phase_ = UnitSelection::DragPhase::Idle;
                    state.pressedEntity_ = IREntity::kNullEntity;
                }
            }
        );
    }
};

template <> struct System<UNIT_SELECTION_RENDER> {
    static SystemId create() {
        return createSystem<C_UnitSelectionTag>(
            "UnitSelectionRender",
            [](C_UnitSelectionTag &) {},
            nullptr,
            []() {
                const auto &state = UnitSelection::getState();
                UnitSelection::drawSelectionRect(state);
                UnitSelection::drawSelectedUnitIndicators();
            }
        );
    }
};

} // namespace IRSystem

#endif /* SYSTEM_UNIT_SELECTION_H */
