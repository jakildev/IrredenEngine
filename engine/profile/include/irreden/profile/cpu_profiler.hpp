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
        void mainThread();
        void profileFunction(unsigned int color);
        void profileBlock(const std::string name, unsigned int color);
    private:
        CPUProfiler();
    };

} // namespace IRProfile


#endif /* CPU_PROFILER_H */
