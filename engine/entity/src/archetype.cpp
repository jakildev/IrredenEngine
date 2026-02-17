#include <irreden/entity/archetype.hpp>

namespace IREntity {
std::string makeComponentStringInternal(const Archetype &type) {
    std::stringstream stream;
    stream << "[ ";
    for (auto i = type.begin(); i != type.end(); i++) {
        stream << *i << " ";
    }
    stream << "]";
    return stream.str();
}
} // namespace IREntity