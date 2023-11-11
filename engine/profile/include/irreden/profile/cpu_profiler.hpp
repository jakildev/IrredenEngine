/*
 * Project: Irreden Engine
 * File: cpu_profiler.hpp
 * Author: Evin Killian jakildev@gmail.com
 * Created Date: October 2023
 * -----
 * Modified By: <your_name> <Month> <YYYY>
 */

#ifndef CPU_PROFILER_H
#define CPU_PROFILER_H

#include <easy/profiler.h>

#include <string>

namespace IRProfile {
    class CPUProfiler {
    public:
        ~CPUProfiler();
        static CPUProfiler& instance();
        inline void mainThread() {
            EASY_MAIN_THREAD;
        }
        // inline void profileFunction(unsigned int color) {
        //     EASY_FUNCTION(color);
        // }
        // inline void profileBlock(
        //     const std::string name,
        //     unsigned int color
        // )
        // {
        //     EASY_BLOCK(name.c_str(), color);
        // }
    private:
        CPUProfiler();
    };

} // namespace IRProfile


#endif /* CPU_PROFILER_H */
