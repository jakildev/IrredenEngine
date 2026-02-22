#include <irreden/ir_profile.hpp>

#include <irreden/profile/cpu_profiler.hpp>

namespace IRProfile {

CPUProfiler &CPUProfiler::instance() {
    static CPUProfiler instance;
    return instance;
}

CPUProfiler::CPUProfiler() {
    EASY_PROFILER_ENABLE;
}

CPUProfiler::~CPUProfiler() {
    EASY_PROFILER_DISABLE;
    if (!m_enabled) {
        return;
    }
    uint32_t res = profiler::dumpBlocksToFile("profiler_dump.prof");
    IRE_LOG_INFO("Dumped profiling blocks, result={}", res);
}

void CPUProfiler::setEnabled(bool enabled) {
    m_enabled = enabled;
    if (m_enabled) {
        EASY_PROFILER_ENABLE;
    } else {
        EASY_PROFILER_DISABLE;
    }
}

bool CPUProfiler::isEnabled() const {
    return m_enabled;
}

void CPUProfiler::mainThread() {
    if (!m_enabled) {
        return;
    }
    EASY_MAIN_THREAD;
}

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