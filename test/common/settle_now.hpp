#ifndef IR_TEST_SETTLE_NOW_H
#define IR_TEST_SETTLE_NOW_H

#include <irreden/ir_entity.hpp>
#include <irreden/ir_system.hpp>
#include <irreden/system/settle_promoter.hpp>

#include <cstddef>
#include <initializer_list>
#include <vector>

namespace IRTest {

/// Settles each entity, which must carry `C_Static`, through the one lane
/// that can attach `C_Settled`: a stamp, a completed round past it,
/// `IRSystem::promoteSettled` and the flush that applies it. Moves both round
/// counters forward, so it is for a test that runs no pipelines. Returns the
/// number of ids `promoteSettled` refused.
inline std::size_t settleNow(std::initializer_list<IREntity::EntityId> entities) {
    for (const IREntity::EntityId entity : entities) {
        IRSystem::restampSettling(entity);
    }
    IRSystem::getSystemManager().seedRoundCountersForTest(IRSystem::roundsOpened() + 1);
    const std::vector<IREntity::EntityId> ids(entities);
    const std::size_t skipped = IRSystem::promoteSettled(ids);
    IREntity::flushStructuralChanges();
    return skipped;
}

} // namespace IRTest

#endif /* IR_TEST_SETTLE_NOW_H */
