#ifndef PROFILE_REPORT_H
#define PROFILE_REPORT_H

#include <cstdint>
#include <string>
#include <vector>

namespace IRProfile {

/// Per-system timing entry for agent-readable profile reports.
struct SystemTimingEntry {
    std::string name_;
    std::string pipeline_;  // "INPUT", "UPDATE", "RENDER"
    uint64_t totalNs_ = 0;
    uint64_t minNs_ = UINT64_MAX;
    uint64_t maxNs_ = 0;
    uint32_t callCount_ = 0;
    uint64_t totalEntityCount_ = 0;
};

/// Per-GPU-stage timing entry.
struct GpuStageEntry {
    std::string name_;
    float totalMs_ = 0.0f;
    float maxMs_ = 0.0f;
    uint32_t sampleCount_ = 0;
};

/// Aggregated data for a profile report, populated by World at shutdown.
struct ProfileReport {
    std::vector<float> frameTimesMs_;
    uint32_t totalFrames_ = 0;
    uint32_t totalUpdateTicks_ = 0;
    uint32_t maxUpdateTicksPerFrame_ = 0;
    uint64_t entityCount_ = 0;
    uint32_t archetypeCount_ = 0;
    std::vector<SystemTimingEntry> systemTimings_;
    std::vector<GpuStageEntry> gpuStages_;
};

/// Write a plain-text profile report to the given file path.
/// Creates parent directories if they don't exist.
void writeProfileReport(const ProfileReport &report, const char *outputPath);

} // namespace IRProfile

#endif /* PROFILE_REPORT_H */
