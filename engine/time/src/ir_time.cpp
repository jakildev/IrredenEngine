#include <irreden/ir_time.hpp>
#include <irreden/time/time_manager.hpp>

namespace IRTime {

    TimeManager& getTimeManager() {
        return TimeManager::instance();
    }

    double deltaTime(Events eventType) {
        if(eventType == Events::UPDATE) {
            return TimeManager::instance().deltaTime<UPDATE>();
        }

        if(eventType == Events::RENDER) {
            return TimeManager::instance().deltaTime<RENDER>();
        }

        return 0.0;
    }

} // namespace IRTime

