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

#include <chrono>
#include <irreden/ir_constants.hpp>

namespace IRTime {
    class TimeManager;

    using Clock = std::chrono::high_resolution_clock;
    using TimePoint = std::chrono::_V2::system_clock::time_point;
    using NanoDuration = std::chrono::duration<int64_t, std::nano>;
    using MilliDuration = std::chrono::duration<double, std::milli>;
    using SecondsDuration = std::chrono::duration<double>;
    using NanoSeconds = std::chrono::nanoseconds;
    using MilliSeconds = std::chrono::milliseconds;
    using Seconds = std::chrono::seconds;
    template <intmax_t fps>
    using FramePeriod = std::chrono::duration<double, std::ratio<1,fps>>;
    constexpr FramePeriod<IRConstants::kFPS> kFPSFramePeriod{1};
    constexpr NanoDuration kFPSNanoDuration =
        std::chrono::duration_cast<NanoDuration>(kFPSFramePeriod);

    const unsigned int kProfileHistoryBufferSize = 100;

    enum Events {
        UPDATE,
        RENDER,
        INPUT,
        NUM_EVENTS
    };

    template<Events eventType>
    class EventProfiler;

    extern TimeManager* g_timeManager;
    TimeManager& getTimeManager();

    double deltaTime(Events eventType);
}

#endif /* IR_TIME_H */
