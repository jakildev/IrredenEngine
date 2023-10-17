/*
 * Project: Irreden Engine
 * File: \irreden-engine\engine\src\profiling\cpu_profiler.hpp
 * Author: Evin Killian jakildev@gmail.com
 * Created Date: October 2023
 * -----
 * Modified By: <your_name> <Month> <YYYY>
 */

#ifndef CPU_PROFILER_H
#define CPU_PROFILER_H

#include <easy/profiler.h>

#define IR_PROFILER_COLOR_UPDATE     0xff0000ff
#define IR_PROFILER_COLOR_RENDER     0xffff0000
#define IR_PROFILER_COLOR_AUDIO      0xff00ffff
#define IR_PROFILER_COLOR_ENTITY_OPS 0xfff0ff00
#define IR_PROFILER_COLOR_SYSTEMS    0xff0f0f00
#define IR_PROFILER_COLOR_COMMANDS    0x88Ff0f00
#define IR_PROFILER_COLOR_BEGIN_EXECUTE      0xff00ff00
#define IR_PROFILER_COLOR_END_EXECUTE        0xff00ff00
#define IR_PROFILER_COLOR_INPUT      0xff4444ff

#include "../profiling/logger_spd.hpp" // ir_profiling

namespace IRProfiling{
    class ProfilerCPU {
    public:
        ProfilerCPU() {
            EASY_PROFILER_ENABLE;
            EASY_MAIN_THREAD;
        }
        ~ProfilerCPU() {
            EASY_PROFILER_DISABLE;
            uint32_t res = profiler::dumpBlocksToFile("profiler_dump.prof");
            ENG_LOG_INFO("Dumped profiling blocks, result={}", res);
        }
    };

} // namespace IRProfiling


#endif /* CPU_PROFILER_H */
