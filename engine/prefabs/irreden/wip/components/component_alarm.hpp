#ifndef COMPONENT_ALARM_H
#define COMPONENT_ALARM_H

namespace IRComponents {

struct C_Alarm {
    int alarmTime_;

    C_Alarm(int alarmTime)
        : alarmTime_(alarmTime) {}

    C_Alarm()
        : alarmTime_(0) {}
};

} // namespace IRComponents

#endif /* COMPONENT_ALARM_H */
