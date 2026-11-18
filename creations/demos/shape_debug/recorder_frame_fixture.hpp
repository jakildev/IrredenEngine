#ifndef RECORDER_FRAME_FIXTURE_H
#define RECORDER_FRAME_FIXTURE_H

#include <irreden/ir_entity.hpp>
#include <irreden/ir_math.hpp>
#include <irreden/ir_profile.hpp>
#include <irreden/ir_video.hpp>
#include <irreden/common/components/component_size_triangles.hpp>
#include <irreden/render/canvas_draw.hpp>
#include <irreden/render/components/component_glitch_state.hpp>
#include <irreden/render/glitch.hpp>

#include <array>
#include <cstddef>
#include <cstdint>

// `--recorder-frame-probe`: does a recording hold each render's own picture?
//
// Every render fills the canvas with one flat colour that spells the render's
// number, and switches the framebuffer glitch pass off and on every
// `kPulseFrames` renders, so the image a recording reads alternates between
// the composite and the pass's output. Each frame the recorder hands to its
// encoder is decoded back to a number: encoded frame j must carry the number
// of the j-th recorded render, across both kinds of edge. Run with
// `--auto-record <N>`; the verdict is one `RecorderFrameProbe:` line and the
// exit status.
namespace IRShapeDebug {

struct RecorderFrameFixture {
    // Three channels of four levels each. The levels sit 64 apart, so a
    // decode survives any colour drift short of 24.
    static constexpr int kLevels = 4;
    static constexpr int kLevelStep = 64;
    static constexpr int kLevelBase = 32;
    static constexpr int kLevelTolerance = 24;
    static constexpr int kFrameModulus = kLevels * kLevels * kLevels;
    static constexpr int kPulseFrames = 4;
    static constexpr int kMaxLoggedMismatches = 8;

    // Fractions of the frame the decode reads; all must spell one number.
    static constexpr std::array<IRMath::vec2, 5> kSamplePoints{{
        {0.5f, 0.5f},
        {0.25f, 0.25f},
        {0.75f, 0.25f},
        {0.25f, 0.75f},
        {0.75f, 0.75f},
    }};

    static constexpr int level(int digit) {
        return kLevelBase + digit * kLevelStep;
    }

    static constexpr IRMath::Color pictureOf(int frame) {
        const int number = frame % kFrameModulus;
        return IRMath::Color{
            static_cast<std::uint8_t>(level(number % kLevels)),
            static_cast<std::uint8_t>(level((number / kLevels) % kLevels)),
            static_cast<std::uint8_t>(level(number / (kLevels * kLevels))),
            255
        };
    }

    // The digit a channel value spells, or -1 when it sits between levels.
    static constexpr int digitOf(int channel) {
        for (int digit = 0; digit < kLevels; ++digit) {
            const int distance = channel - level(digit);
            if (distance >= -kLevelTolerance && distance <= kLevelTolerance) {
                return digit;
            }
        }
        return -1;
    }

    // The number an RGB triple spells, or -1.
    static constexpr int numberOf(const std::uint8_t *rgba) {
        const int low = digitOf(rgba[0]);
        const int mid = digitOf(rgba[1]);
        const int high = digitOf(rgba[2]);
        if (low < 0 || mid < 0 || high < 0) {
            return -1;
        }
        return low + mid * kLevels + high * kLevels * kLevels;
    }

    static constexpr bool glitchOnAt(int frame) {
        return (frame / kPulseFrames) % 2 == 1;
    }

    // Tear only: its bands shift rows sideways, which a flat picture does not
    // show, so the pass's output spells the same number as the composite.
    static IRComponents::C_GlitchState glitchState(bool on) {
        IRComponents::C_GlitchState state;
        state.intensity_ = on ? 1.0f : 0.0f;
        state.tearWeight_ = 1.0f;
        return state;
    }

    void init(int recordFrames) {
        recordFrames_ = recordFrames;
        layer_ = IRPrefab::CanvasDraw::createLayer("recorderFrameProbe");
        IRVideo::setEncodedFrameObserver(
            [this](const std::uint8_t *rgbaData, int width, int height) {
                onEncodedFrame(rgbaData, width, height);
            }
        );
    }

    // One render's picture and glitch state. Runs ahead of the composite.
    void queueFrame() {
        const bool recording = IRVideo::recordingState() == IRVideo::RecordingState::RECORDING;
        if (wasRecording_ && !recording) {
            logVerdict();
        }
        wasRecording_ = recording;

        ++frame_;
        // The recorder counts an update tick before the render it captures,
        // so the first render to see a tick is encoded frame 0.
        if (recording && firstRecordedFrame_ < 0 && IRVideo::capturedUpdateTicks() > 0) {
            firstRecordedFrame_ = frame_;
        }
        IRPrefab::Glitch::setState(glitchState(glitchOnAt(frame_)));
        IRPrefab::CanvasDraw::queueRect(
            layer_,
            IRMath::ivec2(0),
            IREntity::getComponent<IRComponents::C_SizeTriangles>(layer_).size_,
            pictureOf(frame_),
            IRPrefab::CanvasDraw::kDepthFront
        );
    }

    void onEncodedFrame(const std::uint8_t *rgbaData, int width, int height) {
        const int index = encodedFrames_++;
        const int renderFrame = firstRecordedFrame_ + index;
        const int expected = renderFrame % kFrameModulus;

        if (index > 0) {
            const bool on = glitchOnAt(renderFrame);
            const bool wasOn = glitchOnAt(renderFrame - 1);
            offToOnEdges_ += (on && !wasOn) ? 1 : 0;
            onToOffEdges_ += (!on && wasOn) ? 1 : 0;
        }
        // Only a frame encoded in the render that drew it can be matched to
        // that render's publication.
        if (renderFrame == frame_ &&
            IRPrefab::Glitch::outputPublishedForCurrentRenderTick() != glitchOnAt(frame_)) {
            ++sourceMismatches_;
        }

        int number = -1;
        bool readable = firstRecordedFrame_ >= 0;
        for (std::size_t i = 0; readable && i < kSamplePoints.size(); ++i) {
            const int x = static_cast<int>(kSamplePoints[i].x * static_cast<float>(width));
            const int y = static_cast<int>(kSamplePoints[i].y * static_cast<float>(height));
            const std::size_t offset =
                (static_cast<std::size_t>(y) * static_cast<std::size_t>(width) +
                 static_cast<std::size_t>(x)) *
                4u;
            const int sample = numberOf(rgbaData + offset);
            readable = sample >= 0 && (i == 0 || sample == number);
            number = sample;
        }
        if (!readable) {
            ++unreadableFrames_;
        } else if (number != expected) {
            ++mismatchedFrames_;
        } else {
            return;
        }
        if (loggedMismatches_++ < kMaxLoggedMismatches) {
            IR_LOG_WARN(
                "RecorderFrameProbe: encoded frame {} should spell {} (glitch {}), reads {}",
                index,
                expected,
                glitchOnAt(renderFrame) ? "on" : "off",
                number
            );
        }
    }

    bool passed() const {
        return encodedFrames_ == recordFrames_ && recordFrames_ > 0 && mismatchedFrames_ == 0 &&
               unreadableFrames_ == 0 && sourceMismatches_ == 0 && offToOnEdges_ > 0 &&
               onToOffEdges_ > 0;
    }

    void logVerdict() {
        verdictLogged_ = true;
        IR_LOG_INFO(
            "RecorderFrameProbe: {} encoded={}/{} mismatched={} unreadable={} "
            "source_mismatched={} off_to_on={} on_to_off={}",
            passed() ? "PASS" : "FAIL",
            encodedFrames_,
            recordFrames_,
            mismatchedFrames_,
            unreadableFrames_,
            sourceMismatches_,
            offToOnEdges_,
            onToOffEdges_
        );
    }

    IREntity::EntityId layer_ = IREntity::kNullEntity;
    int recordFrames_ = 0;
    int frame_ = 0;
    int firstRecordedFrame_ = -1;
    bool wasRecording_ = false;
    bool verdictLogged_ = false;
    int encodedFrames_ = 0;
    int mismatchedFrames_ = 0;
    int unreadableFrames_ = 0;
    int sourceMismatches_ = 0;
    int offToOnEdges_ = 0;
    int onToOffEdges_ = 0;
    int loggedMismatches_ = 0;
};

} // namespace IRShapeDebug

#endif /* RECORDER_FRAME_FIXTURE_H */
