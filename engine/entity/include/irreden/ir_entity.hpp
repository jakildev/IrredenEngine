#ifndef IR_ENTITY_H
#define IR_ENTITY_H

#include <irreden/ir_math.hpp>

#include <irreden/entity/ir_entity_types.hpp>
#include <irreden/entity/entity_manager.hpp>
#include <irreden/entity/prefabs.hpp>

#include <irreden/common/components/component_local_transform.hpp>
#include <irreden/common/components/component_world_transform.hpp>
#include <irreden/common/components/component_persistent.hpp>

#include <tuple>
#include <type_traits>

namespace IREntity {
// Gets created by ir_world and set here.
// Might just make managers static classes in the future, but then
// cleanup order becomes a bit more ambiguous. Will have to write
// cleanup functions, etc.
extern EntityManager *g_entityManager;
EntityManager &getEntityManager();

smart_ComponentData createComponentData(ComponentId type);
std::string makeComponentString(const Archetype &type);
EntityId getRelatedEntityFromArchetype(Archetype type, Relation relation);
EntityId getParentEntityFromArchetype(Archetype type);
// Setup-time only: at most ~3 named entities ("camera", "mainFramebuffer",
// "modifierGlobals"). Do not call inside a per-entity tick.
void setName(EntityId entity, const std::string &name);
EntityId getEntity(const std::string &name);
EntityRecord getEntityRecord(EntityId entity);

inline bool entityExists(EntityId entity) {
    return getEntityManager().entityExists(entity);
}

inline EntityId getLiveEntityCount() {
    return getEntityManager().getLiveEntityCount();
}

template <typename Component>
using LuaCreateEntityFunction = std::function<Component(IRMath::ivec3)>;

template <typename... Components> Archetype getArchetype() {
    return getEntityManager().getArchetype<Components...>();
}

template <typename Component> ComponentId getComponentType() {
    return getEntityManager().getComponentType<Component>();
}

template <typename Component> std::vector<Component> &getComponentData(ArchetypeNode *node) {
    return getEntityManager().getComponentData<Component>(node);
}

std::vector<ArchetypeNode *> queryArchetypeNodesSimple(
    const Archetype &includeComponents, const Archetype &excludeComponents = Archetype{}
);

std::vector<ArchetypeNode *> queryArchetypeNodesRelational(
    const Relation relation,
    const Archetype &includeComponents,
    const Archetype &excludeComponents = Archetype{}
);

inline std::vector<EntityId> collectEntitiesSimple(
    const Archetype &includeComponents, const Archetype &excludeComponents = Archetype{}
) {
    std::vector<EntityId> entities;
    auto nodes = queryArchetypeNodesSimple(includeComponents, excludeComponents);
    for (auto *node : nodes) {
        entities.insert(
            entities.end(),
            node->entities_.begin(),
            node->entities_.begin() + node->length_
        );
    }
    return entities;
}

template <typename Component>
inline void removeComponentsSimple(
    const Archetype &includeComponents, const Archetype &excludeComponents = Archetype{}
) {
    auto entities = collectEntitiesSimple(includeComponents, excludeComponents);
    for (auto entity : entities) {
        getEntityManager().removeComponent<Component>(entity);
    }
}

template <typename Component>
inline void removeComponentsDeferred(
    const Archetype &includeComponents, const Archetype &excludeComponents = Archetype{}
) {
    auto entities = collectEntitiesSimple(includeComponents, excludeComponents);
    for (auto entity : entities) {
        getEntityManager().removeComponentDeferred<Component>(entity);
    }
}

template <typename Component> int countComponents() {
    int total = 0;
    auto nodes = queryArchetypeNodesSimple(getArchetype<Component>());
    for (auto *node : nodes) {
        total += static_cast<int>(getComponentData<Component>(node).size());
    }
    return total;
}

template <typename Component, typename Function> void forEachComponent(Function &&function) {
    auto nodes = queryArchetypeNodesSimple(getArchetype<Component>());
    for (auto *node : nodes) {
        auto &components = getComponentData<Component>(node);
        if constexpr (std::is_invocable_v<Function, Component &>) {
            for (auto &component : components) {
                function(component);
            }
        } else if constexpr (std::is_invocable_v<Function, EntityId &, Component &>) {
            auto &entities = node->entities_;
            for (int i = 0; i < node->length_; ++i) {
                function(entities[i], components[i]);
            }
        } else if constexpr (
            std::is_invocable_v<
                Function,
                const Archetype &,
                std::vector<EntityId> &,
                std::vector<Component> &>
        ) {
            function(node->type_, node->entities_, components);
        } else {
            IR_ASSERT(
                false,
                "Unsupported forEachComponent signature. Use (Component&), "
                "(EntityId&, Component&), or "
                "(const Archetype&, std::vector<EntityId>&, std::vector<Component>&)."
            );
        }
    }
}

bool isPureComponent(ComponentId component);
bool isChildOfRelation(RelationId relation);
NodeId getParentNodeFromRelation(RelationId relation);

// Auto-attached components are added with default values only when the
// caller hasn't supplied one. The duplicate-pack guard matters because
// `insertEntityToNode` emplaces per pack element, not per archetype slot —
// a duplicate type silently appends a second row into the same column
// (entity record points at the first, the caller's value at row+1 is
// orphaned).
template <typename... Components> EntityId createEntity(const Components &...components) {
    using LT = IRComponents::C_LocalTransform;
    using WT = IRComponents::C_WorldTransform;
    constexpr bool kHasLT = (std::is_same_v<LT, Components> || ...);
    constexpr bool kHasWT = (std::is_same_v<WT, Components> || ...);

    auto defaults = std::tuple_cat(
        std::conditional_t<kHasLT, std::tuple<>, std::tuple<LT>>{},
        std::conditional_t<kHasWT, std::tuple<>, std::tuple<WT>>{}
    );

    return std::apply(
        [&](auto &&...defaultComponents) {
            return getEntityManager().createEntity(defaultComponents..., components...);
        },
        defaults
    );
}

template <PrefabTypes type, typename... Args> EntityId createEntity(Args &&...args) {
    return Prefab<type>::create(args...);
}

EntityId setParent(EntityId child, EntityId parent);
void destroyEntity(EntityId entity);
void destroyAllEntities();

/// Scene-transition teardown (#1814): destroy every live gameplay entity,
/// preserving singletons and any entity tagged `C_Persistent`. The renderer's
/// camera + canvas entities are stamped `C_Persistent` at construction so the
/// render context survives. Call at a frame boundary (eager + snapshot-based,
/// like `destroyAllEntities`); the scene machine then re-registers pipelines
/// and spawns the next scene. Lua: `IRWorld.resetGameplay()`.
void resetGameplay();

// Returns the first EntityId of the batch
// Needs to guarentee that entities are ajacent for
// voxel scenes to work
// TODO: Consolidate all createEntity... functions into one variable
// param structure call. Do the same for systems.
template <typename... Components>
std::vector<EntityId> createEntityBatch(int count, const Components &...components) {
    std::vector<EntityId> res;
    for (int i = 0; i < count; i++) {
        res.push_back(createEntity(components...));
    }
    return res;
}

void handleCreateEntityExtraParams(EntityId entity, const CreateEntityExtraParams &params);

// TODO: Pack vectors and send to entityManager all at once
// TODO: Consolidate with Ext version
template <typename... Functions>
std::vector<EntityId>
createEntityBatchWithFunctions(IRMath::ivec3 numEntities, Functions... functions) {
    std::vector<EntityId> res;
    for (int i = 0; i < numEntities.x; i++) {
        for (int j = 0; j < numEntities.y; j++) {
            for (int k = 0; k < numEntities.z; k++) {
                res.push_back(createEntity(functions(IRMath::ivec3{i, j, k})...));
            }
        }
    }
    return res;
}

// TODO: Pack vectors and send to entityManager all at once
template <typename... Functions>
std::vector<EntityId> createEntityBatchWithFunctions_Ext(
    IRMath::ivec3 numEntities, const CreateEntityExtraParams &params, Functions... functions
) {
    const IRMath::vec3 center = IRMath::vec3(numEntities) / IRMath::vec3(2);
    IREntity::CreateEntityCallbackParams callbackParams{IRMath::ivec3{0, 0, 0}, center};
    std::vector<EntityId> res;
    for (int i = 0; i < numEntities.x; i++) {
        for (int j = 0; j < numEntities.y; j++) {
            for (int k = 0; k < numEntities.z; k++) {
                callbackParams.index = IRMath::ivec3{i, j, k};
                res.push_back(createEntity(functions(callbackParams)...));
                handleCreateEntityExtraParams(res.back(), params);
            }
        }
    }
    return res;
}

template <typename Component> Component &getComponent(EntityId entity) {
    return getEntityManager().getComponent<Component>(entity);
}

// Delegates to getEntity — setup-time only, see above.
template <typename Component> Component &getComponent(const std::string &name) {
    return getEntityManager().getComponent<Component>(getEntity(name));
}

template <typename Component> std::optional<Component *> getComponentOptional(EntityId entity) {
    return getEntityManager().getComponentOptional<Component>(entity);
}

template <typename Component> Component &setComponent(EntityId entity, Component component) {
    return getEntityManager().setComponent(entity, component);
}

template <typename Component> void removeComponent(EntityId entity) {
    getEntityManager().removeComponent<Component>(entity);
}

template <typename Component> void removeComponentDeferred(EntityId entity) {
    getEntityManager().removeComponentDeferred<Component>(entity);
}

template <typename Component>
void setComponentDeferred(EntityId entity, const Component &component) {
    getEntityManager().setComponentDeferred<Component>(entity, component);
}

inline void flushStructuralChanges() {
    getEntityManager().flushStructuralChanges();
}

/// Singleton-component API. One entity per component type, lazily created
/// on first access and cached by `ComponentId`. The cache survives entity
/// destruction lazily — calls after `destroyAllEntities` re-create on
/// demand.
///
/// Use for "global game state" components where exactly one record exists
/// per world: framework-level globals (the modifier framework's
/// `C_GlobalModifiers`), per-world settings, scratch containers. Do NOT
/// use as a back door for "things that should be on the manager"; if the
/// data is graphics-device-level state, it belongs on the manager.
///
/// Singleton entities are normal ECS entities and participate in
/// archetype iteration. A `forEachComponent<T>` that matches `T` will see
/// the singleton row.
template <typename Component> EntityId singletonEntity() {
    return getEntityManager().template getOrCreateSingleton<Component>();
}

template <typename Component> EntityId singletonEntityOrNull() {
    return getEntityManager().getSingletonByComponentIdOrNull(
        getEntityManager().template getComponentType<Component>()
    );
}

template <typename Component> Component &singleton() {
    return getComponent<Component>(singletonEntity<Component>());
}

template <typename Component> Component *singletonOrNull() {
    EntityId entity = singletonEntityOrNull<Component>();
    if (entity == kNullEntity) {
        return nullptr;
    }
    auto opt = getComponentOptional<Component>(entity);
    return opt.has_value() ? *opt : nullptr;
}

} // namespace IREntity

#endif /* IR_ENTITY_H */
