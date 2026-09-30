#ifndef LUA_HIERARCHY_BINDINGS_H
#define LUA_HIERARCHY_BINDINGS_H

#include <irreden/ir_entity.hpp>
#include <irreden/script/ir_script_types.hpp>
#include <irreden/script/lua_script.hpp>

#include <sol/sol.hpp>

#include <string>

namespace IRScript::detail {

// Composite-entity hierarchy over CHILD_OF on the `IREntity` table. Entity
// arguments take an integer id or a `LuaEntity`; ids come back as integers.
//
// Every verb except `deferredDestroyTree` mutates archetypes eagerly, so a
// Lua system tick routes it through `IREntity.deferredCall`.
// `forEachChild` reads the inverse view (O(archetype nodes)) and calls `fn`
// over the children as they stood before the first call.
// `destroyEntity` never cascades; the tree verbs visit children before
// parents. A detached canvas is CHILD_OF mainFramebuffer, not its owner, so
// no tree verb reaches it.
inline IREntity::EntityId
requireLiveHierarchyEntity(const sol::object &value, const char *function, const char *param) {
    auto fail = [&](const char *reason) {
        return sol::error{std::string{"IREntity."} + function + ": " + param + reason};
    };
    IREntity::EntityId entity = IREntity::kNullEntity;
    if (value.is<IRScript::LuaEntity>()) {
        entity = value.as<IRScript::LuaEntity>().entity;
    } else if (value.get_type() == sol::type::number) {
        constexpr double kLargestExactLuaInteger = 9007199254740991.0;
        const double number = value.as<double>();
        if (!(number >= 0.0 && number <= kLargestExactLuaInteger)) {
            throw fail(" must be an integer entity id");
        }
        entity = static_cast<IREntity::EntityId>(number);
        if (static_cast<double>(entity) != number) {
            throw fail(" must be an integer entity id");
        }
    } else {
        throw fail(" must be an entity id or LuaEntity");
    }
    // A deferred create has a record but no archetype node until the flush.
    const IREntity::EntityRecord *record = IREntity::getEntityManager().findRecord(entity);
    if (record == nullptr || record->archetypeNode == nullptr) {
        throw fail(" is not a live, placed entity");
    }
    return entity & IREntity::IR_ENTITY_ID_BITS;
}

inline void bindEntityHierarchy(LuaScript &script) {
    sol::table entityTable = script.lua()["IREntity"];

    entityTable["setParent"] = [](sol::object childObj, sol::object parentObj) {
        auto &em = IREntity::getEntityManager();
        const auto child = requireLiveHierarchyEntity(childObj, "setParent", "child");
        const auto parent = requireLiveHierarchyEntity(parentObj, "setParent", "parent");
        if (child == parent || em.isAncestor(child, parent)) {
            throw sol::error{
                "IREntity.setParent: parent is the child or one of its descendants (cycle)"
            };
        }
        em.setRelation(IREntity::CHILD_OF, child, parent);
    };

    entityTable["clearParent"] = [](sol::object childObj) {
        IREntity::getEntityManager().clearParent(
            requireLiveHierarchyEntity(childObj, "clearParent", "child")
        );
    };

    entityTable["getParent"] = [](sol::object entityObj) -> sol::optional<IREntity::EntityId> {
        const IREntity::EntityId parent = IREntity::getEntityManager().getParent(
            requireLiveHierarchyEntity(entityObj, "getParent", "entity")
        );
        if (parent == IREntity::kNullEntity) {
            return sol::nullopt;
        }
        return parent;
    };

    entityTable["forEachChild"] = [](sol::object parentObj, sol::protected_function fn) {
        const auto children = IREntity::getEntityManager().getChildren(
            requireLiveHierarchyEntity(parentObj, "forEachChild", "parent")
        );
        for (IREntity::EntityId child : children) {
            sol::protected_function_result result = fn(child);
            if (!result.valid()) {
                sol::error err = result;
                throw sol::error{
                    std::string{"IREntity.forEachChild: callback error: "} + err.what()
                };
            }
        }
    };

    entityTable["destroyTree"] = [](sol::object rootObj) {
        IREntity::getEntityManager().destroyTree(
            requireLiveHierarchyEntity(rootObj, "destroyTree", "root")
        );
    };

    // Drained with `deferredDestroy` at pipeline end; the set is the tree
    // as it stands at this call.
    entityTable["deferredDestroyTree"] = [](sol::object rootObj) {
        IREntity::getEntityManager().markTreeForDeletion(
            requireLiveHierarchyEntity(rootObj, "deferredDestroyTree", "root")
        );
    };

    entityTable["detachChildren"] = [](sol::object parentObj) {
        IREntity::getEntityManager().detachChildren(
            requireLiveHierarchyEntity(parentObj, "detachChildren", "parent")
        );
    };
}

} // namespace IRScript::detail

#endif /* LUA_HIERARCHY_BINDINGS_H */
