#ifndef PROCESS_MEMORY_H
#define PROCESS_MEMORY_H

#include <cstddef>

namespace IRProfile {

struct ProcessMemoryStats {
    std::size_t residentBytes_ = 0;
    std::size_t peakResidentBytes_ = 0;
};

ProcessMemoryStats processMemoryStats();

} // namespace IRProfile

#endif /* PROCESS_MEMORY_H */
