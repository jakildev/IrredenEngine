#ifndef IR_TIME_H
#define IR_TIME_H

#include <irreden/time/ir_time_types.hpp>

namespace IRTime {

extern TimeManager *g_timeManager;
TimeManager &getTimeManager();

double deltaTime(Events eventType);
bool shouldUpdate();

double renderFps();
double renderFrameTimeMs();
double updateFps();
unsigned int droppedFrames();
void resetDroppedFrames();
} // namespace IRTime

#endif /* IR_TIME_H */
