#ifndef IR_PREFAB_STATIC_NOTES_H
#define IR_PREFAB_STATIC_NOTES_H

// The change-note half of IRPrefab::Static, split out so a component header
// can route a mutation to its owning entity without pulling in the transition
// step (docs/design/static-residents.md, "Engine mutators").

#include <irreden/ir_entity.hpp>

#include <cstdint>

namespace IRPrefab::Static {

namespace detail {

// A staged note is the entity id with its kind in the top byte.
enum class NoteKind : std::uint64_t {
    CHANGED = 0,
    HELD = 1,
    THAWED = 2,
    RESETTLED = 3,
};

inline constexpr int kNoteKindShift = 56;

inline void stageNote(IREntity::EntityId entity, NoteKind kind) {
    if (entity == IREntity::kNullEntity) {
        return;
    }
    IREntity::getEntityManager().stageEntityNote(
        (entity & IREntity::IR_ENTITY_ID_BITS) |
        (static_cast<std::uint64_t>(kind) << kNoteKindShift)
    );
}

} // namespace detail

/// Records that something a static entity's derived state depends on has
/// changed. Appends 8 bytes to the calling thread's staging slot and looks
/// nothing up, so it is safe from a worker tick; `drain` resettles a settled
/// entity and restarts the settle stamp of a settling one. A note for an
/// entity that is not static is dropped.
inline void noteChanged(IREntity::EntityId entity) {
    detail::stageNote(entity, detail::NoteKind::CHANGED);
}

/// A deriving system reports that it could not finish for this entity on
/// this dispatch. The entity stays settling and is derived again.
inline void noteHeld(IREntity::EntityId entity) {
    detail::stageNote(entity, detail::NoteKind::HELD);
}

} // namespace IRPrefab::Static

#endif /* IR_PREFAB_STATIC_NOTES_H */
