#ifndef PICK_PRIORITY_H
#define PICK_PRIORITY_H

#include <irreden/entity/ir_entity_types.hpp>

namespace IRPrefab::PickPriority {

struct Candidate {
    IREntity::EntityId entity_ = IREntity::kNullEntity;
    int priority_ = 0;
    int isoDepth_ = 0;
};

inline bool outranks(const Candidate &candidate, const Candidate &current) {
    if (candidate.entity_ == IREntity::kNullEntity) {
        return false;
    }
    if (current.entity_ == IREntity::kNullEntity) {
        return true;
    }
    if (candidate.priority_ != current.priority_) {
        return candidate.priority_ > current.priority_;
    }
    return candidate.isoDepth_ < current.isoDepth_;
}

inline void select(Candidate &current, const Candidate &candidate) {
    if (outranks(candidate, current)) {
        current = candidate;
    }
}

} // namespace IRPrefab::PickPriority

#endif /* PICK_PRIORITY_H */
