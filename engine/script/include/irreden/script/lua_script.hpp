/*
 * Project: Irreden Engine
 * File: lua_script.hpp
 * Author: Evin Killian jakildev@gmail.com
 * Created Date: October 2023
 * -----
 * Modified By: <your_name> <Month> <YYYY>
 */

#ifndef LUA_SCRIPT_H
#define LUA_SCRIPT_H

// #include <lua54/lua.hpp>

#define SOL_ALL_SAFETIES_ON 1
#include <sol/sol.hpp>

#include <irreden/ir_math.hpp>
#include <irreden/ir_entity.hpp>

#include <irreden/script/ir_script_types.hpp>

namespace IRScript {

    class LuaScript {
    public:
        LuaScript();
        LuaScript(const char* filename);

        ~LuaScript();

        inline sol::state& lua() {
            return m_lua;
        }

        sol::table getTable(const char* name);
        void scriptFile(const char* filename);

        void bindCreateEntityBatchFunction();

    template <typename Enum>
    void registerEnum(
        const char* name,
        std::initializer_list<std::pair<std::string_view, Enum>> values
    ) {

        m_lua.new_enum<Enum>(
            name,
            values
        );
    }

    template <typename T, typename... Constructors, typename... KeyValuePairs>
    sol::usertype<T> registerType(
        const std::string& name,
        KeyValuePairs... keyValuePairs
    ) {
        IR_LOG_INFO("Registering lua type {}", name);
        IR_ASSERT(sizeof...(Constructors) > 0, "At least one constructor must be specified");

        return m_lua.new_usertype<T>(name,
            sol::constructors<Constructors...>(),
            keyValuePairs...
        );
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


    // Perhaps should take a templated entity
    template <typename... Components>
    void registerCreateEntityBatchFunction(const char* funcName) {

        // TODO something else here, prob constexpr template
        if (!m_lua["IREntity"].valid()) {
            m_lua["IREntity"] = m_lua.create_table();
        }
        auto wrappedFunction = wrapCreateEntityBatchWithFunctions<Components...>();
        m_lua["IREntity"][funcName] = &wrappedFunction;
    }

    private: //----------------------------------------------------------------
        sol::state m_lua;

    template <typename Component>
    ComponentFunction<Component> wrapLuaFunction(sol::protected_function function) {
        return [function](IREntity::CreateEntityCallbackParams params) {
            sol::protected_function_result result = function(params);

            IR_ASSERT(
                result.valid(),
                "Error in protected_function_result: {}",
                sol::error{result}.what()
            );

            Component component = result;
            return component;
        };
    }

    // WRAPPER FUNCTIONS FOR ENTITY BATCHES, SPECIFIC TEMPLATES FOR DIFFERENT NUMBER OF COMPONENTS ----------
    template<typename ComponentA>
    auto wrapCreateEntityBatchWithFunctions() {
        IR_LOG_INFO("Creating entity batch with 1 component");
        return [this](IRMath::ivec3 partitions, sol::protected_function funcA) {
           std::vector<IREntity::EntityId> entities = createEntityBatchWithFunctions_Ext(
                partitions,
                {},
                wrapLuaFunction<ComponentA>(funcA)
            );
            std::vector<IRScript::LuaEntity> luaEntities;
            luaEntities.resize(entities.size());
            for (int i = 0; i < entities.size(); i++)
            {
                luaEntities[i].entity = entities[i];
            }
            return luaEntities;
        };
    }

    template<typename ComponentA, typename ComponentB>
    auto wrapCreateEntityBatchWithFunctions() {
        IR_LOG_INFO("Creating entity batch with 2 components");
        return [this](IRMath::ivec3 partitions, sol::protected_function funcA, sol::protected_function funcB) {
            std::vector<IREntity::EntityId> entities = createEntityBatchWithFunctions_Ext(
                partitions,
                {},
                wrapLuaFunction<ComponentA>(funcA),
                wrapLuaFunction<ComponentB>(funcB)
            );
            std::vector<IRScript::LuaEntity> luaEntities;
            luaEntities.resize(entities.size());
            for (int i = 0; i < entities.size(); i++)
            {
                luaEntities[i].entity = entities[i];
            }
            return luaEntities;
        };
    }

    template<typename ComponentA, typename ComponentB, typename ComponentC>
    auto wrapCreateEntityBatchWithFunctions() {
        IR_LOG_INFO("Creating entity batch with 3 components");
        return [this](IRMath::ivec3 partitions, sol::protected_function funcA, sol::protected_function funcB, sol::protected_function funcC) {
            std::vector<IREntity::EntityId> entities = createEntityBatchWithFunctions_Ext(
                partitions,
                {},
                wrapLuaFunction<ComponentA>(funcA),
                wrapLuaFunction<ComponentB>(funcB),
                wrapLuaFunction<ComponentC>(funcC)
            );
            std::vector<IRScript::LuaEntity> luaEntities;
            luaEntities.resize(entities.size());
            for (int i = 0; i < entities.size(); i++)
            {
                luaEntities[i].entity = entities[i];
            }
            return luaEntities;
        };
    }

    template<typename ComponentA, typename ComponentB, typename ComponentC, typename ComponentD>
    auto wrapCreateEntityBatchWithFunctions() {
        IR_LOG_INFO("Creating entity batch with 4 components");
        return [this](IRMath::ivec3 partitions, sol::protected_function funcA, sol::protected_function funcB, sol::protected_function funcC, sol::protected_function funcD) {
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
            for (int i = 0; i < entities.size(); i++)
            {
                luaEntities[i].entity = entities[i];
            }
            return luaEntities;
        };
    }

    template<typename ComponentA, typename ComponentB, typename ComponentC, typename ComponentD, typename ComponentE>
    auto wrapCreateEntityBatchWithFunctions() {
        IR_LOG_INFO("Creating entity batch with 5 components");
        return [this](IRMath::ivec3 partitions, sol::protected_function funcA, sol::protected_function funcB, sol::protected_function funcC, sol::protected_function funcD, sol::protected_function funcE) {
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
            for (int i = 0; i < entities.size(); i++)
            {
                luaEntities[i].entity = entities[i];
            }
            return luaEntities;
        };
    }

    template<typename ComponentA, typename ComponentB, typename ComponentC, typename ComponentD, typename ComponentE, typename ComponentF>
    auto wrapCreateEntityBatchWithFunctions() {
        IR_LOG_INFO("Creating entity batch with 6 components");
        return [this](IRMath::ivec3 partitions, sol::protected_function funcA, sol::protected_function funcB, sol::protected_function funcC, sol::protected_function funcD, sol::protected_function funcE, sol::protected_function funcF) {
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
            for (int i = 0; i < entities.size(); i++)
            {
                luaEntities[i].entity = entities[i];
            }
            return luaEntities;
        };
    }

    template<typename ComponentA, typename ComponentB, typename ComponentC, typename ComponentD, typename ComponentE, typename ComponentF, typename ComponentG>
    auto wrapCreateEntityBatchWithFunctions() {
        IR_LOG_INFO("Creating entity batch with 7 components");
        return [this](IRMath::ivec3 partitions, sol::protected_function funcA, sol::protected_function funcB, sol::protected_function funcC, sol::protected_function funcD, sol::protected_function funcE, sol::protected_function funcF, sol::protected_function funcG) {
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
            for (int i = 0; i < entities.size(); i++)
            {
                luaEntities[i].entity = entities[i];
            }
            return luaEntities;
        };
    }

    template<typename ComponentA, typename ComponentB, typename ComponentC, typename ComponentD, typename ComponentE, typename ComponentF, typename ComponentG, typename ComponentH>
    auto wrapCreateEntityBatchWithFunctions() {
        IR_LOG_INFO("Creating entity batch with 8 components");
        return [this](IRMath::ivec3 partitions, sol::protected_function funcA, sol::protected_function funcB, sol::protected_function funcC, sol::protected_function funcD, sol::protected_function funcE, sol::protected_function funcF, sol::protected_function funcG, sol::protected_function funcH) {
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
            for (int i = 0; i < entities.size(); i++)
            {
                luaEntities[i].entity = entities[i];
            }
            return luaEntities;
        };
    }

    template<typename ComponentA, typename ComponentB, typename ComponentC, typename ComponentD, typename ComponentE, typename ComponentF, typename ComponentG, typename ComponentH, typename ComponentI>
    auto wrapCreateEntityBatchWithFunctions() {
        IR_LOG_INFO("Creating entity batch with 9 components");
        return [this](IRMath::ivec3 partitions, sol::protected_function funcA, sol::protected_function funcB, sol::protected_function funcC, sol::protected_function funcD, sol::protected_function funcE, sol::protected_function funcF, sol::protected_function funcG, sol::protected_function funcH, sol::protected_function funcI) {
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
            for (int i = 0; i < entities.size(); i++)
            {
                luaEntities[i].entity = entities[i];
            }
            return luaEntities;
        };
    }

    template<typename ComponentA, typename ComponentB, typename ComponentC, typename ComponentD, typename ComponentE, typename ComponentF, typename ComponentG, typename ComponentH, typename ComponentI, typename ComponentJ>
    auto wrapCreateEntityBatchWithFunctions() {
        IR_LOG_INFO("Creating entity batch with 10 components");
        return [this](IRMath::ivec3 partitions, sol::protected_function funcA, sol::protected_function funcB, sol::protected_function funcC, sol::protected_function funcD, sol::protected_function funcE, sol::protected_function funcF, sol::protected_function funcG, sol::protected_function funcH, sol::protected_function funcI, sol::protected_function funcJ) {
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
            for (int i = 0; i < entities.size(); i++)
            {
                luaEntities[i].entity = entities[i];
            }
            return luaEntities;
        };
    }
    };
} // namespace IRScript

#endif /* LUA_SCRIPT_H */
