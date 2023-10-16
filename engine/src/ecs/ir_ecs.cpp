/*
 * Project: Irreden Engine
 * File: \irreden-engine\src\entity\ir_ecs.cpp
 * Author: Evin Killian jakildev@gmail.com
 * Created Date: October 2023
 * -----
 * Modified By: <your_name> <Month> <YYYY>
 */

#include "ir_ecs.hpp"

#include <sstream>

namespace IRECS {
    std::string makeComponentString(Archetype type) {
        std::stringstream stream;
        stream << "[ ";
        for (auto i = type.begin(); i != type.end(); i++) {
            stream << *i << " ";
        }
        stream << "]";
        return stream.str();
    }
} // namespace IRECS