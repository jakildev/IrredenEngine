#include "cpu_profiler.hpp"

namespace IRProfiling{
    ProfilerCPU::ProfilerCPU() {
        EASY_PROFILER_ENABLE;
        EASY_MAIN_THREAD;
    }
    ProfilerCPU::~ProfilerCPU() {
        EASY_PROFILER_DISABLE;
        uint32_t res = profiler::dumpBlocksToFile("profiler_dump.prof");
        ENG_LOG_INFO("Dumped profiling blocks, result={}", res);
    }

} // namespace IRProfiling