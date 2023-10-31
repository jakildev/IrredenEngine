/*
 * Project: Irreden Engine
 * File: ir_ecs.cpp
 * Author: Evin Killian jakildev@gmail.com
 * Created Date: October 2023
 * -----
 * Modified By: <your_name> <Month> <YYYY>
 */

#include <irreden/ir_ecs.hpp>
#include <irreden/ecs/entity_manager.hpp>
#include <irreden/ecs/system_manager.hpp>

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

    EntityManager* g_entityManager = nullptr;
    EntityManager& getEntityManager() {
        IR_ENG_ASSERT(
            g_entityManager != nullptr,
            "EntityManager not initialized"
        );
        return *g_entityManager;
    }

    SystemManager* g_systemManager = nullptr;
    SystemManager& getSystemManager() {
        IR_ENG_ASSERT(
            g_systemManager != nullptr,
            "SystemManager not initialized"
        );
        return *g_systemManager;
    }
    // template <IRSystemName systemName>
    // IRSystem<systemName>& getSystem() {
    //     return SystemManager::instance().get<systemName>();
    // }

    // template <
    //     PrefabTypes type,
    //     typename... Args
    // >
    // EntityId createPrefab(Args&&... args)
    // {
    //     return Prefab<type>::create(
    //         args...
    //     );
    // }


} // namespace IRECS