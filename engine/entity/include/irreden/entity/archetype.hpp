/*
 * Project: Irreden Engine
 * File: archetype.hpp
 * Author: Evin Killian jakildev@gmail.com
 * Created Date: November 2023
 * -----
 * Modified By: <your_name> <Month> <YYYY>
 */

#ifndef ARCHETYPE_H
#define ARCHETYPE_H

#include <irreden/entity/ir_entity_types.hpp>

#include <sstream>

namespace IREntity {
    std::string makeComponentStringInternal(const Archetype& type);

} // namespace IREntity

#endif /* ARCHETYPE_H */
