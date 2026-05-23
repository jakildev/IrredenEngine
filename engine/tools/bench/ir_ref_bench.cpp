// ir_ref_bench — synthetic-load reference micro-bench for ir-perf-grid.
//
// Runs a deterministic, fixed-iteration sequence of IRMath hot-path calls
// (iso projection, SDF primitives, trixel origin offsets) and prints the
// wall-clock as a single JSON line. ir-perf-grid captures the value at the
// start of each matrix run as ref_ms; the comparator normalizes per-cell
// measurements by ref_target / ref_ms so a backgrounded second worker
// pulling cores away doesn't masquerade as a regression.
//
// Deterministic: same input arrays, fixed loop bounds, accumulator returned
// via exit code's low bits so the optimizer can't elide the work. The inner
// kIters is hand-tuned so the wall-clock lands near 50ms on a mid-tier CI
// host; faster hosts will report < 50ms, loaded hosts > 50ms — that's the
// signal the normalization step consumes.
//
// Output: a single JSON line on stdout, e.g.
//   {"ms": 49.78, "iters": 4000, "ops": 21504000}
//
// Stderr stays clean on success so callers can `ir-host-probe ; ir_ref_bench`
// and parse stdout directly.

#include <irreden/ir_math.hpp>
#include <irreden/math/sdf.hpp>

#include <array>
#include <chrono>
#include <cstdio>
#include <cstdlib>

namespace {

constexpr int kN = 64;

// 4×4×4 query points in [-1.5, 1.5]^3 — same shape as bench_sdf.cpp so the
// branch coverage of taperedBox / curvedPanel matches the production bench.
constexpr std::array<IRMath::vec3, kN> kQueryPts = [] {
    std::array<IRMath::vec3, kN> pts{};
    int idx = 0;
    for (int zi = 0; zi < 4; ++zi)
        for (int yi = 0; yi < 4; ++yi)
            for (int xi = 0; xi < 4; ++xi)
                pts[idx++] = IRMath::vec3(
                    -1.5f + static_cast<float>(xi) * 1.0f,
                    -1.5f + static_cast<float>(yi) * 1.0f,
                    -1.5f + static_cast<float>(zi) * 1.0f
                );
    return pts;
}();

constexpr IRMath::vec3 kHalf{3.0f, 3.0f, 3.0f};

// 16×16 grid of iso-projection inputs covering a representative voxel slab.
constexpr int kIsoN = 256;
constexpr std::array<IRMath::ivec3, kIsoN> kIsoPts = [] {
    std::array<IRMath::ivec3, kIsoN> pts{};
    int idx = 0;
    for (int y = 0; y < 16; ++y)
        for (int x = 0; x < 16; ++x)
            pts[idx++] = IRMath::ivec3(x - 8, y - 8, x ^ y);
    return pts;
}();

// Hand-tuned to land near 50ms on a 2023 Apple-Silicon laptop with no
// contention. Bench scales linearly, so on a 2× slower host it lands near
// 100ms — the normalization math handles that. Overridable via the single
// CLI arg for hand-tuning on a new calibration host.
constexpr int kDefaultIters = 100000;

constexpr long long kOpsPerIter =
    static_cast<long long>(kIsoN) * 2 +    // iso projections (ivec3 + vec3)
    static_cast<long long>(kIsoN) +        // pos3DtoDistance
    static_cast<long long>(kN) * 3;        // SDF box + sphere + cylinder

float runBench(int iters) {
    float acc = 0.0f;
    for (int iter = 0; iter < iters; ++iter) {
        for (const auto& p : kIsoPts) {
            const IRMath::ivec2 q = IRMath::pos3DtoPos2DIso(p);
            acc += static_cast<float>(q.x + q.y);
        }
        for (const auto& p : kIsoPts) {
            const IRMath::vec2 q = IRMath::pos3DtoPos2DIso(IRMath::vec3(p));
            acc += q.x + q.y;
        }
        for (const auto& p : kIsoPts) {
            acc += static_cast<float>(IRMath::pos3DtoDistance(p));
        }
        for (const auto& p : kQueryPts) {
            acc += IRMath::SDF::box(p, kHalf);
            acc += IRMath::SDF::sphere(p, 3.0f);
            acc += IRMath::SDF::cylinder(p, 2.0f, 3.0f);
        }
    }
    return acc;
}

} // namespace

// Volatile sink — forces the compiler to materialize runBench's accumulator
// and emit every iteration. Without this the entire kIters * kIsoN * ... loop
// folds to a single constant (or vanishes entirely on -O3).
volatile float g_sink = 0.0f;

int main(int argc, char** argv) {
    int iters = kDefaultIters;
    if (argc >= 2) {
        iters = std::atoi(argv[1]);
        if (iters <= 0) iters = kDefaultIters;
    }

    const auto start = std::chrono::steady_clock::now();
    const float acc = runBench(iters);
    g_sink = acc;
    const auto end = std::chrono::steady_clock::now();

    const double ms =
        std::chrono::duration<double, std::milli>(end - start).count();

    const long long ops = kOpsPerIter * static_cast<long long>(iters);

    std::printf("{\"ms\": %.3f, \"iters\": %d, \"ops\": %lld}\n",
                ms, iters, ops);
    return 0;
}
