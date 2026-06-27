// jitter_probe — temporal-jitter detector for render-verification sweeps.
//
// img_diff catches SPATIAL drift between two frames; jitter_probe catches
// TEMPORAL jitter across a SEQUENCE of frames captured while the camera moves
// smoothly (a pan or yaw sweep). A correct pipeline translates a shape SMOOTHLY:
// its centroid follows a straight line, so the per-frame delta keeps one sign
// and the residual off that line stays sub-pixel. A jittering pipeline (e.g. an
// integer canvas anchor whose sub-pixel compensation is at the wrong scale)
// makes the centroid oscillate — the delta reverses sign and the residual spikes
// — even though each individual frame looks fine. That oscillation is invisible
// in any single screenshot; it only shows up across the sequence.
//
// Usage:
//   jitter_probe <frame_000.png> <frame_001.png> ... <frame_N.png>
//     [--threshold L]      foreground = pixels with (R+G+B) > L (0..765, default 24)
//     [--color R,G,B,T]    instead, foreground = pixels within T of color R,G,B
//     [--reversal-eps PX]  per-frame deltas under this are treated as 0 (default 0.10)
//     [--max-residual PX]  SMOOTH verdict requires residual <= this (default 1.50)
//     [--verbose]          print the per-frame centroid + residual table
//
// Capture the sequence with an ISOLATED shape on a black field so the centroid
// is clean — e.g. `shape_debug --spin-shape box --spin-shape-voxel --pan-sweep`
// (pan jitter) or `--yaw-sweep` (rotation jitter). For a multi-shape scene pass
// --color to lock onto one shape. Frames MUST be given in capture order.
//
// Exit: 0 = SMOOTH, 1 = JITTER detected, 2 = argument / IO error. (Mirrors
// img_diff's 0/1/2 convention so it slots into the same verification scripts.)

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#include <irreden/ir_args.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace {

struct Args {
    std::vector<std::string> frames_;
    int threshold_ = 24;
    bool useColor_ = false;
    int colorR_ = 0, colorG_ = 0, colorB_ = 0, colorTol_ = 0;
    double reversalEps_ = 0.10;
    double maxResidual_ = 1.50;
    bool verbose_ = false;
};

// Foreground centroid (mean x,y of matching pixels) for one frame.
// Returns false if too few pixels match (shape off-screen / empty frame).
bool centroid(const Args &args, const std::string &path, double &cx, double &cy, long &count) {
    int w = 0, h = 0, channels = 0;
    unsigned char *data = stbi_load(path.c_str(), &w, &h, &channels, 4);
    if (!data) {
        std::fprintf(
            stderr,
            "jitter_probe: failed to load '%s': %s\n",
            path.c_str(),
            stbi_failure_reason()
        );
        return false;
    }
    double sx = 0.0, sy = 0.0;
    long n = 0;
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const unsigned char *p = data + (static_cast<long>(y) * w + x) * 4;
            bool fg;
            if (args.useColor_) {
                fg = std::abs(int(p[0]) - args.colorR_) <= args.colorTol_ &&
                     std::abs(int(p[1]) - args.colorG_) <= args.colorTol_ &&
                     std::abs(int(p[2]) - args.colorB_) <= args.colorTol_;
            } else {
                fg = (int(p[0]) + int(p[1]) + int(p[2])) > args.threshold_;
            }
            if (fg) {
                sx += x;
                sy += y;
                ++n;
            }
        }
    }
    stbi_image_free(data);
    count = n;
    if (n < 50) {
        return false;
    }
    cx = sx / double(n);
    cy = sy / double(n);
    return true;
}

// Least-squares line fit y = m*i + b over the valid samples; fills residual[].
void detrend(
    const std::vector<double> &v, const std::vector<bool> &valid, std::vector<double> &residual
) {
    double si = 0, sv = 0, sii = 0, siv = 0;
    int n = 0;
    for (size_t i = 0; i < v.size(); ++i) {
        if (!valid[i])
            continue;
        si += i;
        sv += v[i];
        sii += double(i) * i;
        siv += double(i) * v[i];
        ++n;
    }
    double m = 0, b = 0;
    const double denom = n * sii - si * si;
    if (n >= 2 && std::fabs(denom) > 1e-9) {
        m = (n * siv - si * sv) / denom;
        b = (sv - m * si) / n;
    } else if (n > 0) {
        b = sv / n;
    }
    residual.assign(v.size(), 0.0);
    for (size_t i = 0; i < v.size(); ++i) {
        residual[i] = valid[i] ? v[i] - (m * i + b) : 0.0;
    }
}

struct AxisStats {
    int reversals_ = 0;
    double maxAbsResidual_ = 0.0;
    double deltaStd_ = 0.0;
    double deltaMaxAbs_ = 0.0;
};

AxisStats analyze(
    const std::vector<double> &v,
    const std::vector<bool> &valid,
    double eps,
    std::vector<double> &residual
) {
    detrend(v, valid, residual);
    AxisStats s;
    for (double r : residual)
        s.maxAbsResidual_ = std::max(s.maxAbsResidual_, std::fabs(r));

    // Per-frame delta sign reversals (jitter) + delta spread, over consecutive
    // valid pairs.
    std::vector<double> deltas;
    int prevSign = 0;
    for (size_t i = 1; i < v.size(); ++i) {
        if (!valid[i] || !valid[i - 1])
            continue;
        const double d = v[i] - v[i - 1];
        deltas.push_back(d);
        const int sign = (d > eps) ? 1 : (d < -eps) ? -1 : 0;
        if (sign != 0 && prevSign != 0 && sign != prevSign)
            ++s.reversals_;
        if (sign != 0)
            prevSign = sign;
    }
    double mean = 0;
    for (double d : deltas) {
        mean += d;
        s.deltaMaxAbs_ = std::max(s.deltaMaxAbs_, std::fabs(d));
    }
    if (!deltas.empty()) {
        mean /= deltas.size();
        double var = 0;
        for (double d : deltas)
            var += (d - mean) * (d - mean);
        s.deltaStd_ = std::sqrt(var / deltas.size());
    }
    return s;
}

} // namespace

int main(int argc, char **argv) {
    IRArgs::Parser parser(
        "jitter_probe — temporal-jitter detector across a frame sequence captured "
        "during a smooth camera sweep. Frames MUST be given in capture order. "
        "Exit: 0 = SMOOTH, 1 = JITTER, 2 = argument/IO error.",
        IRArgs::Common::NONE
    );
    parser.integer("--threshold", "Foreground = pixels with (R+G+B) > L (0..765)", 24);
    parser.string("--color", "Foreground = pixels within T of color R,G,B (format: R,G,B,T)", "");
    parser.number("--reversal-eps", "Per-frame deltas under this (px) are treated as 0", 0.10f);
    parser.number("--max-residual", "SMOOTH verdict requires residual <= this (px)", 1.50f);
    parser.flag("--verbose", "Print the per-frame centroid + residual table");
    parser.variadic("frames", "Frame PNGs in capture order (>= 3)", 3);
    parser.parse(argc, argv);

    Args args;
    args.threshold_ = parser.getInt("--threshold");
    args.reversalEps_ = parser.getFloat("--reversal-eps");
    args.maxResidual_ = parser.getFloat("--max-residual");
    args.verbose_ = parser.getFlag("--verbose");
    args.frames_ = parser.positionalArgs();
    if (parser.wasProvided("--color")) {
        args.useColor_ = true;
        const std::string color = parser.getString("--color");
        if (std::sscanf(
                color.c_str(), "%d,%d,%d,%d", &args.colorR_, &args.colorG_, &args.colorB_,
                &args.colorTol_
            ) != 4) {
            std::fprintf(stderr, "jitter_probe: --color expects R,G,B,T\n");
            return 2;
        }
    }

    const size_t n = args.frames_.size();
    std::vector<double> cx(n, 0.0), cy(n, 0.0);
    std::vector<bool> valid(n, false);
    int validCount = 0;
    for (size_t i = 0; i < n; ++i) {
        long count = 0;
        if (centroid(args, args.frames_[i], cx[i], cy[i], count)) {
            valid[i] = true;
            ++validCount;
        } else {
            std::fprintf(
                stderr,
                "jitter_probe: frame %zu '%s' has no usable foreground (%ld px)\n",
                i,
                args.frames_[i].c_str(),
                count
            );
        }
    }
    if (validCount < 3) {
        std::fprintf(stderr, "jitter_probe: < 3 frames had a usable foreground\n");
        return 2;
    }

    std::vector<double> rx, ry;
    const AxisStats sx = analyze(cx, valid, args.reversalEps_, rx);
    const AxisStats sy = analyze(cy, valid, args.reversalEps_, ry);

    if (args.verbose_) {
        std::printf("frame    centroid_x  resid_x    centroid_y  resid_y\n");
        for (size_t i = 0; i < n; ++i) {
            if (!valid[i]) {
                std::printf("%5zu    (empty)\n", i);
                continue;
            }
            std::printf("%5zu    %9.2f  %+7.2f    %9.2f  %+7.2f\n", i, cx[i], rx[i], cy[i], ry[i]);
        }
    }

    const bool smooth = sx.reversals_ == 0 && sy.reversals_ == 0 &&
                        sx.maxAbsResidual_ <= args.maxResidual_ &&
                        sy.maxAbsResidual_ <= args.maxResidual_;

    std::printf(
        "jitter_probe: frames=%zu (valid=%d)  verdict=%s\n"
        "  x: reversals=%d  max_residual=%.2fpx  delta_std=%.2f  delta_max=%.2f\n"
        "  y: reversals=%d  max_residual=%.2fpx  delta_std=%.2f  delta_max=%.2f\n"
        "  (thresholds: reversals=0, max_residual<=%.2fpx)\n",
        n,
        validCount,
        smooth ? "SMOOTH" : "JITTER",
        sx.reversals_,
        sx.maxAbsResidual_,
        sx.deltaStd_,
        sx.deltaMaxAbs_,
        sy.reversals_,
        sy.maxAbsResidual_,
        sy.deltaStd_,
        sy.deltaMaxAbs_,
        args.maxResidual_
    );
    return smooth ? 0 : 1;
}
