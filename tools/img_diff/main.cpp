// img_diff: highlight per-pixel drift between two PNGs.
//
// Spec: GitHub issue #435. Standalone tool with no engine dependencies — used
// by `render-debug-loop`, the `attach-screenshots` skill, and human reviewers
// to make pixel-level baseline drift impossible to miss.
//
// Output PNG layout:
//   * Drifted pixel  → solid red (255, 0, 0, 255)
//   * Unchanged      → baseline pixel desaturated to ~30% luminance, so the
//                      drift pops against quiet visible context (rather than
//                      a flat black field).
//
// A pixel "drifts" when ANY channel differs from the baseline by strictly
// more than `--threshold` (default 0). Alpha is included by default; pass
// `--ignore-alpha` to compare RGB only.

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

#include <irreden/ir_args.hpp>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>

namespace {

constexpr int kExitMatch = 0;
constexpr int kExitDrift = 1;
constexpr int kExitArgError = 2;

constexpr float kContextLuminanceScale = 0.3f;

struct Args {
    std::string baselinePath_;
    std::string currentPath_;
    std::string outputPath_;
    int threshold_ = 0;
    bool ignoreAlpha_ = false;
};

struct Image {
    unsigned char *data_ = nullptr;
    int width_ = 0;
    int height_ = 0;

    ~Image() {
        if (data_ != nullptr) {
            stbi_image_free(data_);
        }
    }
};

bool loadRgba(const std::string &path, Image &out) {
    int channels = 0;
    out.data_ = stbi_load(path.c_str(), &out.width_, &out.height_, &channels, 4);
    if (out.data_ == nullptr) {
        std::fprintf(
            stderr,
            "img_diff: failed to load '%s': %s\n",
            path.c_str(),
            stbi_failure_reason()
        );
        return false;
    }
    return true;
}

} // namespace

int main(int argc, char **argv) {
    IRArgs::Parser parser(
        "img_diff — highlight per-pixel drift between two PNGs. Drifted pixels are "
        "written red; unchanged pixels become a 30%-luminance copy of the baseline "
        "so context stays visible. Exit: 0 = no drift, 1 = drift, 2 = argument/IO error.",
        IRArgs::Common::NONE
    );
    parser.integer("--threshold", "Per-channel delta tolerance (default 0)", 0);
    parser.flag("--ignore-alpha", "Compare RGB only; ignore the alpha channel");
    parser.positional("baseline", "Baseline PNG");
    parser.positional("current", "Current PNG");
    parser.positional("out_diff", "Output diff PNG");
    parser.parse(argc, argv);

    Args args;
    args.baselinePath_ = parser.getPositional("baseline");
    args.currentPath_ = parser.getPositional("current");
    args.outputPath_ = parser.getPositional("out_diff");
    args.threshold_ = parser.getInt("--threshold");
    args.ignoreAlpha_ = parser.getFlag("--ignore-alpha");
    if (args.threshold_ < 0) {
        std::fprintf(stderr, "img_diff: --threshold must be >= 0\n");
        return kExitArgError;
    }

    Image baseline;
    Image current;
    if (!loadRgba(args.baselinePath_, baseline) || !loadRgba(args.currentPath_, current)) {
        return kExitArgError;
    }

    if (baseline.width_ != current.width_ || baseline.height_ != current.height_) {
        std::fprintf(
            stderr,
            "img_diff: dimension mismatch: baseline=%dx%d, current=%dx%d\n",
            baseline.width_, baseline.height_, current.width_, current.height_
        );
        return kExitArgError;
    }

    const int width = baseline.width_;
    const int height = baseline.height_;
    const int totalPixels = width * height;
    const int threshold = args.threshold_;
    const int channelsCompared = args.ignoreAlpha_ ? 3 : 4;

    auto *out = static_cast<unsigned char *>(std::malloc(static_cast<size_t>(totalPixels) * 4));
    if (out == nullptr) {
        std::fprintf(stderr, "img_diff: out of memory\n");
        return kExitArgError;
    }

    std::int64_t driftPixels = 0;
    int maxDelta = 0;

    for (int i = 0; i < totalPixels; ++i) {
        const int byteOffset = i * 4;
        int pixelMaxDelta = 0;
        for (int c = 0; c < channelsCompared; ++c) {
            const int b = baseline.data_[byteOffset + c];
            const int v = current.data_[byteOffset + c];
            const int d = (v >= b) ? (v - b) : (b - v);
            if (d > pixelMaxDelta) {
                pixelMaxDelta = d;
            }
        }
        if (pixelMaxDelta > maxDelta) {
            maxDelta = pixelMaxDelta;
        }

        const bool drifted = pixelMaxDelta > threshold;
        if (drifted) {
            ++driftPixels;
            out[byteOffset + 0] = 255;
            out[byteOffset + 1] = 0;
            out[byteOffset + 2] = 0;
            out[byteOffset + 3] = 255;
        } else {
            for (int c = 0; c < 3; ++c) {
                const int b = baseline.data_[byteOffset + c];
                const int dim = static_cast<int>(static_cast<float>(b) * kContextLuminanceScale);
                out[byteOffset + c] = static_cast<unsigned char>(dim);
            }
            out[byteOffset + 3] = 255;
        }
    }

    const int writeOk = stbi_write_png(
        args.outputPath_.c_str(),
        width,
        height,
        4,
        out,
        width * 4
    );
    std::free(out);
    if (writeOk == 0) {
        std::fprintf(stderr, "img_diff: failed to write '%s'\n", args.outputPath_.c_str());
        return kExitArgError;
    }

    const double driftPct = totalPixels == 0
        ? 0.0
        : (100.0 * static_cast<double>(driftPixels) / static_cast<double>(totalPixels));

    std::printf(
        "img_diff: total=%d drift=%lld (%.4f%%) max_delta=%d threshold=%d -> %s\n",
        totalPixels,
        static_cast<long long>(driftPixels),
        driftPct,
        maxDelta,
        threshold,
        args.outputPath_.c_str()
    );

    return driftPixels == 0 ? kExitMatch : kExitDrift;
}
