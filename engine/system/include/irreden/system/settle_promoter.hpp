#ifndef SETTLE_PROMOTER_H
#define SETTLE_PROMOTER_H

#include <irreden/ir_entity.hpp>
#include <irreden/ir_system.hpp>

#include <irreden/system/components/component_static.hpp>

#include <cstddef>
#include <cstdint>
#include <span>

namespace IRSystem::detail {

/// The promotion's inputs, closed to every other lane
/// (docs/design/static-residents.md, "The engine-owned boundary"): the one
/// call that attaches `C_Settled`, and the two writes to the stamp and the
/// floor it reads. The stamp and the floor are round numbers that only ever
/// rise, so the writes can only delay a promotion.
class SettlePromoter {
  public:
    static std::size_t promoteSettled(std::span<const IREntity::EntityId> entities) {
        IREntity::EntityManager &manager = IREntity::getEntityManager();
        const IREntity::ComponentId staticTag = manager.findComponentType<IRComponents::C_Static>();
        const IREntity::ComponentId settledTag =
            manager.findComponentType<IRComponents::C_Settled>();
        const SystemManager &systems = getSystemManager();
        const std::uint64_t completed = systems.roundsCompleted();
        const std::uint64_t floor = systems.settleFloor();
        std::size_t skipped = 0;
        for (const IREntity::EntityId entity : entities) {
            const IREntity::EntityRecord *record = manager.findRecord(entity);
            if (record == nullptr || record->archetypeNode == nullptr ||
                staticTag == IREntity::kNullComponent ||
                !record->archetypeNode->type_.contains(staticTag) ||
                (settledTag != IREntity::kNullComponent &&
                 record->archetypeNode->type_.contains(settledTag))) {
                ++skipped;
                continue;
            }
            const std::uint64_t stamp = staticRow(*record).settleStamp_;
            if (stamp == 0 || completed < stamp || completed < floor) {
                ++skipped;
                continue;
            }
            manager.attachEngineComponentDeferred<IRComponents::C_Settled>(
                entity,
                IREntity::EngineAttachKey<IRComponents::C_Settled>{}
            );
        }
        return skipped;
    }

    static void restampSettling(IRComponents::C_Static &row) {
        const std::uint64_t next = getSystemManager().roundsOpened() + 1;
        row.settleStamp_ = row.settleStamp_ > next ? row.settleStamp_ : next;
    }

    static bool restampSettling(IREntity::EntityId entity) {
        IREntity::EntityManager &entities = IREntity::getEntityManager();
        const IREntity::EntityRecord *record = entities.findRecord(entity);
        const IREntity::ComponentId staticTag =
            entities.findComponentType<IRComponents::C_Static>();
        if (record == nullptr || record->archetypeNode == nullptr ||
            staticTag == IREntity::kNullComponent ||
            !record->archetypeNode->type_.contains(staticTag)) {
            return false;
        }
        restampSettling(staticRow(*record));
        return true;
    }

    static void raiseSettleFloor() {
        SystemManager &systems = getSystemManager();
        const std::uint64_t next = systems.roundsOpened() + 1;
        systems.m_settleFloor = systems.m_settleFloor > next ? systems.m_settleFloor : next;
    }

  private:
    static IRComponents::C_Static &staticRow(const IREntity::EntityRecord &record) {
        return IREntity::getComponentData<IRComponents::C_Static>(record.archetypeNode)[record.row];
    }
};

} // namespace IRSystem::detail

namespace IRSystem {

/// The one call that attaches `C_Settled`. For each id it re-checks that the
/// entity is live, carries `C_Static` and lacks `C_Settled`, that its stamp
/// is nonzero, and that `roundsCompleted` has reached both the stamp and the
/// settle floor; it then stages the attach for the next structural flush,
/// which drops it if `C_Static` is gone by then. Returns how many ids failed
/// the check and were skipped. Main thread only.
inline std::size_t promoteSettled(std::span<const IREntity::EntityId> entities) {
    return detail::SettlePromoter::promoteSettled(entities);
}

/// Write `roundsOpened + 1` into the settle stamp of a `C_Static` row, or
/// keep a higher one. Returns false when `entity` carries no `C_Static`.
inline bool restampSettling(IREntity::EntityId entity) {
    return detail::SettlePromoter::restampSettling(entity);
}

/// The row form, for the transition step's walk over a settling column.
inline void restampSettling(IRComponents::C_Static &row) {
    detail::SettlePromoter::restampSettling(row);
}

/// Raise the settle floor to `roundsOpened + 1`, or keep a higher one.
inline void raiseSettleFloor() {
    detail::SettlePromoter::raiseSettleFloor();
}

/// See `SystemManager::settleFloor`.
inline std::uint64_t settleFloor() {
    return getSystemManager().settleFloor();
}

} // namespace IRSystem

#endif /* SETTLE_PROMOTER_H */
