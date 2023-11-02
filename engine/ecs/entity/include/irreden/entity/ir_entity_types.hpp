#ifndef IR_ENTITY_TYPES_H
#define IR_ENTITY_TYPES_H

#include <cstdint>
#include <set>
#include <memory>

namespace IRECS {

    class EntityManager;
    class ArchetypeGraph;
    class ArchetypeNode;
    class IComponentData;

    using EntityId = std::uint64_t;
    using ComponentId = EntityId;
    using Archetype = std::set<ComponentId>;
    using smart_ArchetypeNode = std::unique_ptr<ArchetypeNode>;
    using smart_ComponentData = std::unique_ptr<IComponentData>;

    constexpr EntityId IR_MAX_ENTITIES =                        0x0000000001FFFFFF;
    constexpr EntityId IR_RESERVED_ENTITIES =                   0x00000000000000FF;
    constexpr EntityId IR_ENTITY_ID_BITS =                      0x00000000FFFFFFFF;
    constexpr EntityId IR_PURE_ENTITY_BIT =                     0x0000000100000000;
    constexpr EntityId IR_ENTITY_FLAG_MARKED_FOR_DELETION =     0x8000000000000000;
    constexpr EntityId kNullEntityId = 0;


    enum IRRelationType {
        CHILD_OF,
        PARENT_TO,
        SIBLING_OF
    };

    enum class IREvents {
        START,
        END
    };

} // namespace IRECS

#endif /* IR_ENTITY_TYPES_H */
