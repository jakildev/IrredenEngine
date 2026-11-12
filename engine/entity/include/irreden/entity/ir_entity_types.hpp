#ifndef IR_ENTITY_TYPES_H
#define IR_ENTITY_TYPES_H

#include <cstdint>
#include <set>
#include <memory>

#include <irreden/ir_math.hpp>

namespace IREntity {

class EntityManager;
class ArchetypeGraph;
class ArchetypeNode;
class IComponentData;

using EntityId = std::uint64_t;
using ComponentId = EntityId;
using RelationId = EntityId;
using Archetype = std::set<ComponentId>;
using smart_ArchetypeNode = std::unique_ptr<ArchetypeNode>;
using smart_ComponentData = std::unique_ptr<IComponentData>;
using NodeId = std::uint32_t;

// TODO: make this a bitset
constexpr EntityId IR_MAX_ENTITIES = 0x0000000001FFFFFF;
constexpr EntityId IR_RESERVED_ENTITIES = 0x00000000000000FF;
constexpr EntityId IR_ENTITY_ID_BITS = 0x00000000FFFFFFFF;
constexpr EntityId IR_PURE_ENTITY_BIT = 0x0000000100000000;
constexpr EntityId kEntityFlagIsRelation = 0x0000000200000000;
constexpr EntityId kEntityFlagIsSystem = 0x0000000400000000;
constexpr EntityId IR_ENTITY_FLAG_MARKED_FOR_DELETION = 0x8000000000000000; // unused i think

constexpr EntityId kNullEntity = 0;
constexpr ComponentId kNullComponent = 0;
constexpr RelationId kNullRelation = 0;
constexpr NodeId kNullNode = 0;

enum Relation { NONE, CHILD_OF, PARENT_TO, SIBLING_OF };

/// How a component relates to a static resident (an entity carrying
/// `C_Static`, `docs/design/static-residents.md`). `INPUT` components are
/// the entity's pose inputs: a system or query that declares a write to one
/// never matches a static archetype. `DERIVED` components are computed from
/// them: only a deriving system may write one on a static entity.
enum class StaticGuard : std::uint8_t { NONE, INPUT, DERIVED };

/// A component states its guard by specializing this in its own header, so
/// every translation unit that names the component sees the same value.
template <typename Component> inline constexpr StaticGuard kStaticGuard = StaticGuard::NONE;

/// True for a component that does not survive an archetype move it is not
/// part of: attaching or detaching any other component, or changing the
/// parent relation, drops it from the destination archetype.
template <typename Component> inline constexpr bool kClearedByForeignMove = false;

struct CreateEntityExtraParams {
    std::pair<Relation, EntityId> relation = {NONE, kNullEntity};
};

struct CreateEntityCallbackParams {
    IRMath::vec3 center;
    IRMath::ivec3 index;

    CreateEntityCallbackParams(IRMath::ivec3 index, IRMath::vec3 center)
        : center{center}
        , index{index} {}

    CreateEntityCallbackParams()
        : center{0.0f}
        , index{0, 0, 0} {}
};

} // namespace IREntity

#endif /* IR_ENTITY_TYPES_H */
