#ifndef COMPONENT_ALARM_H
#define COMPONENT_ALARM_H

#include <irreden/ir_math.hpp>

using namespace IRMath;

namespace IRComponents {

struct C_Alarm {
    int alarmTime_;

    C_Alarm(int alarmTime) : alarmTime_(alarmTime) {}

    // Default
    C_Alarm() : {}
};

} // namespace IRComponents

#endif /* COMPONENT_ALARM_H */
