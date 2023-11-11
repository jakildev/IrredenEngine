/*
 * Project: Irreden Engine
 * File: ir_time.hpp
 * Author: Evin Killian jakildev@gmail.com
 * Created Date: October 2023
 * -----
 * Modified By: <your_name> <Month> <YYYY>
 */

#ifndef IR_TIME_H
#define IR_TIME_H

#include <irreden/time/ir_time_types.hpp>

namespace IRTime {

    extern TimeManager* g_timeManager;
    TimeManager& getTimeManager();

    double deltaTime(Events eventType);
    bool shouldUpdate();
}

#endif /* IR_TIME_H */
