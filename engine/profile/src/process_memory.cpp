#include <irreden/profile/process_memory.hpp>

#if defined(_WIN32)
#define PSAPI_VERSION 2
#include <windows.h>
#include <psapi.h>
#elif defined(__APPLE__)
#include <mach/mach.h>
#include <sys/resource.h>
#else
#include <cstdlib>
#include <fstream>
#include <string>
#endif

#include <algorithm>

namespace IRProfile {

ProcessMemoryStats processMemoryStats() {
    ProcessMemoryStats stats;
#if defined(_WIN32)
    PROCESS_MEMORY_COUNTERS counters{};
    counters.cb = sizeof(counters);
    if (GetProcessMemoryInfo(GetCurrentProcess(), &counters, sizeof(counters)) != 0) {
        stats.residentBytes_ = counters.WorkingSetSize;
        stats.peakResidentBytes_ = counters.PeakWorkingSetSize;
    }
#elif defined(__APPLE__)
    mach_task_basic_info_data_t basic{};
    mach_msg_type_number_t count = MACH_TASK_BASIC_INFO_COUNT;
    if (task_info(
            mach_task_self(),
            MACH_TASK_BASIC_INFO,
            reinterpret_cast<task_info_t>(&basic),
            &count
        ) == KERN_SUCCESS) {
        stats.residentBytes_ = basic.resident_size;
    }
    rusage usage{};
    if (getrusage(RUSAGE_SELF, &usage) == 0) {
        stats.peakResidentBytes_ = static_cast<std::size_t>(usage.ru_maxrss);
    }
#else
    // VmRSS and VmHWM come from one pass over the same counters, so they form
    // a consistent pair; statm plus getrusage read two accounting paths that
    // can disagree by several pages.
    std::ifstream status("/proc/self/status");
    std::string line;
    while (std::getline(status, line)) {
        std::size_t *field = nullptr;
        if (line.rfind("VmRSS:", 0) == 0) {
            field = &stats.residentBytes_;
        } else if (line.rfind("VmHWM:", 0) == 0) {
            field = &stats.peakResidentBytes_;
        }
        if (field != nullptr) {
            *field = std::strtoull(line.c_str() + line.find(':') + 1, nullptr, 10) * 1024u;
        }
    }
#endif
    // A high-water mark below the current sample is never meaningful; the
    // platform pairs come from separately updated counters.
    stats.peakResidentBytes_ = std::max(stats.peakResidentBytes_, stats.residentBytes_);
    return stats;
}

} // namespace IRProfile
