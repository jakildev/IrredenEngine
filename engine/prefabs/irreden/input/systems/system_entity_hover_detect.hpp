#ifndef SYSTEM_ENTITY_HOVER_DETECT_H
#define SYSTEM_ENTITY_HOVER_DETECT_H

#include <irreden/ir_system.hpp>
#include <irreden/ir_entity.hpp>
#include <irreden/ir_render.hpp>
#include <irreden/ir_input.hpp>
#include <irreden/ir_profile.hpp>

#include <irreden/input/components/component_entity_event_handlers.hpp>
#include <irreden/input/components/component_hitbox_2d.hpp>
#include <irreden/input/components/component_hitbox_2d_gui.hpp>

namespace IRSystem {

// Spelling insurance: out-of-tree consumers may name this type directly
// rather than going through the accessor (see #2582).
using EntityEventHandlers = IRComponents::C_EntityEventHandlers;

// The world's Lua handler registry. Lazy-creates the singleton row on first
// call, which `World`'s ctor already did — see the seed in `world.cpp` for
// why that pre-warm matters (a first registration from inside a system tick
// would otherwise be an eager structural change mid-iteration).
inline IRComponents::C_EntityEventHandlers &getEntityEventHandlers() {
    return IREntity::singleton<IRComponents::C_EntityEventHandlers>();
}

struct C_EntityHoverDetectTag {};

template <> struct System<ENTITY_HOVER_DETECT> {
    IREntity::EntityId previousHoveredEntity_ = IREntity::kNullEntity;
    int logThrottleCounter_ = 0;

    void tick(C_EntityHoverDetectTag &) {}

    void beginTick() {
        // Resolve the hovered entity from three sources in priority
        // order: GUI hitbox > world hitbox > trixel entity-id readback.
        // The two hitbox sources scan their archetype columns once per
        // frame here — not per-entity getComponent — and stop at the
        // first hovered_ flag (archetype-iteration order is the
        // deterministic tie-break). Pipeline order
        // HITBOX_MOUSE_TEST{,_GUI} → ENTITY_HOVER_DETECT populates the
        // flags before this read; if a creation omits either hitbox
        // system, its scan finds zero hovered entities and the priority
        // chain falls through.
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

        IREntity::EntityId currentHovered = (guiHovered != IREntity::kNullEntity) ? guiHovered
                                            : (worldHovered != IREntity::kNullEntity)
                                                ? worldHovered
                                                : IRRender::getEntityIdAtMouseTrixel();

        if (++logThrottleCounter_ % 600 == 0 && currentHovered != IREntity::kNullEntity) {
            IRE_LOG_DEBUG("[HoverDetect] eid={}", currentHovered);
        }

        applyHoverTransition(currentHovered);
        dispatchClicks(currentHovered);
    }

    // Hover enter/leave dispatch. Kept separate from beginTick so it is
    // reachable without a GL context — beginTick's three resolution sources
    // go through IRRender/IRInput manager globals a headless test cannot
    // stand up, so this is the half the gtests drive (see #2582).
    void applyHoverTransition(IREntity::EntityId currentHovered) {
        if (currentHovered == previousHoveredEntity_) {
            return;
        }
        IRE_LOG_DEBUG(
            "[HoverDetect] state change: {} -> {}",
            previousHoveredEntity_,
            currentHovered
        );
        // singletonOrNull, never the lazy-creating singleton<>: a mid-tick
        // eager createEntity is a structural change during iteration. No
        // registry yet ≡ the old empty-vectors no-op.
        auto *handlers = IREntity::singletonOrNull<IRComponents::C_EntityEventHandlers>();
        if (handlers != nullptr) {
            if (previousHoveredEntity_ != IREntity::kNullEntity) {
                handlers->fireUnhovered(previousHoveredEntity_);
            }
            if (currentHovered != IREntity::kNullEntity) {
                handlers->fireHovered(currentHovered);
            }
        }
        previousHoveredEntity_ = currentHovered;
    }

    static SystemId create() {
        SystemId id =
            registerSystem<ENTITY_HOVER_DETECT, C_EntityHoverDetectTag>("EntityHoverDetect");
        // previousHoveredEntity_ is an event *payload*, not a cached handle
        // with a lazy-respawn guard, and System<N> members survive
        // resetGameplay (systems are never destroyed). Without this hook the
        // transition after a resetGameplay that destroyed the hovered entity
        // would hand the dead id to every Lua onEntityUnhovered handler.
        // Nulling it here deliberately SUPPRESSES that unhover rather than
        // delivering it against a corpse — the documented behaviour, see
        // input/CLAUDE.md and #2582.
        auto *params = getSystemParams<System<ENTITY_HOVER_DETECT>>(id);
        IREntity::getEntityManager().registerPreDestroyHook([params](IREntity::EntityId destroyed) {
            if (params->previousHoveredEntity_ == destroyed) {
                params->previousHoveredEntity_ = IREntity::kNullEntity;
            }
        });
        return id;
    }

  private:
    void dispatchClicks(IREntity::EntityId currentHovered) {
        const bool rightPressed = IRInput::checkKeyMouseButton(
            KeyMouseButtons::kMouseButtonRight,
            ButtonStatuses::PRESSED
        );
        // Hover test first so the left-button query short-circuits away: a
        // left click is only actionable on a hovered entity, and this keeps
        // the un-hovered steady state at one input query per frame.
        const bool leftPressed =
            currentHovered != IREntity::kNullEntity && IRInput::checkKeyMouseButton(
                                                           KeyMouseButtons::kMouseButtonLeft,
                                                           ButtonStatuses::PRESSED
                                                       );
        if (!rightPressed && !leftPressed) {
            return;
        }
        auto *handlers = IREntity::singletonOrNull<IRComponents::C_EntityEventHandlers>();
        if (handlers == nullptr) {
            return;
        }
        if (rightPressed) {
            handlers->fireRightClick();
            if (currentHovered != IREntity::kNullEntity) {
                IRE_LOG_DEBUG("[Click] Entity {} clicked (right button)", currentHovered);
                handlers->fireClicked(currentHovered, 1);
            }
        }
        if (leftPressed) {
            IRE_LOG_DEBUG("[Click] Entity {} clicked (left button)", currentHovered);
            handlers->fireClicked(currentHovered, 0);
        }
    }
};

} // namespace IRSystem

#endif /* SYSTEM_ENTITY_HOVER_DETECT_H */
