#include <gtest/gtest.h>

#include <irreden/ir_math.hpp>
#include <irreden/ir_platform.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <vector>

namespace {

// kIsoToScreenSign = vec2(1.0f, screenYDirection_) — the Y sign depends on
// the host backend (OpenGL +1 in our convention, Metal -1 or vice versa via
// IRPlatform::kGfx). The tests below derive the expected values from the
// platform sign so they pass identically on both hosts.
constexpr float kSignY = IRPlatform::kGfx.screenYDirection_;

TEST(CameraSubPixelOffsets, IntegerIsoIsZeroResidual) {
    // At integer iso, fract(camIso) is 0; both decomposition outputs must
    // be zero so the camera-anchored content lands exactly on the screen
    // center. Verified at several integer points and zoom levels.
    const std::vector<IRMath::vec2> integerCameras{
        {0.0f, 0.0f}, {1.0f, 0.0f}, {0.0f, 1.0f}, {3.0f, 5.0f}, {-2.0f, -7.0f}
    };
    const std::vector<IRMath::vec2> zooms{
        {1.0f, 1.0f}, {2.0f, 2.0f}, {4.0f, 4.0f}
    };
    const std::vector<IRMath::ivec2> scaleFactors{{1, 1}, {2, 2}, {4, 4}};

    for (const IRMath::vec2 cam : integerCameras) {
        for (const IRMath::vec2 zoom : zooms) {
            for (const IRMath::ivec2 sf : scaleFactors) {
                const auto sub = IRMath::cameraSubPixelOffsets(cam, zoom, sf);
                EXPECT_EQ(sub.framebufferGamePxOffset_.x, 0);
                EXPECT_EQ(sub.framebufferGamePxOffset_.y, 0);
                EXPECT_EQ(sub.screenPxResidual_.x, 0);
                EXPECT_EQ(sub.screenPxResidual_.y, 0);
            }
        }
    }
}

TEST(CameraSubPixelOffsets, HalfIsoXAtZoom1ScaleFactor4) {
    // X: fract(0.5) = 0.5, * zoom * 2 = 1.0 game pixels → framebuffer
    // offset = 1 game pixel (X sign positive). screen residual: subGamePx
    // = 0.0, * scaleFactor = 0 screen pixels.
    const auto sub = IRMath::cameraSubPixelOffsets(
        IRMath::vec2{0.5f, 0.0f}, IRMath::vec2{1.0f, 1.0f}, IRMath::ivec2{4, 4}
    );
    EXPECT_EQ(sub.framebufferGamePxOffset_.x, 1);
    EXPECT_EQ(sub.framebufferGamePxOffset_.y, 0);
    EXPECT_EQ(sub.screenPxResidual_.x, 0);
    EXPECT_EQ(sub.screenPxResidual_.y, 0);
}

TEST(CameraSubPixelOffsets, QuarterIsoXAtZoom1ScaleFactor4) {
    // X: fract(0.25) = 0.25, * zoom * 2 = 0.5 game pixels → framebuffer
    // offset = 0 game pixels (floor 0.5 → 0). screen residual: subGamePx
    // = 0.5, * scaleFactor 4 = 2 screen pixels.
    const auto sub = IRMath::cameraSubPixelOffsets(
        IRMath::vec2{0.25f, 0.0f}, IRMath::vec2{1.0f, 1.0f}, IRMath::ivec2{4, 4}
    );
    EXPECT_EQ(sub.framebufferGamePxOffset_.x, 0);
    EXPECT_EQ(sub.framebufferGamePxOffset_.y, 0);
    EXPECT_EQ(sub.screenPxResidual_.x, 2);
    EXPECT_EQ(sub.screenPxResidual_.y, 0);
}

TEST(CameraSubPixelOffsets, ScreenResidualBoundedByScaleFactor) {
    // The screen residual is bounded by `[-scaleFactor, scaleFactor]`
    // per component — the floor() snap guarantees the decomposition fits
    // inside one game pixel (modulo the asymmetric extra step at fract→1
    // when signY is negative, which is exactly compensated by the trixel
    // canvas re-rasterization at the next integer iso).
    const IRMath::ivec2 sf{4, 4};
    const IRMath::vec2 zoom{1.0f, 1.0f};
    for (int step = 0; step < 100; ++step) {
        const float f = static_cast<float>(step) / 100.0f;
        const auto sub = IRMath::cameraSubPixelOffsets({f, f}, zoom, sf);
        EXPECT_GE(sub.screenPxResidual_.x, -sf.x);
        EXPECT_LE(sub.screenPxResidual_.x, sf.x);
        EXPECT_GE(sub.screenPxResidual_.y, -sf.y);
        EXPECT_LE(sub.screenPxResidual_.y, sf.y);
    }
}

TEST(CameraSubPixelOffsets, MonotoneAsCameraAdvances) {
    // As `cameraIso` advances smoothly across one iso pixel along X, the
    // combined screen position (gamePxOffset * scaleFactor + screenPxResidual)
    // must be non-decreasing — this is the anti-vibration property the
    // helper exists to guarantee. A non-monotone step would produce
    // visible +/-1px jitter on a smoothly-moving camera. X uses the
    // unsigned-positive direction (`IRPlatform::kIsoToScreenSign.x == 1`)
    // so the combined value is non-decreasing; the Y direction's signed
    // case is covered by `MatchesLegacyDecomposition`.
    const IRMath::ivec2 sf{4, 4};
    const IRMath::vec2 zoom{1.0f, 1.0f};
    // Strictly less than 1.0: fract(1.0) wraps to 0 (the next iso pixel
    // step), where the trixel canvas re-rasterizes — the combined output
    // wraps with it and is expected to drop back to 0.
    int prevX = std::numeric_limits<int>::min();
    for (int step = 0; step < 200; ++step) {
        const float f = static_cast<float>(step) / 200.0f;
        const auto sub = IRMath::cameraSubPixelOffsets({f, 0.0f}, zoom, sf);
        const int combinedX = sub.framebufferGamePxOffset_.x * sf.x + sub.screenPxResidual_.x;
        EXPECT_GE(combinedX, prevX)
            << "non-monotone at fract=" << f << " combined=" << combinedX << " prev=" << prevX;
        prevX = combinedX;
    }
}

TEST(CameraSubPixelOffsets, MatchesLegacyDecomposition) {
    // Byte-for-byte parity against the pre-refactor inline math that lived
    // in TRIXEL_TO_FRAMEBUFFER + FRAMEBUFFER_TO_SCREEN. Sampled across
    // sub-iso positions at two zoom levels and three scale factors.
    const std::vector<IRMath::vec2> cameraSamples{
        {0.1f, 0.2f}, {0.3f, 0.4f}, {0.5f, 0.5f}, {0.7f, 0.9f}, {0.99f, 0.01f}
    };
    const std::vector<IRMath::vec2> zooms{{1.0f, 1.0f}, {2.0f, 2.0f}};
    const std::vector<IRMath::ivec2> scaleFactors{{1, 1}, {3, 3}, {5, 5}};
    const IRMath::vec2 sign = IRPlatform::kIsoToScreenSign;

    for (const IRMath::vec2 cam : cameraSamples) {
        for (const IRMath::vec2 zoom : zooms) {
            for (const IRMath::ivec2 sf : scaleFactors) {
                const auto sub = IRMath::cameraSubPixelOffsets(cam, zoom, sf);
                const IRMath::vec2 legacyGame = IRMath::floor(
                    IRMath::pos2DIsoToPos2DGameResolution(IRMath::fract(cam), zoom)
                ) * sign;
                const IRMath::vec2 legacyScreen = IRMath::floor(
                    IRMath::fract(IRMath::pos2DIsoToPos2DGameResolution(
                        IRMath::fract(cam), zoom
                    )) * sign * IRMath::vec2(sf)
                );
                EXPECT_EQ(sub.framebufferGamePxOffset_.x, static_cast<int>(legacyGame.x));
                EXPECT_EQ(sub.framebufferGamePxOffset_.y, static_cast<int>(legacyGame.y));
                EXPECT_EQ(sub.screenPxResidual_.x, static_cast<int>(legacyScreen.x));
                EXPECT_EQ(sub.screenPxResidual_.y, static_cast<int>(legacyScreen.y));
            }
        }
    }
    (void)kSignY;
}

// ---------------------------------------------------------------------------
// Triangle step
// ---------------------------------------------------------------------------

TEST(TriangleStepSize, FractionalZoomKeepsItsFraction) {
    const IRMath::vec2 gameStep = IRMath::calcTriangleStepSizeGameResolution(IRMath::vec2(2.5f));
    EXPECT_EQ(gameStep, IRMath::vec2(5.0f, 2.5f));
    const IRMath::vec2 screenStep =
        IRMath::calcTriangleStepSizeScreen(IRMath::vec2(2.5f), IRMath::ivec2(3, 3));
    EXPECT_EQ(screenStep, IRMath::vec2(15.0f, 7.5f));
}

TEST(TriangleStepSize, PowerOfTwoZoomIsWholePixels) {
    for (const float zoom : {1.0f, 2.0f, 4.0f, 8.0f, 16.0f, 64.0f}) {
        EXPECT_EQ(
            IRMath::calcTriangleStepSizeGameResolution(IRMath::vec2(zoom)),
            IRMath::vec2(2.0f * zoom, zoom)
        );
        EXPECT_EQ(
            IRMath::calcTriangleStepSizeScreen(IRMath::vec2(zoom), IRMath::ivec2(2, 2)),
            IRMath::vec2(4.0f * zoom, 2.0f * zoom)
        );
    }
}

// ---------------------------------------------------------------------------
// Continuous-zoom raster phase
// ---------------------------------------------------------------------------

// One rendered frame of a sweep: where the camera is and what it is zoomed to.
struct RasterFrame {
    IRMath::vec2 cameraIso_;
    float zoom_;
};

// Both placement terms for one axis of one frame, signed the way the helpers
// return them.
struct AxisSplit {
    double gatherTranslation_;
    int screenResidual_;
};

// Backing texels per iso unit at @p zoom with one base subdivision under the
// continuous policy's round-up rule.
int continuousDensity(float zoom) {
    return static_cast<int>(std::ceil(zoom));
}

IRMath::CameraRasterPhase freshSample(const RasterFrame &frame) {
    return IRMath::CameraRasterPhase{
        IRMath::dvec2(frame.cameraIso_),
        IRMath::cameraZoomPitch(IRMath::vec2(frame.zoom_)),
        IRMath::dvec2(0.0)
    };
}

// The samples a run of frames publishes: the first is fresh, each later one
// advances the phase from its predecessor.
std::vector<IRMath::CameraRasterPhase> carriedSamples(const std::vector<RasterFrame> &frames) {
    std::vector<IRMath::CameraRasterPhase> samples;
    samples.reserve(frames.size());
    for (const RasterFrame &frame : frames) {
        samples.push_back(
            samples.empty()
                ? freshSample(frame)
                : IRMath::advanceCameraRasterPhase(
                      samples.back(),
                      frame.cameraIso_,
                      IRMath::vec2(frame.zoom_)
                  )
        );
    }
    return samples;
}

AxisSplit continuousSplit(
    const IRMath::CameraRasterPhase &sample, const RasterFrame &frame, int axis, int scale
) {
    const int density = continuousDensity(frame.zoom_);
    const IRMath::dvec2 gather = IRMath::cameraRasterGatherTranslation(
        sample,
        frame.cameraIso_ * static_cast<float>(density),
        IRMath::vec2(0.0f),
        density
    );
    const IRMath::ivec2 residual =
        IRMath::cameraRasterScreenResidual(sample, IRMath::ivec2(scale));
    return AxisSplit{gather[axis], residual[axis]};
}

// The snapped-zoom split evaluated at a fractional pitch: what the composite
// does with no continuous placement.
AxisSplit densityScaledSplit(const RasterFrame &frame, int axis, int scale) {
    const float density = static_cast<float>(continuousDensity(frame.zoom_));
    const IRMath::CameraSubPixelOffsets gather = IRMath::cameraSubPixelOffsets(
        frame.cameraIso_ * density,
        IRMath::vec2(frame.zoom_ / density),
        IRMath::ivec2(1)
    );
    const IRMath::CameraSubPixelOffsets residual = IRMath::cameraSubPixelOffsets(
        frame.cameraIso_,
        IRMath::vec2(frame.zoom_),
        IRMath::ivec2(scale)
    );
    return AxisSplit{
        static_cast<double>(gather.framebufferGamePxOffset_[axis]),
        residual.screenPxResidual_[axis]
    };
}

// A sample whose edge sits this close to a framebuffer pixel centre is a
// raster tie: which pixel it lands in is decided by the last bit of the
// arithmetic, so it says nothing about direction.
constexpr double kRasterTieBand = 1e-9;

struct EdgeSweepResult {
    long long maxBackwardPx_ = 0;
    long long firedSamples_ = 0;
    long long tieSamples_ = 0;
};

// Largest backward step, in screen pixels, of any tracked edge across
// @p frames. Edges are world-fixed, at @p edgesPerCell positions per iso cell
// (every backing-texel edge when that equals the density), in the cells that
// stay ahead of the view centre, so every frame of every sweep here moves
// each of them forward. An edge at framebuffer position `u` is drawn at pixel
// `round(u)` and then at screen pixel `round(u) * scale + residual`.
template <typename SplitFn>
EdgeSweepResult sweepEdges(
    const std::vector<RasterFrame> &frames, int axis, int scale, int edgesPerCell, SplitFn &&split
) {
    const double sign = static_cast<double>(IRPlatform::kIsoToScreenSign[axis]);
    const double firstCamera = static_cast<double>(frames.front().cameraIso_[axis]);
    const int firstEdge = static_cast<int>(std::ceil((0.25 - firstCamera) * edgesPerCell));
    const int lastEdge = static_cast<int>(std::floor((6.0 - firstCamera) * edgesPerCell));

    EdgeSweepResult result;
    for (int edge = firstEdge; edge <= lastEdge; ++edge) {
        const double edgeIso = static_cast<double>(edge) / edgesPerCell;
        bool hasPrevious = false;
        long long previousScreenPx = 0;
        for (std::size_t i = 0; i < frames.size(); ++i) {
            const RasterFrame &frame = frames[i];
            const int density = continuousDensity(frame.zoom_);
            const double texelPitch =
                IRMath::cameraZoomPitch(IRMath::vec2(frame.zoom_))[axis] / density;
            const double rasterTexels = static_cast<double>(
                std::floor(frame.cameraIso_[axis] * static_cast<float>(density))
            );
            const AxisSplit placement = split(i, frame, axis, scale);
            const double framebufferPx = (edgeIso * density + rasterTexels) * texelPitch +
                                         placement.gatherTranslation_ * sign;
            const double pixelPhase = framebufferPx - std::floor(framebufferPx);
            if (std::abs(pixelPhase - 0.5) < kRasterTieBand) {
                ++result.tieSamples_;
                continue;
            }
            const long long screenPx =
                static_cast<long long>(std::floor(framebufferPx + 0.5)) * scale +
                static_cast<long long>(sign) * placement.screenResidual_;
            ++result.firedSamples_;
            if (hasPrevious) {
                result.maxBackwardPx_ =
                    std::max(result.maxBackwardPx_, previousScreenPx - screenPx);
            }
            previousScreenPx = screenPx;
            hasPrevious = true;
        }
    }
    return result;
}

std::vector<RasterFrame> panFrames(float zoom) {
    constexpr int kSteps = 6000;
    std::vector<RasterFrame> frames;
    frames.reserve(kSteps + 1);
    for (int i = 0; i <= kSteps; ++i) {
        const float camera = 16.0f + 3.0f * static_cast<float>(i) / kSteps;
        frames.push_back(RasterFrame{IRMath::vec2(camera, camera), zoom});
    }
    return frames;
}

std::vector<RasterFrame> zoomFrames(float camera, float zoomFrom, float zoomTo, int steps) {
    std::vector<RasterFrame> frames;
    frames.reserve(steps + 1);
    for (int i = 0; i <= steps; ++i) {
        const float zoom = zoomFrom + (zoomTo - zoomFrom) * static_cast<float>(i) / steps;
        frames.push_back(RasterFrame{IRMath::vec2(camera, camera), zoom});
    }
    return frames;
}

std::vector<RasterFrame> panAndZoomFrames() {
    constexpr int kSteps = 240;
    std::vector<RasterFrame> frames;
    frames.reserve(kSteps + 1);
    for (int i = 0; i <= kSteps; ++i) {
        const float t = static_cast<float>(i) / kSteps;
        const float camera = 17.3f + 3.0f * t;
        frames.push_back(RasterFrame{IRMath::vec2(camera, camera), 2.05f + 0.9f * t});
    }
    return frames;
}

// Every backing-texel edge when the density holds for the whole sweep; whole
// cells when it does not, since those are the only edges every density shares.
int edgesPerCellFor(const std::vector<RasterFrame> &frames) {
    const int density = continuousDensity(frames.front().zoom_);
    for (const RasterFrame &frame : frames) {
        if (continuousDensity(frame.zoom_) != density) {
            return 1;
        }
    }
    return density;
}

EdgeSweepResult sweepCarried(const std::vector<RasterFrame> &frames, int axis, int scale) {
    const std::vector<IRMath::CameraRasterPhase> samples = carriedSamples(frames);
    return sweepEdges(
        frames,
        axis,
        scale,
        edgesPerCellFor(frames),
        [&samples](std::size_t index, const RasterFrame &frame, int splitAxis, int splitScale) {
            return continuousSplit(samples[index], frame, splitAxis, splitScale);
        }
    );
}

// A fresh sample every frame: the continuous split with its phase never
// carried.
EdgeSweepResult sweepZeroPhase(const std::vector<RasterFrame> &frames, int axis, int scale) {
    return sweepEdges(
        frames,
        axis,
        scale,
        edgesPerCellFor(frames),
        [](std::size_t, const RasterFrame &frame, int splitAxis, int splitScale) {
            return continuousSplit(freshSample(frame), frame, splitAxis, splitScale);
        }
    );
}

EdgeSweepResult sweepDensityScaled(const std::vector<RasterFrame> &frames, int axis, int scale) {
    return sweepEdges(
        frames,
        axis,
        scale,
        edgesPerCellFor(frames),
        [](std::size_t, const RasterFrame &frame, int splitAxis, int splitScale) {
            return densityScaledSplit(frame, splitAxis, splitScale);
        }
    );
}

constexpr float kFractionalZooms[] = {2.5f, 2.7f, 3.3f};
constexpr int kOutputScales[] = {1, 2, 3};
constexpr float kOffOriginCamera = 17.3f;

TEST(CameraSubPixelRasterPhase, PanNeverStepsAnEdgeBackward) {
    for (const float zoom : kFractionalZooms) {
        const std::vector<RasterFrame> frames = panFrames(zoom);
        for (int axis = 0; axis < 2; ++axis) {
            for (const int scale : kOutputScales) {
                const EdgeSweepResult result = sweepCarried(frames, axis, scale);
                EXPECT_EQ(result.maxBackwardPx_, 0)
                    << "zoom=" << zoom << " axis=" << axis << " scale=" << scale;
                EXPECT_GT(result.firedSamples_, 1000);
            }
        }
    }
}

TEST(CameraSubPixelRasterPhase, ZoomAtAnOffOriginCameraNeverStepsAnEdgeBackward) {
    const std::vector<std::vector<RasterFrame>> sweeps{
        zoomFrames(kOffOriginCamera, 2.05f, 2.95f, 240),
        zoomFrames(kOffOriginCamera, 2.0f, 4.0f, 240),
        zoomFrames(16.0f, 2.5f, 2.6f, 20),
        zoomFrames(16.4f, 2.5f, 2.6f, 20),
    };
    for (std::size_t sweep = 0; sweep < sweeps.size(); ++sweep) {
        for (int axis = 0; axis < 2; ++axis) {
            for (const int scale : kOutputScales) {
                const EdgeSweepResult result = sweepCarried(sweeps[sweep], axis, scale);
                EXPECT_EQ(result.maxBackwardPx_, 0)
                    << "sweep=" << sweep << " axis=" << axis << " scale=" << scale;
                EXPECT_GT(result.firedSamples_, 100);
            }
        }
    }
}

TEST(CameraSubPixelRasterPhase, PanAndZoomTogetherNeverStepAnEdgeBackward) {
    const std::vector<RasterFrame> frames = panAndZoomFrames();
    for (int axis = 0; axis < 2; ++axis) {
        for (const int scale : kOutputScales) {
            const EdgeSweepResult result = sweepCarried(frames, axis, scale);
            EXPECT_EQ(result.maxBackwardPx_, 0) << "axis=" << axis << " scale=" << scale;
            EXPECT_GT(result.firedSamples_, 1000);
        }
    }
}

// Negative control for the zoom sweeps: with the phase reset every frame, the
// rounding phase of the framebuffer grid sweeps as the pitch changes and every
// edge re-rounds against it. A sweep that reads zero here could not have seen
// the defect the carried phase removes.
TEST(CameraSubPixelRasterPhase, ZeroPhaseSplitStepsBackwardDuringAnOffOriginZoom) {
    const std::vector<RasterFrame> frames = zoomFrames(kOffOriginCamera, 2.05f, 2.95f, 240);
    for (int axis = 0; axis < 2; ++axis) {
        for (const int scale : kOutputScales) {
            const EdgeSweepResult result = sweepZeroPhase(frames, axis, scale);
            EXPECT_GT(result.maxBackwardPx_, 0) << "axis=" << axis << " scale=" << scale;
            EXPECT_GT(result.firedSamples_, 1000);
        }
    }
}

// The same split is exact with the camera on the world iso origin, which is
// why the zoom sweeps above hold the camera away from it.
TEST(CameraSubPixelRasterPhase, ZeroPhaseSplitIsCleanWithTheCameraOnTheOrigin) {
    const std::vector<RasterFrame> frames = zoomFrames(0.0f, 2.05f, 2.95f, 240);
    for (const int scale : kOutputScales) {
        EXPECT_EQ(sweepZeroPhase(frames, 0, scale).maxBackwardPx_, 0) << "scale=" << scale;
    }
}

// Negative control for the pan sweep: the snapped-zoom split assumes a backing
// texel spans whole framebuffer pixels. At a fractional pitch its two floors
// disagree, and the upscale residual exposes it at any output scale above 1.
TEST(CameraSubPixelRasterPhase, DensityScaledSplitStepsBackwardDuringAFractionalPan) {
    for (const int scale : {2, 3}) {
        long long maxBackwardPx = 0;
        for (const float zoom : kFractionalZooms) {
            const std::vector<RasterFrame> frames = panFrames(zoom);
            for (int axis = 0; axis < 2; ++axis) {
                maxBackwardPx = std::max(
                    maxBackwardPx,
                    sweepDensityScaled(frames, axis, scale).maxBackwardPx_
                );
            }
        }
        EXPECT_GT(maxBackwardPx, 0) << "scale=" << scale;
    }
}

// The continuous split is the snapped split wherever the snapped one is exact:
// a power-of-two zoom at one base subdivision (so a backing texel is a whole
// 2x1 framebuffer pixels) and a phase that has not been carried anywhere.
TEST(CameraSubPixelRasterPhase, FreshPhaseMatchesTheSnappedSplitAtPowerOfTwoZoom) {
    const std::vector<IRMath::vec2> cameras{
        {17.3f, 4.6f}, {-4.7f, -0.83f}, {0.37f, -12.125f}, {123.456f, -77.7f}
    };
    for (const float zoom : {1.0f, 2.0f, 4.0f, 8.0f}) {
        const int density = static_cast<int>(zoom);
        for (const IRMath::vec2 camera : cameras) {
            const RasterFrame frame{camera, zoom};
            const IRMath::CameraRasterPhase sample = freshSample(frame);
            const IRMath::vec2 rasterCamera = camera * static_cast<float>(density);

            const IRMath::dvec2 gather = IRMath::cameraRasterGatherTranslation(
                sample,
                rasterCamera,
                IRMath::vec2(0.0f),
                density
            );
            const IRMath::CameraSubPixelOffsets snappedGather = IRMath::cameraSubPixelOffsets(
                rasterCamera,
                IRMath::vec2(zoom / static_cast<float>(density)),
                IRMath::ivec2(1)
            );
            EXPECT_EQ(gather, IRMath::dvec2(snappedGather.framebufferGamePxOffset_))
                << "zoom=" << zoom << " camera=(" << camera.x << "," << camera.y << ")";

            for (const int scale : {1, 2, 3, 4}) {
                EXPECT_EQ(
                    IRMath::cameraRasterScreenResidual(sample, IRMath::ivec2(scale)),
                    IRMath::cameraSubPixelOffsets(camera, IRMath::vec2(zoom), IRMath::ivec2(scale))
                        .screenPxResidual_
                ) << "zoom=" << zoom << " scale=" << scale;
            }

            const IRMath::vec2 pitch = IRMath::vec2(IRMath::cameraZoomPitch(IRMath::vec2(zoom)));
            EXPECT_EQ(
                IRMath::cameraRasterDetachedOffset(sample),
                IRMath::dvec2(
                    IRMath::floor(IRMath::fract(camera) * pitch) * IRMath::vec2(1.0f, -1.0f)
                )
            ) << "zoom=" << zoom;
        }
    }
}

// The world canvas and a detached canvas place from the same sample: the
// raster's whole texels plus the gather translation, and the detached term
// plus its whole cells, are the same framebuffer offset, and that offset is
// within one framebuffer pixel below the raw camera offset.
TEST(CameraSubPixelRasterPhase, MainAndDetachedPlacementShareOneOffset) {
    const std::vector<RasterFrame> frames = panAndZoomFrames();
    const std::vector<IRMath::CameraRasterPhase> samples = carriedSamples(frames);
    for (std::size_t i = 0; i < frames.size(); ++i) {
        const RasterFrame &frame = frames[i];
        const IRMath::CameraRasterPhase &sample = samples[i];
        const int density = continuousDensity(frame.zoom_);
        const IRMath::vec2 rasterCamera = frame.cameraIso_ * static_cast<float>(density);
        const IRMath::dvec2 sign = IRMath::dvec2(IRPlatform::kIsoToScreenSign);

        const IRMath::dvec2 mainOffset =
            IRMath::dvec2(IRMath::floor(rasterCamera)) * sample.pitch_ /
                static_cast<double>(density) +
            IRMath::cameraRasterGatherTranslation(sample, rasterCamera, IRMath::vec2(0.0f), density) *
                sign;
        const IRMath::dvec2 detachedOffset =
            IRMath::floor(sample.cameraIso_) * sample.pitch_ +
            IRMath::cameraRasterDetachedOffset(sample) * IRMath::dvec2(1.0, -1.0);
        const IRMath::dvec2 rawOffset = sample.cameraIso_ * sample.pitch_;

        for (int axis = 0; axis < 2; ++axis) {
            EXPECT_NEAR(mainOffset[axis], detachedOffset[axis], 1e-9) << "frame=" << i;
            EXPECT_LE(detachedOffset[axis], rawOffset[axis] + 1e-9) << "frame=" << i;
            EXPECT_GT(detachedOffset[axis], rawOffset[axis] - 1.0 - 1e-9) << "frame=" << i;
            EXPECT_GE(sample.phase_[axis], 0.0);
            EXPECT_LT(sample.phase_[axis], 1.0);
        }
    }
}

// A zoom excursion that returns to a power of two leaves a carried phase
// behind, so the placement there is offset from the snapped split by a
// uniform sub-pixel amount. The pan that follows is still rigid.
TEST(CameraSubPixelRasterPhase, CarriedPhaseSurvivesAnExcursionAndStaysRigid) {
    std::vector<RasterFrame> frames;
    for (const RasterFrame &frame : zoomFrames(kOffOriginCamera, 4.0f, 2.7f, 60)) {
        frames.push_back(frame);
    }
    for (int i = 1; i <= 60; ++i) {
        const float camera = kOffOriginCamera + 1.37f * static_cast<float>(i) / 60.0f;
        frames.push_back(RasterFrame{IRMath::vec2(camera, camera), 2.7f});
    }
    const float pannedCamera = frames.back().cameraIso_.x;
    for (const RasterFrame &frame : zoomFrames(pannedCamera, 2.7f, 4.0f, 60)) {
        frames.push_back(frame);
    }
    const IRMath::CameraRasterPhase returned = carriedSamples(frames).back();
    EXPECT_TRUE(returned.phase_.x != 0.0 || returned.phase_.y != 0.0);

    std::vector<RasterFrame> pan;
    for (int i = 0; i <= 2000; ++i) {
        const float camera = pannedCamera + 2.0f * static_cast<float>(i) / 2000.0f;
        pan.push_back(RasterFrame{IRMath::vec2(camera, camera), 4.0f});
    }
    std::vector<IRMath::CameraRasterPhase> samples;
    samples.reserve(pan.size());
    IRMath::CameraRasterPhase previous = returned;
    for (const RasterFrame &frame : pan) {
        previous = IRMath::advanceCameraRasterPhase(
            previous,
            frame.cameraIso_,
            IRMath::vec2(frame.zoom_)
        );
        samples.push_back(previous);
    }
    for (int axis = 0; axis < 2; ++axis) {
        const EdgeSweepResult result = sweepEdges(
            pan,
            axis,
            3,
            4,
            [&samples](std::size_t index, const RasterFrame &frame, int splitAxis, int scale) {
                return continuousSplit(samples[index], frame, splitAxis, scale);
            }
        );
        EXPECT_EQ(result.maxBackwardPx_, 0) << "axis=" << axis;
        EXPECT_GT(result.firedSamples_, 1000);
    }
}

} // namespace
