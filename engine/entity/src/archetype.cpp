/*
 * Project: Irreden Engine
 * File: archetype.cpp
 * Author: Evin Killian jakildev@gmail.com
 * Created Date: November 2023
 * -----
 * Modified By: <your_name> <Month> <YYYY>
 */

#include <irreden/entity/archetype.hpp>

namespace IREntity {
    std::string makeComponentStringInternal(const Archetype& type) {
        std::stringstream stream;
        stream << "[ ";
        for (auto i = type.begin(); i != type.end(); i++) {
            stream << *i << " ";
        }
        stream << "]";
        return stream.str();
    }
} // namespace IREntity