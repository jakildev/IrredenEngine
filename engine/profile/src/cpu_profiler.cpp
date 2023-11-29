/*
 * Project: Irreden Engine
 * File: cpu_profiler.cpp
 * Author: Evin Killian jakildev@gmail.com
 * Created Date: October 2023
 * -----
 * Modified By: <your_name> <Month> <YYYY>
 */

#include <irreden/ir_profile.hpp>

#include <irreden/profile/cpu_profiler.hpp>

namespace IRProfile{

    CPUProfiler& CPUProfiler::instance() {
        static CPUProfiler instance;
        return instance;
    }

    CPUProfiler::CPUProfiler() {
        EASY_PROFILER_ENABLE;
    }

    CPUProfiler::~CPUProfiler() {
        EASY_PROFILER_DISABLE;
        uint32_t res = profiler::dumpBlocksToFile("profiler_dump.prof");
        IRE_LOG_INFO("Dumped profiling blocks, result={}", res);
    }
    // inline void CPUProfiler::mainThread() {
    //     EASY_MAIN_THREAD;
    // }

    // inline void CPUProfiler::profileFunction(unsigned int color) {
    //     EASY_FUNCTION(color);
    // }

    // inline void CPUProfiler::profileBlock(
    //     const std::string name,
    //     unsigned int color
    // )
    // {
    //     EASY_BLOCK(name.c_str(), color);
    // }

} // namespace IRProfile