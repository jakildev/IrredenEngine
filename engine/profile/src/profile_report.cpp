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
    if (values.empty())
        return p;

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
        if (pipeline == kPipelineOrder[i])
            return i;
    }
    return 99;
}

// One value per recorded frame, in frame order, wrapped at perLine values; an
// empty series writes nothing, header included.
template <typename T, typename PrintValue>
void writeSeries(
    FILE *f, const char *header, const std::vector<T> &values, size_t perLine, PrintValue printValue
) {
    if (values.empty())
        return;
    std::fputs(header, f);
    for (size_t i = 0; i < values.size(); ++i) {
        printValue(values[i]);
        const bool endsLine = (i + 1) % perLine == 0 || i + 1 == values.size();
        std::fputc(endsLine ? '\n' : ' ', f);
    }
    std::fputc('\n', f);
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
        std::fprintf(
            f,
            "Frame time:   avg=%.2fms   p50=%.2fms   p95=%.2fms   p99=%.2fms   "
            "min=%.2fms   max=%.2fms\n",
            fp.avg_,
            fp.p50_,
            fp.p95_,
            fp.p99_,
            fp.min_,
            fp.max_
        );
        const size_t warmupFrames = report.frameTimesMs_.size() / kProfileWarmupDivisor;
        Percentiles steady = computePercentiles(
            std::vector<float>(
                report.frameTimesMs_.begin() + static_cast<std::ptrdiff_t>(warmupFrames),
                report.frameTimesMs_.end()
            )
        );
        std::fprintf(
            f,
            "Steady frame time (first %zu of %zu frames excluded):   avg=%.2fms   p50=%.2fms   "
            "p95=%.2fms   p99=%.2fms   min=%.2fms   max=%.2fms\n",
            warmupFrames,
            report.frameTimesMs_.size(),
            steady.avg_,
            steady.p50_,
            steady.p95_,
            steady.p99_,
            steady.min_,
            steady.max_
        );
    }

    float avgUpdateTicks = report.totalFrames_ > 0 ? static_cast<float>(report.totalUpdateTicks_) /
                                                         static_cast<float>(report.totalFrames_)
                                                   : 0.0f;
    std::fprintf(
        f,
        "Update ticks: avg=%.1f/frame  max=%u\n",
        avgUpdateTicks,
        report.maxUpdateTicksPerFrame_
    );
    std::fprintf(
        f,
        "Entity count: %llu (%u archetypes)\n",
        static_cast<unsigned long long>(report.entityCount_),
        report.archetypeCount_
    );
    std::fprintf(f, "\n");

    // --- Per-system timing ---
    if (!report.systemTimings_.empty()) {
        // Sort by pipeline order, then by total time descending within each pipeline.
        std::vector<const SystemTimingEntry *> sorted;
        sorted.reserve(report.systemTimings_.size());
        for (auto &e : report.systemTimings_) {
            sorted.push_back(&e);
        }
        std::sort(
            sorted.begin(),
            sorted.end(),
            [](const SystemTimingEntry *a, const SystemTimingEntry *b) {
                int ra = pipelineRank(a->pipeline_);
                int rb = pipelineRank(b->pipeline_);
                if (ra != rb)
                    return ra < rb;
                return a->totalNs_ > b->totalNs_;
            }
        );

        std::fprintf(f, "--- Per-system timing (by pipeline, then total descending) ---\n");
        std::fprintf(
            f,
            "%-8s %-36s %10s %9s %9s %9s %7s %10s\n",
            "Pipeline",
            "System",
            "Total(ms)",
            "Avg(ms)",
            "Min(ms)",
            "Max(ms)",
            "Calls",
            "Entities"
        );

        for (auto *entry : sorted) {
            if (entry->callCount_ == 0)
                continue;
            double totalMs = static_cast<double>(entry->totalNs_) / 1e6;
            double avgMs = totalMs / static_cast<double>(entry->callCount_);
            double minMs = static_cast<double>(entry->minNs_) / 1e6;
            double maxMs = static_cast<double>(entry->maxNs_) / 1e6;

            std::fprintf(
                f,
                "%-8s %-36s %10.2f %9.3f %9.3f %9.3f %7u %10llu\n",
                entry->pipeline_.c_str(),
                entry->name_.c_str(),
                totalMs,
                avgMs,
                minMs,
                maxMs,
                entry->callCount_,
                static_cast<unsigned long long>(entry->totalEntityCount_)
            );
        }
        std::fprintf(f, "\n");
    }

    std::fprintf(f, "--- GPU frame timing ---\n");
    std::fprintf(
        f,
        "Envelope includes inter-submission gaps; buffer spans include GPU stalls.\n"
        "Neither is GPU busy time. commandBuffers counts valid frames only.\n"
    );
    const auto validGpuFrames =
        report.gpuFrameTimings_.empty() ? 0u : report.gpuFrameTimings_.front().sampleCount_;
    std::fprintf(
        f,
        "Coverage: supported=%u attempted=%llu valid=%u invalid=%llu commandBuffers=%llu\n",
        report.gpuFrameTimingSupported_ ? 1u : 0u,
        static_cast<unsigned long long>(report.gpuFrameAttempted_),
        validGpuFrames,
        static_cast<unsigned long long>(report.gpuFrameInvalid_),
        static_cast<unsigned long long>(report.gpuFrameCommandBuffers_)
    );
    std::fprintf(f, "Metric Avg(ms) Min(ms) Max(ms) Samples\n");
    for (const auto &metric : report.gpuFrameTimings_) {
        if (metric.sampleCount_ == 0)
            continue;
        std::fprintf(
            f,
            "%s %.3f %.3f %.3f %u\n",
            metric.name_.c_str(),
            metric.totalMs_ / metric.sampleCount_,
            metric.minMs_,
            metric.maxMs_,
            metric.sampleCount_
        );
    }
    std::fprintf(f, "\n");

    // --- GPU stage timing ---
    if (!report.gpuStages_.empty()) {
        std::fprintf(f, "--- GPU stage timing ---\n");
        std::fprintf(
            f,
            "%-36s %9s %9s %9s %7s\n",
            "Stage",
            "Avg(ms)",
            "Min(ms)",
            "Max(ms)",
            "Samples"
        );

        for (auto &stage : report.gpuStages_) {
            if (stage.sampleCount_ == 0)
                continue;
            float avgMs = stage.totalMs_ / static_cast<float>(stage.sampleCount_);
            std::fprintf(
                f,
                "%-36s %9.3f %9.3f %9.3f %7u\n",
                stage.name_.c_str(),
                avgMs,
                stage.minMs_,
                stage.maxMs_,
                stage.sampleCount_
            );
        }
        std::fprintf(f, "\n");
    }

    // --- Voxel cull stats ---
    if (report.voxelCullStats_.sampleCount_ > 0) {
        const auto &c = report.voxelCullStats_;
        const double samples = static_cast<double>(c.sampleCount_);
        const double avgVisible = static_cast<double>(c.visibleSum_) / samples;
        const double avgTotal = static_cast<double>(c.totalSum_) / samples;
        const double avgRetained = static_cast<double>(c.visibleSum_ + c.feederSum_) / samples;
        const double ratio = avgTotal > 0.0 ? avgRetained / avgTotal : 0.0;
        std::fprintf(f, "--- Voxel cull stats ---\n");
        std::fprintf(f, "%-12s %14s %14s %10s\n", "", "Avg", "Max", "Samples");
        std::fprintf(
            f,
            "%-12s %14.1f %14u %10u\n",
            "Visible",
            avgVisible,
            c.maxVisible_,
            c.sampleCount_
        );
        std::fprintf(f, "%-12s %14.1f %14u %10u\n", "Total", avgTotal, c.maxTotal_, c.sampleCount_);
        std::fprintf(
            f,
            "%-12s %14.1f %14u %10u\n",
            "AxisEntries",
            static_cast<double>(c.axisEntrySum_) / samples,
            c.maxAxisEntries_,
            c.sampleCount_
        );
        const double avgFeeder = static_cast<double>(c.feederSum_) / samples;
        std::fprintf(f, "%-12s %14.1f %14u %10u\n", "Feeder", avgFeeder, c.maxFeeder_, c.sampleCount_);
        std::fprintf(
            f,
            "Ratio:       %14.4f (unique retained candidates / pool slots; includes separate "
            "feeders)\n",
            ratio
        );
        std::fprintf(
            f,
            "Feeder ratio: cross-run only — this report's Avg is one config's feeder count, "
            "not a total to divide by. Compute pv-on Avg / pv-off Avg from two separate "
            "--occlusion-cull on/off invocations of the same scene.\n"
        );
        std::fprintf(f, "\n");
    }

    // --- CPU phase timing ---
    if (!report.cpuPhases_.empty()) {
        std::fprintf(f, "--- CPU phase timing ---\n");
        std::fprintf(f, "%-36s %9s %9s %7s\n", "Phase", "Avg(ms)", "Max(ms)", "Samples");

        for (auto &phase : report.cpuPhases_) {
            if (phase.sampleCount_ == 0)
                continue;
            double avgMs = phase.totalMs_ / static_cast<double>(phase.sampleCount_);
            std::fprintf(
                f,
                "%-36s %9.3f %9.3f %7u\n",
                phase.name_.c_str(),
                avgMs,
                phase.maxMs_,
                phase.sampleCount_
            );
        }
        std::fprintf(f, "\n");
    }

    // scripts/perf/compare_perf_runs.py parses this section, the steady frame
    // line and the frame series by their literal text, and
    // scripts/perf/test_profile_parser.py pins that text to these format strings.
    const auto &w = report.witness_;
    std::fprintf(f, "--- Run witness ---\n");
    std::fprintf(
        f,
        "Camera yaw: first=%.3fdeg last=%.3fdeg travel=%.3fdeg samples=%u\n",
        w.yawFirstDeg_,
        w.yawLastDeg_,
        w.yawTravelDeg_,
        w.poseSamples_
    );
    std::fprintf(f, "Camera zoom: first=%.3f last=%.3f\n", w.zoomFirst_, w.zoomLast_);
    std::fprintf(
        f,
        "Per-axis overflow: maxEntries=%u maxDropped=%u cap=%u samples=%u\n",
        w.maxOverflowEntries_,
        w.maxOverflowDropped_,
        w.overflowCap_,
        w.overflowSamples_
    );
    std::fprintf(f, "\n");

    if (report.frameTimesMs_.size() <= kProfileSeriesMaxFrames) {
        writeSeries(
            f,
            "--- Frame times (ms, in order) ---\n",
            report.frameTimesMs_,
            10,
            [f](float ms) { std::fprintf(f, "%.3f", ms); }
        );
        writeSeries(
            f,
            "--- Update ticks (per frame, in order) ---\n",
            report.frameUpdateTicks_,
            30,
            [f](uint32_t ticks) { std::fprintf(f, "%u", ticks); }
        );
    }

    std::fprintf(f, "=== END REPORT ===\n");
    std::fclose(f);
}

} // namespace IRProfile
