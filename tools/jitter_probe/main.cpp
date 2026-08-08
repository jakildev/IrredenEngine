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
//     [--max-excursion-x PX] SMOOTH verdict also requires x excursion <= this
//     [--max-excursion-y PX] ... same for y (each independently optional)
//     [--stationary]       assert the centroid does NOT move (pivot-pin check)
//     [--max-deviation PX] PINNED verdict requires deviation <= this (default 1.50)
//     [--verbose]          print the per-frame centroid + residual table
//     [--expect-frames N]  fail (exit 2) unless exactly N frames were passed
//
// Capture the sequence with an ISOLATED shape on a black field so the centroid
// is clean — e.g. `shape_debug --spin-shape box --spin-shape-voxel --pan-sweep`
// (pan jitter) or `--yaw-sweep` (rotation jitter). For a multi-shape scene pass
// --color to lock onto one shape. Frames MUST be given in capture order.
//
// --max-excursion-{x,y} answer a third question, per axis: how far did the
// centroid travel on that axis, end to end (max-min), regardless of HOW it got
// there. The line fit is blind to a perfectly smooth systematic migration — it
// fits it and calls the residual clean — so on a probe where one axis is
// supposed to stay PINNED while the other legitimately translates, the shipped
// criteria cannot express the contract and the migration scores SMOOTH (#2606).
// Because each flag is independently optional, "x stays pinned while y may
// translate" is exactly `--max-excursion-x <bar>` with y unconstrained, which
// --stationary (both axes pinned) cannot say.
//
// --stationary answers a different question than the default line-fit: the
// default asserts SMOOTH LINEAR motion (a sweep translates the shape without
// oscillation), while --stationary asserts NO motion at all — the shape's
// centroid holds its frame-0 position across the whole sequence. That is the
// rotation-pivot contract (a probe centered on the pivot must not move during a
// yaw sweep), and the line-fit cannot express it: a slow orbital arc fits a
// line well enough to pass SMOOTH while being exactly the pivot-drift bug.
// Verdict: PINNED iff max |centroid_i - centroid_0| <= --max-deviation on both
// axes. Use a Z-yaw-invariant probe (vertical cylinder) so silhouette change
// doesn't contaminate the centroid.
//
// Exit: 0 = SMOOTH/PINNED, 1 = JITTER/DRIFT detected, 2 = argument / IO error.
// (Mirrors img_diff's 0/1/2 convention so it slots into the same verification
// scripts.)

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
    bool hasMaxExcursionX_ = false;
    double maxExcursionX_ = 0.0;
    bool hasMaxExcursionY_ = false;
    double maxExcursionY_ = 0.0;
    bool stationary_ = false;
    double maxDeviation_ = 1.50;
    bool verbose_ = false;
};

// "%.2fpx" as a string. The thresholds line lists only the bars that were
// actually provided, so it is assembled rather than formatted in one shot.
std::string formatPx(double px) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.2fpx", px);
    return buf;
}

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

// Max deviation of each valid sample from the FIRST valid sample — the
// --stationary metric. The reference is frame 0 (not the mean) so a monotone
// drift accumulates against it instead of averaging itself half away.
double maxDeviationFromFirst(
    const std::vector<double> &v, const std::vector<bool> &valid, std::vector<double> &deviation
) {
    double ref = 0.0;
    bool haveRef = false;
    double maxDev = 0.0;
    deviation.assign(v.size(), 0.0);
    for (size_t i = 0; i < v.size(); ++i) {
        if (!valid[i])
            continue;
        if (!haveRef) {
            ref = v[i];
            haveRef = true;
        }
        deviation[i] = v[i] - ref;
        maxDev = std::max(maxDev, std::fabs(deviation[i]));
    }
    return maxDev;
}

// Peak-to-peak spread (max-min) of the valid samples — the excursion metric.
// Deliberately shape-blind: it says how far the centroid travelled on this axis
// end to end and nothing about how, so a perfectly smooth migration registers at
// full size where the line fit's residual reads ~0.
double excursion(const std::vector<double> &v, const std::vector<bool> &valid) {
    double lo = 0.0, hi = 0.0;
    bool haveRef = false;
    for (size_t i = 0; i < v.size(); ++i) {
        if (!valid[i])
            continue;
        if (!haveRef) {
            lo = v[i];
            hi = v[i];
            haveRef = true;
            continue;
        }
        lo = std::min(lo, v[i]);
        hi = std::max(hi, v[i]);
    }
    return haveRef ? hi - lo : 0.0;
}

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
    parser.number(
        "--max-excursion-x",
        "SMOOTH verdict also requires x excursion (max-min) <= this (px); omit to disable",
        0.0f
    );
    parser.number(
        "--max-excursion-y",
        "SMOOTH verdict also requires y excursion (max-min) <= this (px); omit to disable",
        0.0f
    );
    parser.flag("--stationary", "Assert the centroid does NOT move (rotation-pivot pin check)");
    parser.number("--max-deviation", "PINNED verdict requires deviation <= this (px)", 1.50f);
    parser.flag("--verbose", "Print the per-frame centroid + residual table");
    parser.integer(
        "--expect-frames",
        "Fail unless exactly this many frames were passed (omit to disable)",
        0
    );
    parser.variadic("frames", "Frame PNGs in capture order (>= 3)", 3);
    parser.parse(argc, argv);

    Args args;
    args.threshold_ = parser.getInt("--threshold");
    args.reversalEps_ = parser.getFloat("--reversal-eps");
    args.maxResidual_ = parser.getFloat("--max-residual");
    // Gate on wasProvided, not on a sentinel: 0.0 is a meaningful bar (assert the
    // axis does not move at all), so a default value cannot double as "unset".
    args.hasMaxExcursionX_ = parser.wasProvided("--max-excursion-x");
    args.maxExcursionX_ = parser.getFloat("--max-excursion-x");
    args.hasMaxExcursionY_ = parser.wasProvided("--max-excursion-y");
    args.maxExcursionY_ = parser.getFloat("--max-excursion-y");
    args.stationary_ = parser.getFlag("--stationary");
    args.maxDeviation_ = parser.getFloat("--max-deviation");
    args.verbose_ = parser.getFlag("--verbose");
    args.frames_ = parser.positionalArgs();
    if (parser.wasProvided("--color")) {
        args.useColor_ = true;
        const std::string color = parser.getString("--color");
        if (std::sscanf(
                color.c_str(),
                "%d,%d,%d,%d",
                &args.colorR_,
                &args.colorG_,
                &args.colorB_,
                &args.colorTol_
            ) != 4) {
            std::fprintf(stderr, "jitter_probe: --color expects R,G,B,T\n");
            return 2;
        }
    }

    // --stationary takes a separate output path and verdict, so an excursion bar
    // passed alongside it would be silently ignored — an assertion the caller
    // believes is live but that can never fire. Reject the combination instead.
    // (It also keeps the --stationary summary byte-identical for its one stdout
    // parser, scripts/pivot-verify.py.)
    if (args.stationary_ && (args.hasMaxExcursionX_ || args.hasMaxExcursionY_)) {
        std::fprintf(
            stderr,
            "jitter_probe: --max-excursion-x/-y cannot be combined with --stationary "
            "(--stationary asserts BOTH axes are pinned and uses its own verdict; the "
            "excursion bars apply to the default smooth-motion verdict only). Drop "
            "--stationary to assert one axis independently.\n"
        );
        return 2;
    }

    // The capture dir is never cleared between runs and VideoManager numbers
    // *around* leftovers, so a shell glob silently widens to earlier runs and to
    // ROI crops. Scoring the wrong set does not fail — it produces a confident
    // verdict, indistinguishable from a real regression. See README.md
    // §"Wipe before every capture" for the measured case.
    if (parser.wasProvided("--expect-frames")) {
        const int expectArg = parser.getInt("--expect-frames");
        if (expectArg < 0) {
            std::fprintf(
                stderr,
                "jitter_probe: --expect-frames expects a non-negative count (got %d)\n",
                expectArg
            );
            return 2;
        }
        const size_t expected = static_cast<size_t>(expectArg);
        if (args.frames_.size() != expected) {
            std::fprintf(
                stderr,
                "jitter_probe: expected %zu frames but got %zu — the glob picked up a "
                "different set than the run produced (stale captures from an earlier run, "
                "or ROI crop files). Wipe the screenshots dir before capturing and match "
                "full frames only.\n",
                expected,
                args.frames_.size()
            );
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

    if (args.stationary_) {
        std::vector<double> dx, dy;
        const double devX = maxDeviationFromFirst(cx, valid, dx);
        const double devY = maxDeviationFromFirst(cy, valid, dy);
        if (args.verbose_) {
            std::printf("frame    centroid_x  dev_x      centroid_y  dev_y\n");
            for (size_t i = 0; i < n; ++i) {
                if (!valid[i]) {
                    std::printf("%5zu    (empty)\n", i);
                    continue;
                }
                std::printf(
                    "%5zu    %9.2f  %+7.2f    %9.2f  %+7.2f\n",
                    i,
                    cx[i],
                    dx[i],
                    cy[i],
                    dy[i]
                );
            }
        }
        const bool pinned = devX <= args.maxDeviation_ && devY <= args.maxDeviation_;
        std::printf(
            "jitter_probe: frames=%zu (valid=%d)  verdict=%s\n"
            "  x: max_deviation=%.2fpx\n"
            "  y: max_deviation=%.2fpx\n"
            "  (threshold: max_deviation<=%.2fpx vs frame 0)\n",
            n,
            validCount,
            pinned ? "PINNED" : "DRIFT",
            devX,
            devY,
            args.maxDeviation_
        );
        return pinned ? 0 : 1;
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

    // Excursion is computed over the same valid-frame mask the fit uses, and
    // printed unconditionally — a run that did not pass a bar still reports the
    // number, so the by-hand reading the docs asked for stops being by hand.
    const double excX = excursion(cx, valid);
    const double excY = excursion(cy, valid);
    const bool excursionOkX = !args.hasMaxExcursionX_ || excX <= args.maxExcursionX_;
    const bool excursionOkY = !args.hasMaxExcursionY_ || excY <= args.maxExcursionY_;

    const bool smooth = sx.reversals_ == 0 && sy.reversals_ == 0 &&
                        sx.maxAbsResidual_ <= args.maxResidual_ &&
                        sy.maxAbsResidual_ <= args.maxResidual_ && excursionOkX && excursionOkY;

    std::string thresholds = "reversals=0, max_residual<=" + formatPx(args.maxResidual_);
    if (args.hasMaxExcursionX_)
        thresholds += ", max_excursion_x<=" + formatPx(args.maxExcursionX_);
    if (args.hasMaxExcursionY_)
        thresholds += ", max_excursion_y<=" + formatPx(args.maxExcursionY_);

    std::printf(
        "jitter_probe: frames=%zu (valid=%d)  verdict=%s\n"
        "  x: reversals=%d  max_residual=%.2fpx  excursion=%.2fpx  delta_std=%.2f  "
        "delta_max=%.2f\n"
        "  y: reversals=%d  max_residual=%.2fpx  excursion=%.2fpx  delta_std=%.2f  "
        "delta_max=%.2f\n"
        "  (thresholds: %s)\n",
        n,
        validCount,
        smooth ? "SMOOTH" : "JITTER",
        sx.reversals_,
        sx.maxAbsResidual_,
        excX,
        sx.deltaStd_,
        sx.deltaMaxAbs_,
        sy.reversals_,
        sy.maxAbsResidual_,
        excY,
        sy.deltaStd_,
        sy.deltaMaxAbs_,
        thresholds.c_str()
    );
    return smooth ? 0 : 1;
}
