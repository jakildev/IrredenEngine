/*
 * Project: Irreden Engine
 * File: component_system_relation.hpp
 * Author: Evin Killian jakildev@gmail.com
 * Created Date: November 2023
 * -----
 * Modified By: <your_name> <Month> <YYYY>
 */

#ifndef COMPONENT_SYSTEM_RELATION_H
#define COMPONENT_SYSTEM_RELATION_H

#include <irreden/system/ir_system_types.hpp>

namespace IRComponents {

    struct C_SystemRelation {
        IRECS::Relation relation_;

        C_SystemRelation(
           IRECS::Relation relation
        )
        :   relation_(relation)
        {

        }

        C_SystemRelation()
        :   relation_(IRECS::Relation::NONE)
        {

        }

    };

}

#endif /* COMPONENT_SYSTEM_RELATION_H */
