#include <irreden/profile/cpu_profiler.hpp>
#include <irreden/ir_profile.hpp>

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
        // IRProfile::engLogInfo("Dumped profiling blocks, result={}", res);
    }

    void CPUProfiler::mainThread() {
        EASY_MAIN_THREAD;
    }

    void CPUProfiler::profileFunction(unsigned int color) {
        EASY_FUNCTION(color);
    }

    void CPUProfiler::profileBlock(
        const std::string name,
        unsigned int color
    )
    {
        EASY_BLOCK(name.c_str(), color);
    }

} // namespace IRProfile