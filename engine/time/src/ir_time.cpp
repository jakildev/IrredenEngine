#include <irreden/ir_time.hpp>
#include <irreden/time/time_manager.hpp>

namespace IRTime {

TimeManager *g_timeManager = nullptr;
TimeManager &getTimeManager() {
    IR_ASSERT(g_timeManager != nullptr, "TimeManager not initialized");
    return *g_timeManager;
}

double deltaTime(Events eventType) {
    if (eventType == Events::UPDATE) {
        return getTimeManager().deltaTime<UPDATE>();
    }

    if (eventType == Events::RENDER) {
        return getTimeManager().deltaTime<RENDER>();
    }
    return 0.0;
}

bool shouldUpdate() {
    return getTimeManager().shouldUpdate();
}

double renderFps() {
    return getTimeManager().fps<RENDER>();
}

double renderFrameTimeMs() {
    return getTimeManager().frameTimeMs<RENDER>();
}

double updateFps() {
    return getTimeManager().fps<UPDATE>();
}

unsigned int droppedFrames() {
    return getTimeManager().droppedFrames();
}

void resetDroppedFrames() {
    getTimeManager().resetDroppedFrames();
}

} // namespace IRTime
