#ifndef COMPONENT_SYSTEM_RELATION_H
#define COMPONENT_SYSTEM_RELATION_H

#include <irreden/system/ir_system_types.hpp>

namespace IRComponents {

struct C_SystemRelation {
    IREntity::Relation relation_;

    C_SystemRelation(IREntity::Relation relation)
        : relation_(relation) {}

    C_SystemRelation()
        : relation_(IREntity::Relation::NONE) {}
};

} // namespace IRComponents

#endif /* COMPONENT_SYSTEM_RELATION_H */
