#ifndef IR_PREFAB_STATIC_RESIDENTS_H
#define IR_PREFAB_STATIC_RESIDENTS_H

// IRPrefab::Static — static residents: entities whose owner declared their
// pose inputs fixed, so the per-update systems stop walking them once the
// engine has derived from those inputs. The contract, the three states and
// every "may assume" statement are in docs/design/static-residents.md.
//
//   dynamic --freeze--> settling --(drain, after a complete round)--> settled
//   settled --resettle / any other archetype move--> settling
//   settling or settled --thaw--> dynamic
//
// Every transition is a structural change applied at a flush. The owner
// calls freeze / thaw / resettle; only `drain` promotes.

#include <irreden/ir_entity.hpp>
#include <irreden/ir_system.hpp>

#include <irreden/common/components/component_modifiers.hpp>
#include <irreden/common/components/component_rotation_mode.hpp>
#include <irreden/common/components/component_static_settings.hpp>
#include <irreden/common/components/component_static_state.hpp>
#include <irreden/common/static_notes.hpp>
#include <irreden/render/components/component_canvas_part.hpp>
#include <irreden/render/components/component_entity_canvas.hpp>
#include <irreden/system/components/component_static.hpp>
#include <irreden/voxel/components/component_joint.hpp>
#include <irreden/voxel/components/component_skeleton.hpp>
#include <irreden/voxel/components/component_voxel_set.hpp>
#include <irreden/voxel/voxel_owner.hpp>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace IRPrefab::Static {

/// Why `freeze` did or did not take, and why the drain refuses an entity
/// created or loaded with `C_Static`.
enum class FreezeResult {
    FROZEN,
    ALREADY_STATIC,
    /// No live, placed entity has this id.
    MISSING,
    /// The entity has a parent that is not static. Freeze the parent first;
    /// its tag applies at the next structural flush.
    PARENT_NOT_STATIC,
    /// The rotation mode is not GRID, or the entity owns an entity canvas
    /// or is a canvas part.
    NOT_GRID,
    /// The voxel set is routed through the GPU transform prepass.
    GPU_TRANSFORM,
    /// The entity is a skeleton root or a joint.
    SKELETAL,
};

/// Populations as they stand now, plus the transition step's lifetime
/// counters. The profile report prints these as the `Static residents`
/// witnesses.
struct Stats {
    std::uint64_t static_ = 0;
    std::uint64_t settling_ = 0;
    std::uint64_t settled_ = 0;
    IRComponents::StaticCounters counters_;
};

namespace detail {

inline IREntity::ComponentId staticTagOrNull() {
    return IREntity::getEntityManager().findComponentType<IRComponents::C_Static>();
}

inline IREntity::ComponentId settledTagOrNull() {
    return IREntity::getEntityManager().findComponentType<IRComponents::C_Settled>();
}

inline bool nodeHas(const IREntity::ArchetypeNode *node, IREntity::ComponentId component) {
    return component != IREntity::kNullComponent && node->type_.contains(component);
}

template <typename Component> bool nodeHas(const IREntity::ArchetypeNode *node) {
    return nodeHas(node, IREntity::getEntityManager().findComponentType<Component>());
}

inline const IREntity::EntityRecord *placedRecord(IREntity::EntityId entity) {
    const IREntity::EntityRecord *record = IREntity::getEntityManager().findRecord(entity);
    return record != nullptr && record->archetypeNode != nullptr ? record : nullptr;
}

/// The conditions every row of an archetype node shares.
inline FreezeResult nodeEligibility(const IREntity::ArchetypeNode *node) {
    const IREntity::EntityId parent =
        IREntity::getEntityManager().getParentEntityFromArchetype(node->type_);
    if (parent != IREntity::kNullEntity) {
        // A child that outlived its parent still names it, and is a root.
        const IREntity::EntityRecord *parentRecord = placedRecord(parent);
        if (parentRecord != nullptr && !nodeHas(parentRecord->archetypeNode, staticTagOrNull())) {
            return FreezeResult::PARENT_NOT_STATIC;
        }
    }
    if (nodeHas<IRComponents::C_EntityCanvas>(node) || nodeHas<IRComponents::C_CanvasPart>(node)) {
        return FreezeResult::NOT_GRID;
    }
    if (nodeHas<IRComponents::C_Skeleton>(node) || nodeHas<IRComponents::C_Joint>(node)) {
        return FreezeResult::SKELETAL;
    }
    return FreezeResult::FROZEN;
}

/// The per-row conditions, read from columns the caller resolved once for
/// the node (null when the node lacks the component).
inline FreezeResult rowEligibility(
    const std::vector<IRComponents::C_RotationMode> *rotationModes,
    const std::vector<IRComponents::C_VoxelSetNew> *voxelSets,
    int row
) {
    if (rotationModes != nullptr &&
        (*rotationModes)[row].mode_ != IRComponents::RotationMode::GRID) {
        return FreezeResult::NOT_GRID;
    }
    if (voxelSets != nullptr &&
        (*voxelSets)[row].gpuTransformSlot_ != IRRender::kVoxelTransformStatic) {
        return FreezeResult::GPU_TRANSFORM;
    }
    return FreezeResult::FROZEN;
}

template <typename Component>
std::vector<Component> *columnOrNull(IREntity::ArchetypeNode *node) {
    return nodeHas<Component>(node) ? &IREntity::getComponentData<Component>(node) : nullptr;
}

inline FreezeResult eligibility(const IREntity::EntityRecord &record) {
    const FreezeResult node = nodeEligibility(record.archetypeNode);
    if (node != FreezeResult::FROZEN) {
        return node;
    }
    return rowEligibility(
        columnOrNull<IRComponents::C_RotationMode>(record.archetypeNode),
        columnOrNull<IRComponents::C_VoxelSetNew>(record.archetypeNode),
        record.row
    );
}

inline IRComponents::C_StaticState &state() {
    return IREntity::singleton<IRComponents::C_StaticState>();
}

// A global modifier changed while settled entities exist: their resolved
// fields keep the values from their last round, by contract. Logged once
// per field in a build with logging, so the limit is visible where it bites.
inline void warnGlobalChangeMissesSettled([[maybe_unused]] IRComponents::FieldBindingId field) {
#ifndef IR_RELEASE
    IREntity::EntityManager &entities = IREntity::getEntityManager();
    const IREntity::ComponentId settledTag = settledTagOrNull();
    if (settledTag == IREntity::kNullComponent ||
        IREntity::queryArchetypeNodesSimple(IREntity::Archetype{settledTag}).empty()) {
        return;
    }
    const IREntity::ComponentId stateType =
        entities.findComponentType<IRComponents::C_StaticState>();
    if (stateType == IREntity::kNullComponent ||
        entities.getSingletonByComponentIdOrNull(stateType) == IREntity::kNullEntity) {
        return;
    }
    std::vector<IRComponents::FieldBindingId> &warned = state().warnedGlobalFields_;
    if (std::find(warned.begin(), warned.end(), field) != warned.end()) {
        return;
    }
    warned.push_back(field);
    IRE_LOG_WARN(
        "A global modifier on field {} changed while settled static entities exist; it does "
        "not reach them. Thaw an entity that needs it.",
        field
    );
#endif
}

} // namespace detail

/// True while the entity carries `C_Static`, settling or settled.
inline bool isStatic(IREntity::EntityId entity) {
    const IREntity::EntityRecord *record = detail::placedRecord(entity);
    return record != nullptr && detail::nodeHas(record->archetypeNode, detail::staticTagOrNull());
}

/// True once the deriving systems have run over the entity's frozen inputs
/// and the drain has promoted it.
inline bool isSettled(IREntity::EntityId entity) {
    const IREntity::EntityRecord *record = detail::placedRecord(entity);
    return record != nullptr && detail::nodeHas(record->archetypeNode, detail::settledTagOrNull());
}

/// Declare a live entity static. Adds `C_Static` at the next structural
/// flush; from that flush no pose writer matches it, and the drain promotes
/// it after a complete round. On an ineligible entity it changes nothing
/// and returns the reason. Main thread or worker tick.
inline FreezeResult freeze(IREntity::EntityId entity) {
    const IREntity::EntityRecord *record = detail::placedRecord(entity);
    if (record == nullptr) {
        return FreezeResult::MISSING;
    }
    if (detail::nodeHas(record->archetypeNode, detail::staticTagOrNull())) {
        return FreezeResult::ALREADY_STATIC;
    }
    const FreezeResult result = detail::eligibility(*record);
    if (result != FreezeResult::FROZEN) {
        return result;
    }
    IREntity::setComponentDeferred(entity, IRComponents::C_Static{});
    return FreezeResult::FROZEN;
}

/// Make a static entity dynamic again: removes both tags at the next
/// structural flush, in one archetype move, for the entity and every static
/// descendant. A no-op on a dynamic entity. Main thread or worker tick.
/// Changing an entity's parent does not need it: the entity layer resettles
/// the static subtree inside the call.
inline void thaw(IREntity::EntityId entity) {
    if (!isStatic(entity)) {
        return;
    }
    // Removing C_Static drops C_Settled in the same move.
    IREntity::removeComponentDeferred<IRComponents::C_Static>(entity);
    detail::stageNote(entity, detail::NoteKind::THAWED);
    for (IREntity::EntityId child : IREntity::getEntityManager().getChildren(entity)) {
        thaw(child);
    }
}

/// Have a static entity derived again from its current inputs: the
/// follow-up to a raw write (`getComponent<C_LocalTransform>(e) = ...`), and
/// what every engine mutator that changes a static entity's inputs once
/// does. A settled entity returns to settling at the next structural flush;
/// a settling one has its wait restarted. Cascades to static descendants. A
/// no-op on a dynamic entity. Main thread or worker tick.
inline void resettle(IREntity::EntityId entity) {
    const IREntity::EntityRecord *record = detail::placedRecord(entity);
    if (record == nullptr || !detail::nodeHas(record->archetypeNode, detail::staticTagOrNull())) {
        return;
    }
    const bool settled = detail::nodeHas(record->archetypeNode, detail::settledTagOrNull());
    if (settled) {
        IREntity::removeComponentDeferred<IRComponents::C_Settled>(entity);
    }
    detail::stageNote(
        entity,
        settled ? detail::NoteKind::RESETTLED : detail::NoteKind::CHANGED
    );
    for (IREntity::EntityId child : IREntity::getEntityManager().getChildren(entity)) {
        resettle(child);
    }
}

namespace detail {

// Makes an entity the drain found ineligible dynamic, with every static
// descendant: a descendant's world transform would depend on a parent that
// can now move.
inline void refuseSubtree(IRComponents::C_StaticState &drainState, IREntity::EntityId entity) {
    if (!isStatic(entity)) {
        return;
    }
    IREntity::getEntityManager().removeComponent<IRComponents::C_Static>(entity);
    ++drainState.counters_.refused_;
    for (IREntity::EntityId child : IREntity::getEntityManager().getChildren(entity)) {
        refuseSubtree(drainState, child);
    }
}

// Step 1 of the drain: apply the staged change notes and holds, then the
// entity layer's tracked-change log. Every entry that names a static entity
// restarts its settle stamp, so no row enters the settling state carrying a
// stamp written before its last change. A row the drain has never stamped
// is left for the walk, which treats it as new.
inline void applyNotes(IRComponents::C_StaticState &drainState, std::uint64_t nextRound) {
    IREntity::EntityManager &entities = IREntity::getEntityManager();
    drainState.notes_.clear();
    entities.takeEntityNotes(drainState.notes_);
    const IREntity::ComponentId staticTag = staticTagOrNull();
    const IREntity::ComponentId settledTag = settledTagOrNull();
    for (const IREntity::EntityId note : drainState.notes_) {
        const auto kind = static_cast<NoteKind>(note >> kNoteKindShift);
        const IREntity::EntityId entity = note & IREntity::IR_ENTITY_ID_BITS;
        if (kind == NoteKind::THAWED) {
            ++drainState.counters_.thawed_;
            continue;
        }
        const IREntity::EntityRecord *record = placedRecord(entity);
        if (record == nullptr) {
            continue;
        }
        IREntity::ArchetypeNode *node = record->archetypeNode;
        if (!nodeHas(node, staticTag)) {
            if (auto *sets = columnOrNull<IRComponents::C_VoxelSetNew>(node)) {
                (*sets)[record->row].ownerStatic_ = false;
            }
            continue;
        }
        std::uint64_t &stamp =
            IREntity::getComponentData<IRComponents::C_Static>(node)[record->row].settleStamp_;
        if (nodeHas(node, settledTag)) {
            // Stamped before the removal that returns it to settling.
            stamp = nextRound;
            entities.removeComponent<IRComponents::C_Settled>(entity);
            ++drainState.counters_.resettled_;
            continue;
        }
        if (kind == NoteKind::RESETTLED) {
            ++drainState.counters_.resettled_;
        }
        if (stamp == 0) {
            continue;
        }
        stamp = nextRound;
        if (kind == NoteKind::HELD) {
            ++drainState.counters_.held_;
        } else if (kind == NoteKind::CHANGED) {
            ++drainState.counters_.restarted_;
        }
    }

    // A component attached or detached on a settled entity, a parent
    // change, a parent's destruction: the entity layer dropped C_Settled
    // and logged the id. The entity may no longer be eligible.
    drainState.notes_.clear();
    entities.takeTrackedChanges(drainState.notes_);
    for (const IREntity::EntityId entity : drainState.notes_) {
        const IREntity::EntityRecord *record = placedRecord(entity);
        if (record == nullptr || !nodeHas(record->archetypeNode, staticTag)) {
            continue;
        }
        std::uint64_t &stamp =
            IREntity::getComponentData<IRComponents::C_Static>(record->archetypeNode)[record->row]
                .settleStamp_;
        if (stamp == 0) {
            continue;
        }
        if (eligibility(*record) != FreezeResult::FROZEN) {
            refuseSubtree(drainState, entity);
            continue;
        }
        stamp = nextRound;
    }
}

// Step 2: a global-modifier change the drain has not seen restarts every
// settling entity at once.
inline void raiseGlobalFloor(IRComponents::C_StaticState &drainState, std::uint64_t nextRound) {
    IREntity::EntityManager &entities = IREntity::getEntityManager();
    const IREntity::ComponentId globalsType =
        entities.findComponentType<IRComponents::C_GlobalModifiers>();
    std::uint64_t revision = 0;
    if (globalsType != IREntity::kNullComponent) {
        const IREntity::EntityId globals = entities.getSingletonByComponentIdOrNull(globalsType);
        if (globals != IREntity::kNullEntity) {
            revision = entities.getComponent<IRComponents::C_GlobalModifiers>(globals).revision_;
        }
    }
    if (revision != drainState.seenGlobalRevision_) {
        drainState.seenGlobalRevision_ = revision;
        drainState.globalFloor_ = nextRound;
    }
}

} // namespace detail

/// The transition step: one main-thread call after the final structural
/// flush of each pipeline. In order it applies the staged change notes,
/// raises the global-modifier floor, stamps and checks new settling rows,
/// promotes the rows whose round has completed (lowest stamp first, up to
/// `C_StaticSettings::promotionsPerRound_` per round) and flushes its own
/// structural changes. Its cost is proportional to the settling population
/// and the staged notes. A loop that never calls it promotes nothing.
inline void drain() {
    IR_ASSERT_MAIN_THREAD();
    IREntity::EntityManager &entities = IREntity::getEntityManager();
    const IREntity::ComponentId staticTag = detail::staticTagOrNull();
    if (staticTag == IREntity::kNullComponent || IRSystem::g_systemManager == nullptr) {
        // No registration has named C_Static, so no entity can carry it.
        return;
    }
    using Clock = std::chrono::steady_clock;
    const Clock::time_point drainStart = Clock::now();

    // Resolved before the state reference is taken: a first use creates the
    // settings singleton.
    const std::uint32_t cap =
        IREntity::singleton<IRComponents::C_StaticSettings>().promotionsPerRound_;
    IRComponents::C_StaticState &drainState = detail::state();
    const std::uint64_t roundsCompleted = IRSystem::roundsCompleted();
    const std::uint64_t nextRound = IRSystem::roundsOpened() + 1;

    detail::applyNotes(drainState, nextRound);
    detail::raiseGlobalFloor(drainState, nextRound);

    if (drainState.budgetRound_ != roundsCompleted) {
        drainState.budgetRound_ = roundsCompleted;
        drainState.promotedThisRound_ = 0;
    }
    const bool capped = cap != 0;
    const std::uint64_t remaining =
        !capped ? 0
                : (drainState.promotedThisRound_ >= cap ? 0 : cap - drainState.promotedThisRound_);

    // A max-heap on the stamp keeps the `remaining` lowest stamps without
    // collecting the whole settling population.
    const auto higherStamp = [](const IRComponents::C_StaticState::Candidate &a,
                                const IRComponents::C_StaticState::Candidate &b) {
        return a.stamp_ < b.stamp_;
    };
    std::vector<IRComponents::C_StaticState::Candidate> &candidates = drainState.candidates_;
    candidates.clear();
    drainState.refused_.clear();
    bool heldByGlobal = false;

    const IREntity::ComponentId settledTag = detail::settledTagOrNull();
    const IREntity::Archetype settlingExclude = settledTag == IREntity::kNullComponent
                                                    ? IREntity::Archetype{}
                                                    : IREntity::Archetype{settledTag};
    for (IREntity::ArchetypeNode *node :
         IREntity::queryArchetypeNodesSimple(IREntity::Archetype{staticTag}, settlingExclude)) {
        std::vector<IRComponents::C_Static> &statics =
            IREntity::getComponentData<IRComponents::C_Static>(node);
        bool resolvedNewRowColumns = false;
        FreezeResult nodeResult = FreezeResult::FROZEN;
        std::vector<IRComponents::C_RotationMode> *rotationModes = nullptr;
        std::vector<IRComponents::C_VoxelSetNew> *voxelSets = nullptr;
        for (int row = 0; row < node->length_; ++row) {
            const std::uint64_t stamp = statics[row].settleStamp_;
            if (stamp == 0) {
                if (!resolvedNewRowColumns) {
                    resolvedNewRowColumns = true;
                    nodeResult = detail::nodeEligibility(node);
                    rotationModes = detail::columnOrNull<IRComponents::C_RotationMode>(node);
                    voxelSets = detail::columnOrNull<IRComponents::C_VoxelSetNew>(node);
                }
                const IREntity::EntityId entity =
                    node->entities_[row] & IREntity::IR_ENTITY_ID_BITS;
                if (nodeResult != FreezeResult::FROZEN ||
                    detail::rowEligibility(rotationModes, voxelSets, row) != FreezeResult::FROZEN) {
                    drainState.refused_.push_back(entity);
                    continue;
                }
                statics[row].settleStamp_ = nextRound;
                if (voxelSets != nullptr) {
                    IRComponents::C_VoxelSetNew &set = (*voxelSets)[row];
                    IRPrefab::Voxel::adoptOwner(set, entity);
                    set.ownerStatic_ = true;
                }
                continue;
            }
            if (roundsCompleted < stamp) {
                continue;
            }
            if (roundsCompleted < drainState.globalFloor_) {
                heldByGlobal = true;
                continue;
            }
            if (capped && remaining == 0) {
                continue;
            }
            const IRComponents::C_StaticState::Candidate candidate{
                stamp,
                node->entities_[row] & IREntity::IR_ENTITY_ID_BITS
            };
            if (!capped || candidates.size() < remaining) {
                candidates.push_back(candidate);
                if (capped) {
                    std::push_heap(candidates.begin(), candidates.end(), higherStamp);
                }
            } else if (stamp < candidates.front().stamp_) {
                std::pop_heap(candidates.begin(), candidates.end(), higherStamp);
                candidates.back() = candidate;
                std::push_heap(candidates.begin(), candidates.end(), higherStamp);
            }
        }
    }
    if (heldByGlobal && drainState.heldByGlobalRound_ != roundsCompleted) {
        drainState.heldByGlobalRound_ = roundsCompleted;
        ++drainState.counters_.heldByGlobal_;
    }

    for (const IREntity::EntityId entity : drainState.refused_) {
        detail::refuseSubtree(drainState, entity);
    }

    if (!candidates.empty()) {
        const Clock::time_point promotionStart = Clock::now();
        if (capped) {
            std::sort_heap(candidates.begin(), candidates.end(), higherStamp);
        }
        for (const IRComponents::C_StaticState::Candidate &candidate : candidates) {
            entities.setComponent(candidate.entity_, IRComponents::C_Settled{});
        }
        drainState.promotedThisRound_ += candidates.size();
        drainState.counters_.promoted_ += candidates.size();
        const double promotionMs =
            std::chrono::duration<double, std::milli>(Clock::now() - promotionStart).count();
        drainState.counters_.promotionMsMax_ =
            promotionMs > drainState.counters_.promotionMsMax_
                ? promotionMs
                : drainState.counters_.promotionMsMax_;
    }

    IREntity::flushStructuralChanges();

    const double drainMs =
        std::chrono::duration<double, std::milli>(Clock::now() - drainStart).count();
    IRComponents::StaticCounters &counters = detail::state().counters_;
    counters.drainMsMax_ = drainMs > counters.drainMsMax_ ? drainMs : counters.drainMsMax_;
}

/// Live populations and the transition step's counters. O(archetype
/// nodes); for reports and tests, not a per-entity tick.
inline Stats stats() {
    Stats result;
    const IREntity::ComponentId staticTag = detail::staticTagOrNull();
    if (staticTag == IREntity::kNullComponent) {
        return result;
    }
    const IREntity::ComponentId settledTag = detail::settledTagOrNull();
    for (const IREntity::ArchetypeNode *node :
         IREntity::queryArchetypeNodesSimple(IREntity::Archetype{staticTag})) {
        const std::uint64_t rows = static_cast<std::uint64_t>(node->length_);
        result.static_ += rows;
        if (detail::nodeHas(node, settledTag)) {
            result.settled_ += rows;
        } else {
            result.settling_ += rows;
        }
    }
    if (const IRComponents::C_StaticState *drainState =
            IREntity::singletonOrNull<IRComponents::C_StaticState>()) {
        result.counters_ = drainState->counters_;
    }
    return result;
}

} // namespace IRPrefab::Static

#endif /* IR_PREFAB_STATIC_RESIDENTS_H */
