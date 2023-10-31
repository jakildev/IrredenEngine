/*
 * Project: Irreden Engine
 * File: ir_profile.cpp
 * Author: Evin Killian jakildev@gmail.com
 * Created Date: October 2023
 * -----
 * Modified By: <your_name> <Month> <YYYY>
 */

#include <irreden/ir_profile.hpp>
#include <irreden/profile/logger_spd.hpp>
#include <irreden/profile/cpu_profiler.hpp>

namespace IRProfile {

    // CPU profiling API
    void profileMainThread() {
        CPUProfiler::instance().mainThread();
    }
    void profileFunction(unsigned int color) {
        CPUProfiler::instance().profileFunction(color);
    }
    void profileBlock(const std::string name, unsigned int color) {
        CPUProfiler::instance().profileBlock(name, color);
    }
} // namespace IRProfile