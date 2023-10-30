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
#include <string>

namespace IRProfile {
    class ProfilerCPU {
    public:
        ~ProfilerCPU();
        static ProfilerCPU& instance();
        void mainThread();
        void profileFunction(unsigned int color);
        void profileBlock(const std::string name, unsigned int color);
    private:
        ProfilerCPU();
    };

} // namespace IRProfile


#endif /* CPU_PROFILER_H */
