#include <irreden/profile/profile_report.hpp>
#include <irreden/ir_profile.hpp>

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <numeric>

namespace IRProfile {

namespace {

struct Percentiles {
    float p50_ = 0.0f;
    float p95_ = 0.0f;
    float p99_ = 0.0f;
    float avg_ = 0.0f;
    float min_ = 0.0f;
    float max_ = 0.0f;
};

Percentiles computePercentiles(std::vector<float> values) {
    Percentiles p;
    if (values.empty()) return p;

    std::sort(values.begin(), values.end());
    float sum = std::accumulate(values.begin(), values.end(), 0.0f);
    size_t n = values.size();

    p.avg_ = sum / static_cast<float>(n);
    p.min_ = values.front();
    p.max_ = values.back();
    p.p50_ = values[n * 50 / 100];
    p.p95_ = values[std::min(n * 95 / 100, n - 1)];
    p.p99_ = values[std::min(n * 99 / 100, n - 1)];
    return p;
}

const char *kPipelineOrder[] = {"INPUT", "UPDATE", "RENDER"};

int pipelineRank(const std::string &pipeline) {
    for (int i = 0; i < 3; ++i) {
        if (pipeline == kPipelineOrder[i]) return i;
    }
    return 99;
}

} // namespace

void writeProfileReport(const ProfileReport &report, const char *outputPath) {
    std::filesystem::path outPath(outputPath);
    std::filesystem::create_directories(outPath.parent_path());

    FILE *f = std::fopen(outputPath, "w");
    if (!f) {
        IRE_LOG_ERROR("Failed to open profile report output: {}", outputPath);
        return;
    }

    // --- Header ---
    std::fprintf(f, "=== PROFILE REPORT (%u frames) ===\n", report.totalFrames_);

    // --- Frame timing ---
    if (!report.frameTimesMs_.empty()) {
        Percentiles fp = computePercentiles(report.frameTimesMs_);
        std::fprintf(f, "Frame time:   avg=%.2fms   p50=%.2fms   p95=%.2fms   p99=%.2fms   "
                        "min=%.2fms   max=%.2fms\n",
                     fp.avg_, fp.p50_, fp.p95_, fp.p99_, fp.min_, fp.max_);
    }

    float avgUpdateTicks = report.totalFrames_ > 0
        ? static_cast<float>(report.totalUpdateTicks_) / static_cast<float>(report.totalFrames_)
        : 0.0f;
    std::fprintf(f, "Update ticks: avg=%.1f/frame  max=%u\n",
                 avgUpdateTicks, report.maxUpdateTicksPerFrame_);
    std::fprintf(f, "Entity count: %llu (%u archetypes)\n",
                 static_cast<unsigned long long>(report.entityCount_),
                 report.archetypeCount_);
    std::fprintf(f, "\n");

    // --- Per-system timing ---
    if (!report.systemTimings_.empty()) {
        // Sort by pipeline order, then by total time descending within each pipeline.
        std::vector<const SystemTimingEntry *> sorted;
        sorted.reserve(report.systemTimings_.size());
        for (auto &e : report.systemTimings_) {
            sorted.push_back(&e);
        }
        std::sort(sorted.begin(), sorted.end(),
                  [](const SystemTimingEntry *a, const SystemTimingEntry *b) {
                      int ra = pipelineRank(a->pipeline_);
                      int rb = pipelineRank(b->pipeline_);
                      if (ra != rb) return ra < rb;
                      return a->totalNs_ > b->totalNs_;
                  });

        std::fprintf(f, "--- Per-system timing (by pipeline, then total descending) ---\n");
        std::fprintf(f, "%-8s %-36s %10s %9s %9s %9s %7s %10s\n",
                     "Pipeline", "System", "Total(ms)", "Avg(ms)", "Min(ms)", "Max(ms)",
                     "Calls", "Entities");

        for (auto *entry : sorted) {
            if (entry->callCount_ == 0) continue;
            double totalMs = static_cast<double>(entry->totalNs_) / 1e6;
            double avgMs = totalMs / static_cast<double>(entry->callCount_);
            double minMs = static_cast<double>(entry->minNs_) / 1e6;
            double maxMs = static_cast<double>(entry->maxNs_) / 1e6;

            std::fprintf(f, "%-8s %-36s %10.2f %9.3f %9.3f %9.3f %7u %10llu\n",
                         entry->pipeline_.c_str(),
                         entry->name_.c_str(),
                         totalMs, avgMs, minMs, maxMs,
                         entry->callCount_,
                         static_cast<unsigned long long>(entry->totalEntityCount_));
        }
        std::fprintf(f, "\n");
    }

    // --- GPU stage timing ---
    if (!report.gpuStages_.empty()) {
        std::fprintf(f, "--- GPU stage timing ---\n");
        std::fprintf(f, "%-36s %9s %9s\n", "Stage", "Avg(ms)", "Max(ms)");

        for (auto &stage : report.gpuStages_) {
            if (stage.sampleCount_ == 0) continue;
            float avgMs = stage.totalMs_ / static_cast<float>(stage.sampleCount_);
            std::fprintf(f, "%-36s %9.3f %9.3f\n",
                         stage.name_.c_str(), avgMs, stage.maxMs_);
        }
        std::fprintf(f, "\n");
    }

    std::fprintf(f, "=== END REPORT ===\n");
    std::fclose(f);
}

} // namespace IRProfile
