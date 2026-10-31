#include <irreden/profile/process_memory.hpp>

#if defined(_WIN32)
#define PSAPI_VERSION 2
#include <windows.h>
#include <psapi.h>
#elif defined(__APPLE__)
#include <mach/mach.h>
#include <sys/resource.h>
#else
#include <sys/resource.h>
#include <unistd.h>

#include <fstream>
#endif

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
    std::ifstream statm("/proc/self/statm");
    std::size_t virtualPages = 0;
    std::size_t residentPages = 0;
    if (statm >> virtualPages >> residentPages) {
        static_cast<void>(virtualPages);
        const long pageSize = sysconf(_SC_PAGESIZE);
        if (pageSize > 0) {
            stats.residentBytes_ = residentPages * static_cast<std::size_t>(pageSize);
        }
    }
    rusage usage{};
    if (getrusage(RUSAGE_SELF, &usage) == 0) {
        stats.peakResidentBytes_ = static_cast<std::size_t>(usage.ru_maxrss) * 1024u;
    }
#endif
    return stats;
}

} // namespace IRProfile
