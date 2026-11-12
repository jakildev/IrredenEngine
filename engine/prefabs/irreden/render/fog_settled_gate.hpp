#ifndef IR_PREFAB_FOG_SETTLED_GATE_H
#define IR_PREFAB_FOG_SETTLED_GATE_H

// The gate the fog subject systems share for settled static residents
// (docs/design/static-residents.md, "Deriving systems" and "Fog triggers").
//
// A fog verdict reads world-scoped inputs as well as the subject's own, so
// these systems keep matching settled rows (`IRSystem::GatesSettled`) and
// decide once per archetype node whether to walk them: a settled node is
// walked only while the gate is armed, a settling node is walked on every
// dispatch with its stagger bypassed, and a dynamic node is walked as
// before.
//
// The gate arms when a world-scoped input of the verdict changed since the
// system's last dispatch, and stays armed for `staggerPeriod_` consecutive
// dispatches so every staggered row is visited once:
//
//   field cells, vision sources, published line of sight
//       -> C_CanvasFogOfWar::revision_
//   C_FogRevealSettings, the active canvas, its pool, whether fog is attached
//       -> copies held here, compared once per dispatch
//
// A subject's own inputs (override, channels, hidden policy, class) arm
// nothing: their setters resettle the entity, which is then a settling row.

#include <irreden/ir_entity.hpp>
#include <irreden/ir_job.hpp>
#include <irreden/ir_math.hpp>
#include <irreden/ir_system.hpp>

#include <irreden/render/components/component_canvas_fog_of_war.hpp>
#include <irreden/render/components/component_fog_reveal_settings.hpp>
#include <irreden/system/components/component_static.hpp>
#include <irreden/voxel/components/component_voxel_pool.hpp>

#include <cstdint>
#include <vector>

namespace IRPrefab::Fog {

struct SettledGate {
    /// Settled rows are walked this dispatch.
    bool armed_ = false;
    /// The node the tick is walking holds settling rows: no later dispatch
    /// makes up for a skipped one, so the row ticks bypass their stagger.
    /// Written on the main thread before a node's rows fan out.
    bool nodeSettling_ = false;

    std::uint32_t armedDispatchesLeft_ = 0;
    bool primed_ = false;
    std::uint64_t seenRevision_ = 0;
    // A replaced fog component restarts its revision; its field is a new
    // allocation, so the pair tells the two instances apart.
    const WorldField *seenField_ = nullptr;
    IRComponents::C_FogRevealSettings seenSettings_{};
    IREntity::EntityId seenCanvas_ = IREntity::kNullEntity;
    const IRComponents::C_VoxelPool *seenPool_ = nullptr;
    bool seenFogAttached_ = false;
    IREntity::ComponentId staticTag_ = IREntity::kNullComponent;
    IREntity::ComponentId settledTag_ = IREntity::kNullComponent;

    /// Once per dispatch, from `beginTick`, with the inputs the system just
    /// resolved. `pool` may be null for a system that reads none.
    void begin(
        IREntity::EntityId activeCanvas,
        const IRComponents::C_VoxelPool *pool,
        const IRComponents::C_CanvasFogOfWar *fog,
        const IRComponents::C_FogRevealSettings &settings
    ) {
        IREntity::EntityManager &entities = IREntity::getEntityManager();
        staticTag_ = entities.findComponentType<IRComponents::C_Static>();
        settledTag_ = entities.findComponentType<IRComponents::C_Settled>();
        const std::uint64_t revision = fog != nullptr ? fog->revision_ : 0;
        const bool fogAttached = fog != nullptr;
        const WorldField *field = fog != nullptr ? fog->field_.get() : nullptr;
        const bool changed =
            !primed_ || revision != seenRevision_ || field != seenField_ ||
            fogAttached != seenFogAttached_ ||
            activeCanvas != seenCanvas_ || pool != seenPool_ ||
            settings.showThreshold_ != seenSettings_.showThreshold_ ||
            settings.hideThreshold_ != seenSettings_.hideThreshold_ ||
            settings.staggerPeriod_ != seenSettings_.staggerPeriod_;
        if (changed) {
            primed_ = true;
            seenRevision_ = revision;
            seenField_ = field;
            seenFogAttached_ = fogAttached;
            seenCanvas_ = activeCanvas;
            seenPool_ = pool;
            seenSettings_ = settings;
            armedDispatchesLeft_ = IRMath::max(settings.staggerPeriod_, std::uint32_t{1});
        }
        armed_ = armedDispatchesLeft_ > 0;
        if (armed_) {
            --armedDispatchesLeft_;
        }
        nodeSettling_ = false;
    }

    /// `exclude` for the system's own `beginTick` queries: the rows the
    /// tick will not walk this dispatch are left out of them too.
    IREntity::Archetype queryExclude(IREntity::Archetype exclude = {}) const {
        if (!armed_ && settledTag_ != IREntity::kNullComponent) {
            exclude.insert(settledTag_);
        }
        return exclude;
    }

    /// Runs the system's row tick, `self.tick(entity, component...)` or
    /// `self.tick(component...)`, over one archetype node, in chunks of `grainSize` across the worker
    /// pool when `grainSize > 0`, serially otherwise. A settled node is
    /// skipped unless the gate is armed. Reports the rows walked.
    template <typename Self, typename... Components>
    void walkNode(
        Self &self,
        int grainSize,
        const IREntity::Archetype &type,
        std::vector<IREntity::EntityId> &entities,
        std::vector<Components> &...columns
    ) {
        const bool isStatic =
            staticTag_ != IREntity::kNullComponent && type.contains(staticTag_);
        const bool settled =
            isStatic && settledTag_ != IREntity::kNullComponent && type.contains(settledTag_);
        if (settled && !armed_) {
            IRSystem::reportRowsWalked(0);
            return;
        }
        nodeSettling_ = isStatic && !settled;
        const int rowCount = static_cast<int>(entities.size());
        const auto rows = [&](int rowBegin, int rowEnd) {
            for (int i = rowBegin; i < rowEnd; ++i) {
                if constexpr (requires(Self &s, IREntity::EntityId &id, Components &...row) {
                                  s.tick(id, row...);
                              }) {
                    self.tick(entities[i], columns[i]...);
                } else {
                    self.tick(columns[i]...);
                }
            }
        };
        if (grainSize > 0 && rowCount > grainSize && IRJob::workerCount() > 0 &&
            IRJob::isMainThread()) {
            IRJob::parallelFor(0, rowCount, grainSize, rows);
        } else {
            rows(0, rowCount);
        }
        nodeSettling_ = false;
        IRSystem::reportRowsWalked(static_cast<std::uint64_t>(rowCount));
    }
};

} // namespace IRPrefab::Fog

#endif /* IR_PREFAB_FOG_SETTLED_GATE_H */
