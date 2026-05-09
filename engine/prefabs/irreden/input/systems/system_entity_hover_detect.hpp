#ifndef SYSTEM_ENTITY_HOVER_DETECT_H
#define SYSTEM_ENTITY_HOVER_DETECT_H

#include <irreden/ir_system.hpp>
#include <irreden/ir_entity.hpp>
#include <irreden/ir_render.hpp>
#include <irreden/ir_input.hpp>
#include <irreden/ir_profile.hpp>

#include <irreden/input/components/component_hitbox_2d.hpp>
#include <irreden/input/components/component_hitbox_2d_gui.hpp>

#include <sol/sol.hpp>
#include <vector>
#include <algorithm>

namespace IRSystem {

struct EntityEventHandlers {
    struct HandlerEntry {
        int id;
        sol::protected_function fn;
    };

    std::vector<HandlerEntry> onHovered;
    std::vector<HandlerEntry> onUnhovered;
    std::vector<HandlerEntry> onClicked;
    std::vector<HandlerEntry> onRightClick;
    int nextId = 1;

    int addOnHovered(sol::protected_function fn) {
        int id = nextId++;
        onHovered.push_back({id, std::move(fn)});
        return id;
    }

    int addOnUnhovered(sol::protected_function fn) {
        int id = nextId++;
        onUnhovered.push_back({id, std::move(fn)});
        return id;
    }

    int addOnClicked(sol::protected_function fn) {
        int id = nextId++;
        onClicked.push_back({id, std::move(fn)});
        return id;
    }

    int addOnRightClick(sol::protected_function fn) {
        int id = nextId++;
        onRightClick.push_back({id, std::move(fn)});
        return id;
    }

    void removeHandler(int handlerId) {
        auto eraseById = [handlerId](std::vector<HandlerEntry> &vec) {
            vec.erase(
                std::remove_if(
                    vec.begin(),
                    vec.end(),
                    [handlerId](const HandlerEntry &e) { return e.id == handlerId; }
                ),
                vec.end()
            );
        };
        eraseById(onHovered);
        eraseById(onUnhovered);
        eraseById(onClicked);
        eraseById(onRightClick);
    }

    void fireHovered(IREntity::EntityId entityId) {
        for (auto &entry : onHovered) {
            auto result = entry.fn(entityId);
            if (!result.valid()) {
                sol::error err = result;
                IRE_LOG_ERROR("onEntityHovered handler error: {}", err.what());
            }
        }
    }

    void fireUnhovered(IREntity::EntityId entityId) {
        for (auto &entry : onUnhovered) {
            auto result = entry.fn(entityId);
            if (!result.valid()) {
                sol::error err = result;
                IRE_LOG_ERROR("onEntityUnhovered handler error: {}", err.what());
            }
        }
    }

    void fireClicked(IREntity::EntityId entityId, int button) {
        for (auto &entry : onClicked) {
            auto result = entry.fn(entityId, button);
            if (!result.valid()) {
                sol::error err = result;
                IRE_LOG_ERROR("onEntityClicked handler error: {}", err.what());
            }
        }
    }

    void fireRightClick() {
        for (auto &entry : onRightClick) {
            auto result = entry.fn();
            if (!result.valid()) {
                sol::error err = result;
                IRE_LOG_ERROR("onRightClick handler error: {}", err.what());
            }
        }
    }
};

inline EntityEventHandlers &getEntityEventHandlers() {
    static EntityEventHandlers instance;
    return instance;
}

struct C_EntityHoverDetectTag {};

template <> struct System<ENTITY_HOVER_DETECT> {
    static SystemId create() {
        static IREntity::EntityId previousHoveredEntity = IREntity::kNullEntity;

        return createSystem<C_EntityHoverDetectTag>(
            "EntityHoverDetect",
            [](C_EntityHoverDetectTag &) {},
            []() {
                // Resolve the hovered entity from three sources in
                // priority order: GUI hitbox > world hitbox > trixel
                // entity-id readback. The two hitbox sources scan their
                // archetype columns once per frame in this beginTick —
                // not per-entity getComponent — and stop at the first
                // hovered_ flag (archetype-iteration order is the
                // deterministic tie-break). Pipeline order
                // HITBOX_MOUSE_TEST{,_GUI} → ENTITY_HOVER_DETECT
                // populates the flags before this read; if a creation
                // omits either hitbox system, its scan finds zero
                // hovered entities and the priority chain falls through.
                IREntity::EntityId guiHovered = IREntity::kNullEntity;
                IREntity::forEachComponent<C_HitBox2DGui>(
                    [&guiHovered](IREntity::EntityId &id, C_HitBox2DGui &hitbox) {
                        if (guiHovered == IREntity::kNullEntity && hitbox.hovered_) {
                            guiHovered = id;
                        }
                    }
                );

                IREntity::EntityId worldHovered = IREntity::kNullEntity;
                IREntity::forEachComponent<C_HitBox2D>(
                    [&worldHovered](IREntity::EntityId &id, C_HitBox2D &hitbox) {
                        if (worldHovered == IREntity::kNullEntity && hitbox.hovered_) {
                            worldHovered = id;
                        }
                    }
                );

                IREntity::EntityId currentHovered = (guiHovered != IREntity::kNullEntity)
                                                        ? guiHovered
                                                    : (worldHovered != IREntity::kNullEntity)
                                                        ? worldHovered
                                                        : IRRender::getEntityIdAtMouseTrixel();
                auto &handlers = getEntityEventHandlers();

                static int logCounter = 0;
                if (++logCounter % 600 == 0 && currentHovered != IREntity::kNullEntity) {
                    IRE_LOG_DEBUG("[HoverDetect] eid={}", currentHovered);
                }

                if (currentHovered != previousHoveredEntity) {
                    IRE_LOG_DEBUG(
                        "[HoverDetect] state change: {} -> {}",
                        previousHoveredEntity,
                        currentHovered
                    );
                    if (previousHoveredEntity != IREntity::kNullEntity) {
                        handlers.fireUnhovered(previousHoveredEntity);
                    }
                    if (currentHovered != IREntity::kNullEntity) {
                        handlers.fireHovered(currentHovered);
                    }
                    previousHoveredEntity = currentHovered;
                }

                if (IRInput::checkKeyMouseButton(
                        KeyMouseButtons::kMouseButtonRight,
                        ButtonStatuses::PRESSED
                    )) {
                    handlers.fireRightClick();
                    if (currentHovered != IREntity::kNullEntity) {
                        IRE_LOG_DEBUG("[Click] Entity {} clicked (right button)", currentHovered);
                        handlers.fireClicked(currentHovered, 1);
                    }
                }
                if (currentHovered != IREntity::kNullEntity) {
                    if (IRInput::checkKeyMouseButton(
                            KeyMouseButtons::kMouseButtonLeft,
                            ButtonStatuses::PRESSED
                        )) {
                        IRE_LOG_DEBUG("[Click] Entity {} clicked (left button)", currentHovered);
                        handlers.fireClicked(currentHovered, 0);
                    }
                }
            }
        );
    }
};

} // namespace IRSystem

#endif /* SYSTEM_ENTITY_HOVER_DETECT_H */
