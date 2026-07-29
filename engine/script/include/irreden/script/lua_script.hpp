#ifndef LUA_SCRIPT_H
#define LUA_SCRIPT_H

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

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
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

    // Bind the input + command surface — IRCommand.{bindPrefab,
    // createCommand, fire, fireByName, CommandName} and IRInput.{InputType,
    // ButtonStatus, Key, Modifier, GamepadButton, GamepadAxis}. Idempotent
    // (each detail bind helper guards on its own bound-table key). Required
    // for any creation that wants to declare commands or bind input from
    // Lua. See docs/design/lua-input-commands.md and engine/script/CLAUDE.md
    // for the surface contract.
    void bindLuaCommands();

    // Bind the synthetic-input surface — IRInput.KeyMouseButtons,
    // IRInput.ButtonStatuses, and IRInput.{beginSyntheticInput,
    // isSyntheticInputActive, injectButton, injectMouseMove, injectScroll}.
    // Idempotent. Extends the IRInput table without displacing the short-name
    // tables populated by bindLuaCommands(). Used by behavior-smoke tests and
    // headless harnesses. See engine/input/CLAUDE.md "Synthetic input".
    void bindLuaInput();

    // Set the creation-default mode used by
    // `IRSystem.registerSystem({...})` when the call has no explicit
    // `mode = "..."` field. Mirrors the build-time
    // `IR_LUA_ECS_DEFAULT_MODE` CMake cache var; creations using the
    // codegen pipeline call this with
    // `IRScript::CodegenRegistry::kDefaultEcsMode` after
    // `registerCodegenComponents()` so runtime dispatch matches
    // build-time dispatch. Default is `EVAL` so creations that don't
    // touch the codegen pipeline keep working without ceremony. Can be
    // changed before any `IRSystem.registerSystem` call fires; later
    // calls observe the new value.
    void setEcsDefaultMode(EcsMode mode) {
        m_ecsDefaultMode = mode;
    }
    EcsMode ecsDefaultMode() const {
        return m_ecsDefaultMode;
    }

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
    // duplicate (and now trips an IR_ASSERT via the SystemName registry);
    // this helper records the existing id without recreating.
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

    // #2446: default-construct a C++-typed component, apply the optional
    // Lua overrides table field-by-field, and attach it via the templated
    // `IREntity::setComponent<T>`. The entity core deliberately refuses to
    // default-row a C++-typed component through `addComponentDynamic` (some
    // have deleted default ctors), so this is how a codegen'd component
    // becomes reachable from `IREntity.addLuaComponent` / `deferredCreate`.
    using ComponentAttachFn =
        std::function<void(IREntity::EntityId, const sol::optional<sol::table> &)>;

    // Record the attach factory for `componentId`. Called once per
    // codegen'd component from the emitted `registerCodegenComponents()`;
    // a hand-written `*_lua.hpp` component may opt in the same way.
    // Per-`LuaScript` (World lifetime) because ComponentIds are per-World
    // runtime allocations — a process-static registry would key stale ids
    // across World re-creation.
    void registerComponentAttachFactory(IREntity::ComponentId componentId, ComponentAttachFn fn) {
        m_componentAttachFactories[componentId] = std::move(fn);
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
    // ComponentId → Lua-visible name, for both C++-bound and Lua-typed
    // components. Diagnostics only — the attach / accessor error paths name
    // the offending component instead of printing a bare numeric id. Kept
    // separate from `m_componentByLuaName` (C++ types only), which the
    // coexistence carve-out in `IRComponent.register` keys off.
    std::unordered_map<IREntity::ComponentId, std::string> m_componentLuaName;

    // ComponentIds registered through `IRComponent.register` — i.e.
    // backed by an `IComponentDataLuaTyped` impl. The `IREntity.*Lua*`
    // accessors `static_cast` `IComponentData*` to that type, which is UB
    // for any other impl, so every such cast is gated on membership here.
    // Excludes the coexistence carve-out's early-return (that path returns
    // an existing C++-typed handle and registers no Lua-typed impl).
    std::unordered_set<IREntity::ComponentId> m_luaTypedComponentIds;

    // ComponentId → attach factory for C++-typed components,
    // populated by the codegen-emitted `registerCodegenComponents()`.
    std::unordered_map<IREntity::ComponentId, ComponentAttachFn> m_componentAttachFactories;

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

    // T-103: per-Lua-system shared sol::protected_function reference. The
    // dynamic-system body lambda captures the shared_ptr; replacing the
    // pointed-to function via `*it->second = newFn` rebinds every future
    // invocation of the same SystemId — no re-create, no archetype change,
    // no entity migration. Only Lua-defined systems (registered via
    // `IRSystem.registerSystem`) have an entry; C++ systems and prefab
    // systems are absent, so `IRSystem.replaceSystemBody` rejects their
    // ids with a Lua error rather than silently no-oping.
    std::unordered_map<IRSystem::SystemId, std::shared_ptr<sol::protected_function>>
        m_luaSystemTicks;

    // Creation-default ECS mode (CODEGEN vs EVAL). Read by
    // `IRSystem.registerSystem({...})` when the call has no explicit
    // `mode` field. EVAL by default so creations that don't use the
    // codegen pipeline keep working without ceremony. Codegen-using
    // creations call
    // `setEcsDefaultMode(IRScript::CodegenRegistry::kDefaultEcsMode)`
    // after `registerCodegenComponents()` so the runtime mirrors the
    // build-time default driven by `IR_LUA_ECS_DEFAULT_MODE`.
    EcsMode m_ecsDefaultMode = EcsMode::EVAL;

    // T-223: per-system one-shot dedupe of the
    // "PARALLEL_FOR requested under EVAL — forced to MAIN_THREAD"
    // warning. Specs that re-register on hot-reload would otherwise
    // log every call; one log per system name is enough to surface
    // the misuse.
    std::unordered_set<std::string> m_warnedParallelForEvalSystems;

    // Tracked so a second registration raises rather than silently shadowing the prior table.
    std::unordered_set<std::string> m_luaEnumNames;

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

    // Attach `componentId` to `entity`, routing on how the component
    // is stored — attach factory (C++-typed / codegen'd) first, then the
    // `addComponentDynamic` + `writeRowFromTable` path (Lua-typed). Raises a
    // `sol::error` naming the supported paths when neither applies, so the
    // engine-side `appendDefaultRow` assertion is unreachable from Lua.
    // Shared by `IREntity.addLuaComponent` and `deferredCreate`'s flush.
    void attachComponentFromLua(
        IREntity::EntityId entity,
        IREntity::ComponentId componentId,
        const sol::optional<sol::table> &overrides
    );

    // True when `componentId` can be attached from Lua by id — i.e.
    // `attachComponentFromLua` will not raise. `deferredCreate` checks this
    // at marshal time so an ineligible entry raises at the Lua call site
    // rather than inside the flush drain, where there is no Lua context.
    bool isLuaAttachable(IREntity::ComponentId componentId) const {
        return m_componentAttachFactories.find(componentId) != m_componentAttachFactories.end() ||
               m_luaTypedComponentIds.find(componentId) != m_luaTypedComponentIds.end();
    }

    // Raises unless `componentId` is Lua-typed. The `IREntity.*Lua*`
    // read/write accessors cast to `IComponentDataLuaTyped`; a C++-typed
    // component reaching that cast is undefined behavior.
    void requireLuaTypedComponent(IREntity::ComponentId componentId, const char *accessor) const;

    // Lua-visible name recorded for `componentId`, or `component id <N>`
    // when none was recorded. Diagnostics only.
    std::string componentDisplayName(IREntity::ComponentId componentId) const;

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
