#include <irreden/ir_profile.hpp>

#include <irreden/profile/logger_spd.hpp>
#include <irreden/profile/cpu_profiler.hpp>

#include <atomic>

namespace IRProfile {

namespace {
std::atomic<bool> g_loggingEnabled{true};
} // namespace

bool isLoggingEnabled() {
    return g_loggingEnabled.load(std::memory_order_relaxed);
}

void shutdownLogging() {
    g_loggingEnabled.store(false, std::memory_order_relaxed);
}

} // namespace IRProfile