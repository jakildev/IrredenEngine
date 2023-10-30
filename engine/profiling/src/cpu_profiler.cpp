#include <irreden/profile/cpu_profiler.hpp>
#include <irreden/ir_profiling.hpp>

namespace IRProfile{

    ProfilerCPU& ProfilerCPU::instance() {
        static ProfilerCPU instance;
        return instance;
    }

    ProfilerCPU::ProfilerCPU() {
        EASY_PROFILER_ENABLE;
    }

    ProfilerCPU::~ProfilerCPU() {
        EASY_PROFILER_DISABLE;
        uint32_t res = profiler::dumpBlocksToFile("profiler_dump.prof");
        // IRProfile::engLogInfo("Dumped profiling blocks, result={}", res);
    }

    void ProfilerCPU::mainThread() {
        EASY_MAIN_THREAD;
    }

    void ProfilerCPU::profileFunction(unsigned int color) {
        IRProfile::profileFunction(color);
    }

    void ProfilerCPU::profileBlock(
        const std::string name,
        unsigned int color
    )
    {
        EASY_BLOCK(name.c_str(), color);
    }

} // namespace IRProfile