#ifndef SYSTEM_ENTITY_HOVER_DETECT_H
#define SYSTEM_ENTITY_HOVER_DETECT_H

#include <irreden/ir_system.hpp>
#include <irreden/ir_render.hpp>
#include <irreden/ir_input.hpp>
#include <irreden/ir_profile.hpp>

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

    void removeHandler(int handlerId) {
        auto eraseById = [handlerId](std::vector<HandlerEntry> &vec) {
            vec.erase(
                std::remove_if(
                    vec.begin(), vec.end(),
                    [handlerId](const HandlerEntry &e) { return e.id == handlerId; }
                ),
                vec.end()
            );
        };
        eraseById(onHovered);
        eraseById(onUnhovered);
        eraseById(onClicked);
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
                IREntity::EntityId currentHovered = IRRender::getEntityIdAtMouseTrixel();
                auto &handlers = getEntityEventHandlers();

                static int logCounter = 0;
                if (++logCounter % 600 == 0 && currentHovered != IREntity::kNullEntity) {
                    IRE_LOG_INFO("[HoverDetect] eid={}", currentHovered);
                }

                if (currentHovered != previousHoveredEntity) {
                    IRE_LOG_INFO(
                        "[HoverDetect] state change: {} -> {}",
                        previousHoveredEntity, currentHovered
                    );
                    if (previousHoveredEntity != IREntity::kNullEntity) {
                        handlers.fireUnhovered(previousHoveredEntity);
                    }
                    if (currentHovered != IREntity::kNullEntity) {
                        handlers.fireHovered(currentHovered);
                    }
                    previousHoveredEntity = currentHovered;
                }

                if (currentHovered != IREntity::kNullEntity) {
                    if (IRInput::checkKeyMouseButton(
                            KeyMouseButtons::kMouseButtonLeft,
                            ButtonStatuses::PRESSED)) {
                        IRE_LOG_INFO(
                            "[Click] Entity {} clicked (left button)",
                            currentHovered
                        );
                        handlers.fireClicked(currentHovered, 0);
                    }
                    if (IRInput::checkKeyMouseButton(
                            KeyMouseButtons::kMouseButtonRight,
                            ButtonStatuses::PRESSED)) {
                        IRE_LOG_INFO(
                            "[Click] Entity {} clicked (right button)",
                            currentHovered
                        );
                        handlers.fireClicked(currentHovered, 1);
                    }
                }
            }
        );
    }
};

} // namespace IRSystem

#endif /* SYSTEM_ENTITY_HOVER_DETECT_H */
