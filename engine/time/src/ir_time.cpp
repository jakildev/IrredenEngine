#include <irreden/ir_time.hpp>
#include <irreden/time/time_manager.hpp>

namespace IRTime {

    TimeManager* g_timeManager = nullptr;
    TimeManager& getTimeManager() {
        IR_ASSERT(
            g_timeManager != nullptr,
            "TimeManager not initialized"
        );
        return *g_timeManager;
    }

    double deltaTime(Events eventType) {
        if(eventType == Events::UPDATE) {
            return getTimeManager().deltaTime<UPDATE>();
        }

        if(eventType == Events::RENDER) {
            return getTimeManager().deltaTime<RENDER>();
        }

        return 0.0;
    }

} // namespace IRTime

