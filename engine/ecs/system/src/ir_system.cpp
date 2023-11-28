/*
 * Project: Irreden Engine
 * File: ir_system.cpp
 * Author: Evin Killian jakildev@gmail.com
 * Created Date: November 2023
 * -----
 * Modified By: <your_name> <Month> <YYYY>
 */

#include <irreden/ir_system.hpp>

namespace IRECS {

    SystemManager* g_systemManager = nullptr;

    SystemManager& getSystemManager() {
        return *g_systemManager;
    }

    void executePipeline(SystemTypes systemType) {
        getSystemManager().executePipeline(systemType);
    }
} // namespace IRECS