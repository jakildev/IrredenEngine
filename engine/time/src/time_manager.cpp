#include <irreden/time/time_manager.hpp>
#include <irreden/ir_time.hpp>

namespace IRTime {

// I forgot about this, maybe I can apply this elsewhere
// like for systems and such...
TimeManager::TimeManager()
    : m_profilerUpdate{}
    , m_profilerRender{}
    , m_start{}
    , m_mainLoopPrevious{}
    , m_mainLoopElapsed{0} {
    g_timeManager = this;
    IRE_LOG_INFO("TimeManager initalized");
}

template <> void TimeManager::beginEvent<UPDATE>() {
    IRE_LOG_DEBUG("Begin update world.");
    m_profilerUpdate.beginEvent();
}

template <> void TimeManager::beginEvent<RENDER>() {
    IRE_LOG_DEBUG("Begin render world.");
    m_profilerRender.beginEvent();
}

template <> void TimeManager::endEvent<UPDATE>() {
    IRE_LOG_DEBUG("End update world.");
    m_profilerUpdate.endEvent();
}

template <> void TimeManager::endEvent<RENDER>() {
    IRE_LOG_DEBUG("End render world.");
    m_profilerRender.endEvent();
}

template <> double TimeManager::deltaTime<UPDATE>() {
    return m_profilerUpdate.deltaTimeFixed();
}

template <> double TimeManager::deltaTime<RENDER>() {
    return m_profilerRender.deltaTime();
}

} // namespace IRTime
