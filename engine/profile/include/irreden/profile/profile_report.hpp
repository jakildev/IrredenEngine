#ifndef IR_PROFILE_REPORT_H
#define IR_PROFILE_REPORT_H

#include <cstdint>
#include <string>
#include <vector>

namespace IRProfile {

/// Per-system timing entry for agent-readable profile reports.
struct SystemTimingEntry {
    std::string name_;
    std::string pipeline_; // "INPUT", "UPDATE", "RENDER"
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
    float minMs_ = 0.0f;
    float maxMs_ = 0.0f;
    uint32_t sampleCount_ = 0;
};

/// Named CPU phase timing entry for manually instrumented system sub-blocks.
struct CpuPhaseEntry {
    std::string name_;
    double totalMs_ = 0.0;
    double maxMs_ = 0.0;
    uint32_t sampleCount_ = 0;
};

/// Voxel cull-effectiveness summary populated from a per-frame
/// accumulator in VOXEL_TO_TRIXEL_STAGE_1. avgVisible / avgTotal at
/// zoom Z answer "did culling actually shrink the working set?" A
/// flat ratio across zooms is the signature of broken culling.
struct VoxelCullStatsSummary {
    uint64_t visibleSum_ = 0;
    uint64_t totalSum_ = 0;
    // Shadow-feeder (struct 1) survivors — the off-screen caster list the
    // #2298 domain-widened per-voxel cull targets. 0 with shadows off.
    uint64_t feederSum_ = 0;
    uint32_t maxVisible_ = 0;
    uint32_t maxTotal_ = 0;
    uint32_t maxFeeder_ = 0;
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
    std::vector<CpuPhaseEntry> cpuPhases_;
    VoxelCullStatsSummary voxelCullStats_;
};

/// Write a plain-text profile report to the given file path.
/// Creates parent directories if they don't exist.
void writeProfileReport(const ProfileReport &report, const char *outputPath);

} // namespace IRProfile

#endif /* IR_PROFILE_REPORT_H */
