/*
 * Project: Irreden Engine
 * File: \irreden-engine\engine\src\time\time_manager.cpp
 * Author: Evin Killian jakildev@gmail.com
 * Created Date: October 2023
 * -----
 * Modified By: <your_name> <Month> <YYYY>
 */

#include <irreden/time/time_manager.hpp>

namespace IRTime {

    // I forgot about this, maybe I can apply this elsewhere
    // like for systems and such...

    template <>
    void TimeManager::beginEvent<UPDATE>() {
        IRProfile::engLogDebug("Begin update world.");
        m_profilerUpdate.beginEvent();
    }

    template <>
    void TimeManager::beginEvent<RENDER>() {
        IRProfile::engLogDebug("Begin render world.");
        m_profilerRender.beginEvent();
    }

    template <>
    void TimeManager::endEvent<UPDATE>() {
        IRProfile::engLogDebug("End update world.");
        m_profilerUpdate.endEvent();
    }

    template <>
    void TimeManager::endEvent<RENDER>() {
        IRProfile::engLogDebug("End render world.");
        m_profilerRender.endEvent();
    }

    template <>
    double TimeManager::deltaTime<UPDATE>() {
        return m_profilerUpdate.deltaTime();
    }

    template <>
    double TimeManager::deltaTime<RENDER>() {
        return m_profilerRender.deltaTime();
    }

} // namespace IRTime
