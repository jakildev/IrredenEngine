#ifndef LUA_SCRIPT_H
#define LUA_SCRIPT_H

// #include <lua54/lua.hpp>

#define SOL_ALL_SAFETIES_ON 1
#include <sol/sol.hpp>
#include <stdexcept>

#include <irreden/ir_math.hpp>
#include <irreden/ir_entity.hpp>
#include <irreden/ir_system.hpp>

#include <irreden/script/ir_script_types.hpp>
#include <irreden/script/lua_archetype_view.hpp>
#include <irreden/script/lua_binding_traits.hpp>
#include <irreden/script/lua_component_data.hpp>

#include <string>
#include <unordered_map>
#include <vector>

namespace IRScript {

class LuaScript {
  public:
    LuaScript();
    LuaScript(const char *filename);

    ~LuaScript();

    inline sol::state &lua() {
        return m_lua;
    }

    sol::table getTable(const char *name);
    void scriptFile(const char *filename);

    void bindCreateEntityBatchFunction();

    // Bind the Lua-driven ECS surface — IRComponent.{register,bindField},
    // IREntity.{addLuaComponent,getLuaComponent,removeLuaComponent,
    // hasLuaComponent}, and IRSystem.registerSystem (T-101 archetype-
    // batched dispatch). Idempotent; safe to call multiple times.
    // Required for any creation that registers Lua-defined components
    // or Lua-defined systems. See docs/design/lua-driven-ecs.md and
    // engine/script/CLAUDE.md.
    void bindLuaDrivenEcs();

    // T-102: register a prefab system NAME so the Lua side's
    // `IRSystem.systemId(SystemName.NAME)` can return its SystemId.
    // Calls `IRSystem::createSystem<NAME>()` once and caches the
    // resulting SystemId in `m_prefabSystemIds`. Re-calling for the
    // same NAME is a no-op (subsequent calls return the cached id),
    // so a creation may safely call it from multiple binding sites
    // — only the first actually creates the system. Mirrors the
    // `registerType` / `registerTypesFromTraits` shape.
    template <IRSystem::SystemName N> IRSystem::SystemId registerPrefabSystem() {
        const int key = static_cast<int>(N);
        auto it = m_prefabSystemIds.find(key);
        if (it != m_prefabSystemIds.end()) {
            return it->second;
        }
        const IRSystem::SystemId id = IRSystem::createSystem<N>();
        m_prefabSystemIds.emplace(key, id);
        return id;
    }

    template <IRSystem::SystemName... Ns> void registerPrefabSystems() {
        (registerPrefabSystem<Ns>(), ...);
    }

    // Cache an already-created prefab SystemId under its enum name. Used
    // when the system was created by an external bootstrap helper —
    // e.g. `IRPrefab::Modifier::registerResolverPipeline()` returns the
    // six modifier-resolver SystemIds and creates the singleton globals
    // entity in the same call. Calling
    // `registerPrefabSystem<MODIFIER_DECAY>()` after that would create a
    // duplicate; this helper records the existing id without recreating.
    void registerPrefabSystemId(IRSystem::SystemName name, IRSystem::SystemId id) {
        m_prefabSystemIds[static_cast<int>(name)] = id;
    }

    // Read access for the Lua-side `IRSystem.systemId` lookup; passed
    // to the binding closure by pointer so the closure reads the live
    // map populated by registerPrefabSystem<N>() calls.
    const std::unordered_map<int, IRSystem::SystemId> *prefabSystemIds() const {
        return &m_prefabSystemIds;
    }

    // True when the C++ component type registered with `luaName`
    // (typically the binding's `registerType<T>("C_Foo")`) was already
    // recorded by a prior `registerType` call. Used by Lua systems to
    // resolve component names against the lua_component_pack the
    // creation has bound.
    bool hasComponentLuaName(const std::string &luaName) const {
        return m_componentByLuaName.find(luaName) != m_componentByLuaName.end();
    }

    // Returns the `ComponentId` recorded for `luaName`, or
    // `IREntity::kNullComponent` if no C++ component type with that
    // Lua name has been registered.
    IREntity::ComponentId componentIdByLuaName(const std::string &luaName) const {
        auto it = m_componentByLuaName.find(luaName);
        if (it == m_componentByLuaName.end()) {
            return IREntity::kNullComponent;
        }
        return it->second;
    }

    // Returns the column accessor pair registered for the C++ component
    // with `componentId`, or nullptr if the component does not have a
    // Lua binding (Lua-defined components go through
    // `LuaTypedColumnView` instead and do not appear in this map).
    const LuaCppColumnAccessor *cppColumnAccessor(IREntity::ComponentId componentId) const {
        auto it = m_cppColumnAccessors.find(componentId);
        if (it == m_cppColumnAccessors.end()) {
            return nullptr;
        }
        return &it->second;
    }

    template <typename T> void registerTypeFromTraits() {
        static_assert(kHasLuaBinding<T>, "Lua binding specialization missing for this type.");
        bindLuaType<T>(*this);
    }

    template <typename... Types> void registerTypesFromTraits() {
        (registerTypeFromTraits<Types>(), ...);
    }

    template <typename Enum>
    void registerEnum(
        const char *name, std::initializer_list<std::pair<std::string_view, Enum>> values
    ) {

        m_lua.new_enum<Enum>(name, values);
    }

    template <typename T, typename... Constructors, typename... KeyValuePairs>
    sol::usertype<T> registerType(const std::string &name, KeyValuePairs... keyValuePairs) {
        IR_LOG_INFO("Registering lua type {}", name);
        IR_ASSERT(sizeof...(Constructors) > 0, "At least one constructor must be specified");

        auto usertype =
            m_lua.new_usertype<T>(name, sol::constructors<Constructors...>(), keyValuePairs...);

        // Components that have a Lua binding (`*_lua.hpp` specializing
        // `kHasLuaBinding<T> = true`) get their Lua-visible name +
        // column-row accessors recorded so dynamic systems can resolve
        // them by name and expose their columns to a Lua tick body.
        if constexpr (kHasLuaBinding<T>) {
            recordComponentLuaName<T>(name);
        }

        return usertype;
    }
    // template <typename T, typename... Args, typename... KeyValuePairs>
    // void registerType(
    //     const std::string& name,
    //     KeyValuePairs... keyValuePairs
    // ) {
    //     IR_LOG_INFO("Registering lua type {}", name);
    //     IR_ASSERT(sizeof...(Args) > 0, "Arguments for type constructor cannot be empty");

    //     m_lua.new_usertype<T>(name,
    //         sol::constructors<T(Args...)>(),
    //         keyValuePairs...
    //     );
    // }

    template <typename... Components> void registerCreateEntityFunction(const char *funcName) {
        if (!m_lua["IREntity"].valid()) {
            m_lua["IREntity"] = m_lua.create_table();
        }
        m_lua["IREntity"][funcName] = [](Components... components) {
            IREntity::EntityId entity = IREntity::createEntity(components...);
            return IRScript::LuaEntity{entity};
        };
    }

    template <typename... UserComponents, typename... TagComponents>
    void registerCreateEntityFunctionWithTags(const char *funcName, TagComponents... tags) {
        if (!m_lua["IREntity"].valid()) {
            m_lua["IREntity"] = m_lua.create_table();
        }
        m_lua["IREntity"][funcName] = [tags...](UserComponents... components) {
            IREntity::EntityId entity = IREntity::createEntity(components..., tags...);
            return IRScript::LuaEntity{entity};
        };
    }

    // Perhaps should take a templated entity
    template <typename... Components> void registerCreateEntityBatchFunction(const char *funcName) {

        // TODO something else here, prob constexpr template
        if (!m_lua["IREntity"].valid()) {
            m_lua["IREntity"] = m_lua.create_table();
        }
        auto wrappedFunction = wrapCreateEntityBatchWithFunctions<Components...>();
        m_lua["IREntity"][funcName] = wrappedFunction;
    }

  private: //----------------------------------------------------------------
    // Lua-name → ComponentId for C++ components that have a Lua
    // binding (populated by `registerType` when `kHasLuaBinding<T>`).
    // Lua-defined components go through `EntityManager`'s
    // `m_pureComponentTypes` directly; this map only covers C++ types.
    std::unordered_map<std::string, IREntity::ComponentId> m_componentByLuaName;
    std::unordered_map<IREntity::ComponentId, std::string> m_componentLuaName;

    // Per-C++-component-id row accessor pair (read + replace) used by
    // `LuaCppColumnView` so a Lua system tick can read or overwrite a
    // typed column row without templating the column view on T.
    // Pointers into this map are handed to view objects on a per-tick
    // basis; std::unordered_map references are stable across rehash,
    // so this is safe.
    std::unordered_map<IREntity::ComponentId, LuaCppColumnAccessor> m_cppColumnAccessors;

    // T-102: SystemName enum value (cast to int) → SystemId returned by
    // `IRSystem::createSystem<NAME>()`. Populated by
    // `registerPrefabSystem<N>()`. The Lua side's `IRSystem.systemId`
    // closure reads through `prefabSystemIds()`; the closure captures
    // the pointer once at bind time so subsequent registrations show up
    // without re-binding.
    std::unordered_map<int, IRSystem::SystemId> m_prefabSystemIds;

    // Declared last so it destructs first: lua_close() runs before any
    // closure-captured map (m_prefabSystemIds etc.) is gone. Mirrors the
    // invariant in world.hpp where m_lua leads so EntityManager outlives
    // Lua — here the direction is flipped because the constraint is
    // "lua_close before captured-map destruction" inside LuaScript itself.
    sol::state m_lua;

    // Wires `IRSystem.registerSystem` and the column-view usertypes
    // into the Lua state. Called from the public `bindLuaDrivenEcs()`
    // entry; the public API stays singular so creations only need one
    // init call regardless of which Lua-driven-ECS PRs land.
    void bindLuaDrivenSystems();

    // Build the read/replace accessor pair for a C++ component type
    // and record it under the type's `ComponentId`. Called from
    // `registerType` when `kHasLuaBinding<T>`.
    template <typename T> void recordComponentLuaName(const std::string &name) {
        auto &em = IREntity::getEntityManager();
        IREntity::ComponentId componentId = em.getComponentType<T>();
        m_componentByLuaName.emplace(name, componentId);
        m_componentLuaName.emplace(componentId, name);

        LuaCppColumnAccessor accessor;
        accessor.reader_ =
            [](sol::state_view lua, IREntity::IComponentData *data, int row) -> sol::object {
            auto *typed = IREntity::castComponentDataPointer<T>(data);
            return sol::make_object(lua, std::ref(typed->dataVector[row]));
        };
        accessor.replacer_ =
            [](sol::state_view, IREntity::IComponentData *data, int row, const sol::object &value) {
                auto *typed = IREntity::castComponentDataPointer<T>(data);
                if (auto opt = value.as<sol::optional<T>>()) {
                    typed->dataVector[row] = *opt;
                }
            };
        m_cppColumnAccessors[componentId] = std::move(accessor);

        // Only if bindLuaDrivenEcs() ran first — that table is the gate.
        if (m_lua["IRComponent"].valid()) {
            sol::table handle = m_lua.create_table();
            handle["typeName"] = name;
            handle["componentId"] = static_cast<lua_Integer>(componentId);
            m_lua["IRComponent"][name] = handle;
        }
    }

    template <typename Component>
    ComponentFunction<Component> wrapLuaFunction(sol::protected_function function) {
        return [function](IREntity::CreateEntityCallbackParams params) {
            sol::protected_function_result result = function(params);

            if (!result.valid()) {
                sol::error err = result;
                IRE_LOG_ERROR("Error in protected_function_result: {}", err.what());
                throw std::runtime_error(err.what());
            }

            Component component = result;
            return component;
        };
    }

    // WRAPPER FUNCTIONS FOR ENTITY BATCHES, SPECIFIC TEMPLATES FOR DIFFERENT NUMBER OF COMPONENTS
    // ----------
    template <typename ComponentA> auto wrapCreateEntityBatchWithFunctions() {
        IR_LOG_INFO("Creating entity batch with 1 component");
        return [this](IRMath::ivec3 partitions, sol::protected_function funcA) {
            std::vector<IREntity::EntityId> entities = createEntityBatchWithFunctions_Ext(
                partitions,
                {},
                wrapLuaFunction<ComponentA>(funcA)
            );
            std::vector<IRScript::LuaEntity> luaEntities;
            luaEntities.resize(entities.size());
            for (int i = 0; i < entities.size(); i++) {
                luaEntities[i].entity = entities[i];
            }
            return luaEntities;
        };
    }

    template <typename ComponentA, typename ComponentB> auto wrapCreateEntityBatchWithFunctions() {
        IR_LOG_INFO("Creating entity batch with 2 components");
        return [this](
                   IRMath::ivec3 partitions,
                   sol::protected_function funcA,
                   sol::protected_function funcB
               ) {
            std::vector<IREntity::EntityId> entities = createEntityBatchWithFunctions_Ext(
                partitions,
                {},
                wrapLuaFunction<ComponentA>(funcA),
                wrapLuaFunction<ComponentB>(funcB)
            );
            std::vector<IRScript::LuaEntity> luaEntities;
            luaEntities.resize(entities.size());
            for (int i = 0; i < entities.size(); i++) {
                luaEntities[i].entity = entities[i];
            }
            return luaEntities;
        };
    }

    template <typename ComponentA, typename ComponentB, typename ComponentC>
    auto wrapCreateEntityBatchWithFunctions() {
        IR_LOG_INFO("Creating entity batch with 3 components");
        return [this](
                   IRMath::ivec3 partitions,
                   sol::protected_function funcA,
                   sol::protected_function funcB,
                   sol::protected_function funcC
               ) {
            std::vector<IREntity::EntityId> entities = createEntityBatchWithFunctions_Ext(
                partitions,
                {},
                wrapLuaFunction<ComponentA>(funcA),
                wrapLuaFunction<ComponentB>(funcB),
                wrapLuaFunction<ComponentC>(funcC)
            );
            std::vector<IRScript::LuaEntity> luaEntities;
            luaEntities.resize(entities.size());
            for (int i = 0; i < entities.size(); i++) {
                luaEntities[i].entity = entities[i];
            }
            return luaEntities;
        };
    }

    template <typename ComponentA, typename ComponentB, typename ComponentC, typename ComponentD>
    auto wrapCreateEntityBatchWithFunctions() {
        IR_LOG_INFO("Creating entity batch with 4 components");
        return [this](
                   IRMath::ivec3 partitions,
                   sol::protected_function funcA,
                   sol::protected_function funcB,
                   sol::protected_function funcC,
                   sol::protected_function funcD
               ) {
            std::vector<IREntity::EntityId> entities = createEntityBatchWithFunctions_Ext(
                partitions,
                {},
                wrapLuaFunction<ComponentA>(funcA),
                wrapLuaFunction<ComponentB>(funcB),
                wrapLuaFunction<ComponentC>(funcC),
                wrapLuaFunction<ComponentD>(funcD)
            );
            std::vector<IRScript::LuaEntity> luaEntities;
            luaEntities.resize(entities.size());
            for (int i = 0; i < entities.size(); i++) {
                luaEntities[i].entity = entities[i];
            }
            return luaEntities;
        };
    }

    template <
        typename ComponentA,
        typename ComponentB,
        typename ComponentC,
        typename ComponentD,
        typename ComponentE>
    auto wrapCreateEntityBatchWithFunctions() {
        IR_LOG_INFO("Creating entity batch with 5 components");
        return [this](
                   IRMath::ivec3 partitions,
                   sol::protected_function funcA,
                   sol::protected_function funcB,
                   sol::protected_function funcC,
                   sol::protected_function funcD,
                   sol::protected_function funcE
               ) {
            std::vector<IREntity::EntityId> entities = createEntityBatchWithFunctions_Ext(
                partitions,
                {},
                wrapLuaFunction<ComponentA>(funcA),
                wrapLuaFunction<ComponentB>(funcB),
                wrapLuaFunction<ComponentC>(funcC),
                wrapLuaFunction<ComponentD>(funcD),
                wrapLuaFunction<ComponentE>(funcE)
            );
            std::vector<IRScript::LuaEntity> luaEntities;
            luaEntities.resize(entities.size());
            for (int i = 0; i < entities.size(); i++) {
                luaEntities[i].entity = entities[i];
            }
            return luaEntities;
        };
    }

    template <
        typename ComponentA,
        typename ComponentB,
        typename ComponentC,
        typename ComponentD,
        typename ComponentE,
        typename ComponentF>
    auto wrapCreateEntityBatchWithFunctions() {
        IR_LOG_INFO("Creating entity batch with 6 components");
        return [this](
                   IRMath::ivec3 partitions,
                   sol::protected_function funcA,
                   sol::protected_function funcB,
                   sol::protected_function funcC,
                   sol::protected_function funcD,
                   sol::protected_function funcE,
                   sol::protected_function funcF
               ) {
            std::vector<IREntity::EntityId> entities = createEntityBatchWithFunctions_Ext(
                partitions,
                {},
                wrapLuaFunction<ComponentA>(funcA),
                wrapLuaFunction<ComponentB>(funcB),
                wrapLuaFunction<ComponentC>(funcC),
                wrapLuaFunction<ComponentD>(funcD),
                wrapLuaFunction<ComponentE>(funcE),
                wrapLuaFunction<ComponentF>(funcF)
            );
            std::vector<IRScript::LuaEntity> luaEntities;
            luaEntities.resize(entities.size());
            for (int i = 0; i < entities.size(); i++) {
                luaEntities[i].entity = entities[i];
            }
            return luaEntities;
        };
    }

    template <
        typename ComponentA,
        typename ComponentB,
        typename ComponentC,
        typename ComponentD,
        typename ComponentE,
        typename ComponentF,
        typename ComponentG>
    auto wrapCreateEntityBatchWithFunctions() {
        IR_LOG_INFO("Creating entity batch with 7 components");
        return [this](
                   IRMath::ivec3 partitions,
                   sol::protected_function funcA,
                   sol::protected_function funcB,
                   sol::protected_function funcC,
                   sol::protected_function funcD,
                   sol::protected_function funcE,
                   sol::protected_function funcF,
                   sol::protected_function funcG
               ) {
            std::vector<IREntity::EntityId> entities = createEntityBatchWithFunctions_Ext(
                partitions,
                {},
                wrapLuaFunction<ComponentA>(funcA),
                wrapLuaFunction<ComponentB>(funcB),
                wrapLuaFunction<ComponentC>(funcC),
                wrapLuaFunction<ComponentD>(funcD),
                wrapLuaFunction<ComponentE>(funcE),
                wrapLuaFunction<ComponentF>(funcF),
                wrapLuaFunction<ComponentG>(funcG)
            );
            std::vector<IRScript::LuaEntity> luaEntities;
            luaEntities.resize(entities.size());
            for (int i = 0; i < entities.size(); i++) {
                luaEntities[i].entity = entities[i];
            }
            return luaEntities;
        };
    }

    template <
        typename ComponentA,
        typename ComponentB,
        typename ComponentC,
        typename ComponentD,
        typename ComponentE,
        typename ComponentF,
        typename ComponentG,
        typename ComponentH>
    auto wrapCreateEntityBatchWithFunctions() {
        IR_LOG_INFO("Creating entity batch with 8 components");
        return [this](
                   IRMath::ivec3 partitions,
                   sol::protected_function funcA,
                   sol::protected_function funcB,
                   sol::protected_function funcC,
                   sol::protected_function funcD,
                   sol::protected_function funcE,
                   sol::protected_function funcF,
                   sol::protected_function funcG,
                   sol::protected_function funcH
               ) {
            std::vector<IREntity::EntityId> entities = createEntityBatchWithFunctions_Ext(
                partitions,
                {},
                wrapLuaFunction<ComponentA>(funcA),
                wrapLuaFunction<ComponentB>(funcB),
                wrapLuaFunction<ComponentC>(funcC),
                wrapLuaFunction<ComponentD>(funcD),
                wrapLuaFunction<ComponentE>(funcE),
                wrapLuaFunction<ComponentF>(funcF),
                wrapLuaFunction<ComponentG>(funcG),
                wrapLuaFunction<ComponentH>(funcH)
            );
            std::vector<IRScript::LuaEntity> luaEntities;
            luaEntities.resize(entities.size());
            for (int i = 0; i < entities.size(); i++) {
                luaEntities[i].entity = entities[i];
            }
            return luaEntities;
        };
    }

    template <
        typename ComponentA,
        typename ComponentB,
        typename ComponentC,
        typename ComponentD,
        typename ComponentE,
        typename ComponentF,
        typename ComponentG,
        typename ComponentH,
        typename ComponentI>
    auto wrapCreateEntityBatchWithFunctions() {
        IR_LOG_INFO("Creating entity batch with 9 components");
        return [this](
                   IRMath::ivec3 partitions,
                   sol::protected_function funcA,
                   sol::protected_function funcB,
                   sol::protected_function funcC,
                   sol::protected_function funcD,
                   sol::protected_function funcE,
                   sol::protected_function funcF,
                   sol::protected_function funcG,
                   sol::protected_function funcH,
                   sol::protected_function funcI
               ) {
            std::vector<IREntity::EntityId> entities = createEntityBatchWithFunctions_Ext(
                partitions,
                {},
                wrapLuaFunction<ComponentA>(funcA),
                wrapLuaFunction<ComponentB>(funcB),
                wrapLuaFunction<ComponentC>(funcC),
                wrapLuaFunction<ComponentD>(funcD),
                wrapLuaFunction<ComponentE>(funcE),
                wrapLuaFunction<ComponentF>(funcF),
                wrapLuaFunction<ComponentG>(funcG),
                wrapLuaFunction<ComponentH>(funcH),
                wrapLuaFunction<ComponentI>(funcI)
            );
            std::vector<IRScript::LuaEntity> luaEntities;
            luaEntities.resize(entities.size());
            for (int i = 0; i < entities.size(); i++) {
                luaEntities[i].entity = entities[i];
            }
            return luaEntities;
        };
    }

    template <
        typename ComponentA,
        typename ComponentB,
        typename ComponentC,
        typename ComponentD,
        typename ComponentE,
        typename ComponentF,
        typename ComponentG,
        typename ComponentH,
        typename ComponentI,
        typename ComponentJ>
    auto wrapCreateEntityBatchWithFunctions() {
        IR_LOG_INFO("Creating entity batch with 10 components");
        return [this](
                   IRMath::ivec3 partitions,
                   sol::protected_function funcA,
                   sol::protected_function funcB,
                   sol::protected_function funcC,
                   sol::protected_function funcD,
                   sol::protected_function funcE,
                   sol::protected_function funcF,
                   sol::protected_function funcG,
                   sol::protected_function funcH,
                   sol::protected_function funcI,
                   sol::protected_function funcJ
               ) {
            std::vector<IREntity::EntityId> entities = createEntityBatchWithFunctions_Ext(
                partitions,
                {},
                wrapLuaFunction<ComponentA>(funcA),
                wrapLuaFunction<ComponentB>(funcB),
                wrapLuaFunction<ComponentC>(funcC),
                wrapLuaFunction<ComponentD>(funcD),
                wrapLuaFunction<ComponentE>(funcE),
                wrapLuaFunction<ComponentF>(funcF),
                wrapLuaFunction<ComponentG>(funcG),
                wrapLuaFunction<ComponentH>(funcH),
                wrapLuaFunction<ComponentI>(funcI),
                wrapLuaFunction<ComponentJ>(funcJ)
            );
            std::vector<IRScript::LuaEntity> luaEntities;
            luaEntities.resize(entities.size());
            for (int i = 0; i < entities.size(); i++) {
                luaEntities[i].entity = entities[i];
            }
            return luaEntities;
        };
    }

    template <
        typename ComponentA,
        typename ComponentB,
        typename ComponentC,
        typename ComponentD,
        typename ComponentE,
        typename ComponentF,
        typename ComponentG,
        typename ComponentH,
        typename ComponentI,
        typename ComponentJ,
        typename ComponentK>
    auto wrapCreateEntityBatchWithFunctions() {
        IR_LOG_INFO("Creating entity batch with 11 components");
        return [this](
                   IRMath::ivec3 partitions,
                   sol::protected_function funcA,
                   sol::protected_function funcB,
                   sol::protected_function funcC,
                   sol::protected_function funcD,
                   sol::protected_function funcE,
                   sol::protected_function funcF,
                   sol::protected_function funcG,
                   sol::protected_function funcH,
                   sol::protected_function funcI,
                   sol::protected_function funcJ,
                   sol::protected_function funcK
               ) {
            std::vector<IREntity::EntityId> entities = createEntityBatchWithFunctions_Ext(
                partitions,
                {},
                wrapLuaFunction<ComponentA>(funcA),
                wrapLuaFunction<ComponentB>(funcB),
                wrapLuaFunction<ComponentC>(funcC),
                wrapLuaFunction<ComponentD>(funcD),
                wrapLuaFunction<ComponentE>(funcE),
                wrapLuaFunction<ComponentF>(funcF),
                wrapLuaFunction<ComponentG>(funcG),
                wrapLuaFunction<ComponentH>(funcH),
                wrapLuaFunction<ComponentI>(funcI),
                wrapLuaFunction<ComponentJ>(funcJ),
                wrapLuaFunction<ComponentK>(funcK)
            );
            std::vector<IRScript::LuaEntity> luaEntities;
            luaEntities.resize(entities.size());
            for (int i = 0; i < entities.size(); i++) {
                luaEntities[i].entity = entities[i];
            }
            return luaEntities;
        };
    }

    template <
        typename ComponentA,
        typename ComponentB,
        typename ComponentC,
        typename ComponentD,
        typename ComponentE,
        typename ComponentF,
        typename ComponentG,
        typename ComponentH,
        typename ComponentI,
        typename ComponentJ,
        typename ComponentK,
        typename ComponentL>
    auto wrapCreateEntityBatchWithFunctions() {
        IR_LOG_INFO("Creating entity batch with 12 components");
        return [this](
                   IRMath::ivec3 partitions,
                   sol::protected_function funcA,
                   sol::protected_function funcB,
                   sol::protected_function funcC,
                   sol::protected_function funcD,
                   sol::protected_function funcE,
                   sol::protected_function funcF,
                   sol::protected_function funcG,
                   sol::protected_function funcH,
                   sol::protected_function funcI,
                   sol::protected_function funcJ,
                   sol::protected_function funcK,
                   sol::protected_function funcL
               ) {
            std::vector<IREntity::EntityId> entities = createEntityBatchWithFunctions_Ext(
                partitions,
                {},
                wrapLuaFunction<ComponentA>(funcA),
                wrapLuaFunction<ComponentB>(funcB),
                wrapLuaFunction<ComponentC>(funcC),
                wrapLuaFunction<ComponentD>(funcD),
                wrapLuaFunction<ComponentE>(funcE),
                wrapLuaFunction<ComponentF>(funcF),
                wrapLuaFunction<ComponentG>(funcG),
                wrapLuaFunction<ComponentH>(funcH),
                wrapLuaFunction<ComponentI>(funcI),
                wrapLuaFunction<ComponentJ>(funcJ),
                wrapLuaFunction<ComponentK>(funcK),
                wrapLuaFunction<ComponentL>(funcL)
            );
            std::vector<IRScript::LuaEntity> luaEntities;
            luaEntities.resize(entities.size());
            for (int i = 0; i < entities.size(); i++) {
                luaEntities[i].entity = entities[i];
            }
            return luaEntities;
        };
    }
};
} // namespace IRScript

#endif /* LUA_SCRIPT_H */
