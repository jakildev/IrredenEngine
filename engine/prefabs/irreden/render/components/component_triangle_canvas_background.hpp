#ifndef COMPONENT_TRIANGLE_CANVAS_BACKGROUND_H
#define COMPONENT_TRIANGLE_CANVAS_BACKGROUND_H

#include <irreden/ir_math.hpp>
#include <irreden/ir_constants.hpp>
#include <irreden/ir_time.hpp>
#include <irreden/math/easing_functions.hpp>
#include "component_triangle_canvas_textures.hpp"
#include <cmath>
#include <glm/gtc/constants.hpp>

using namespace IRMath;

namespace IRComponents {


enum class BackgroundTypes { kSingleColor, kGradient, kGradientRandom, kPulsePattern };

struct C_TriangleCanvasBackground {
    struct LinearDirectionMotion {
        bool enabled_ = false;
        float timeSeconds_ = 0.0f;
        float periodSeconds_ = 1.0f;
        vec2 startDirection_ = IRMath::normalize(vec2(1.0f, 1.0f));
        vec2 endDirection_ = IRMath::normalize(vec2(1.0f, 1.0f));
        IREasingFunctions forwardEasing_ = IREasingFunctions::kLinearInterpolation;
        IREasingFunctions backwardEasing_ = IREasingFunctions::kLinearInterpolation;
    };

    C_TriangleCanvasBackground(
        BackgroundTypes type,
        const std::vector<Color> &colors,
        ivec2 size,
        float pulseSpeed = 1.0f,
        int patternScale = 10
    )
        : m_size{size}
        , m_type{type}
        , m_colors{colors}
        , m_randomColorData{} {
        m_pulseSpeed = pulseSpeed;
        m_patternScale = IRMath::max(1, patternScale);
        m_pulsePhase = 0.0f;
        m_timeSincePulseUpdate = 0.0f;
        m_hasPulseFrame = false;
        m_randomColorData.resize(size.x * size.y);
        m_patternMask.resize(size.x * size.y);
        for (auto &color : m_colors) {
            // color = colorHSVToColor(
            //     colorToColorHSV(color)
            // );
            // ColorHSV colorHSV = colorToColorHSV(color);
            // colorHSV.value_ = min(
            //     colorHSV.value_ * 0.2f,
            //     1.0f
            // );

            // colorHSV.saturation_ = min(
            //     colorHSV.saturation_ * 1.2f,
            //     1.0f
            // );
            // color = colorHSVToColor(colorHSV);
        }
    }

    C_TriangleCanvasBackground(
        BackgroundTypes type,
        const Color &colorA,
        const Color &colorB,
        ivec2 size,
        float pulseSpeed = 1.0f,
        int patternScale = 10
    )
        : C_TriangleCanvasBackground(type, std::vector<Color>{colorA, colorB}, size, pulseSpeed, patternScale
          ) {}

    // Default
    C_TriangleCanvasBackground()
        : C_TriangleCanvasBackground{
              BackgroundTypes::kSingleColor, {IRColors::kInvisable}, ivec2{1, 1}
          } {
        for (int y = 0; y < m_size.y; y++) {
            for (int x = 0; x < m_size.x; x++) {
                setRandomColor(ivec2(x, y), randomColor(m_colors));
            }
        }
    }

    void clearCanvasWithBackground(const C_TriangleCanvasTextures &textures) {
        if (m_type == BackgroundTypes::kSingleColor) {
            textures.clearWithColor(m_colors[0]);
            return;
        }

        if (m_type == BackgroundTypes::kGradient) {
            // TODO
        }

        if (m_type == BackgroundTypes::kGradientRandom) {
            // randomize a certain amount each render frame i guess
            // for now.
            int size1D = m_size.x * m_size.y;
            int randomizeAmount = size1D / 2000;
            for (int y = 0; y < randomizeAmount; y++) {
                m_randomColorData[randomInt(0, size1D - 1)] = randomColor(m_colors);
            }
            textures.clearWithColorData(m_size, m_randomColorData);
            return;
        }

        if (m_type == BackgroundTypes::kPulsePattern) {
            if (m_colors.empty()) {
                textures.clearWithColor(IRColors::kBlack);
                return;
            }

            if (m_colors.size() == 1) {
                textures.clearWithColor(m_colors[0]);
                return;
            }

            const float delta = IRTime::deltaTime(IRTime::RENDER);
            m_pulsePhase += delta * m_pulseSpeed;
            if (m_pulseWaveDirectionMotionPrimary_.enabled_) {
                m_pulseWaveDirection_ = sampleLinearDirectionMotion(
                    m_pulseWaveDirectionMotionPrimary_,
                    delta,
                    m_pulseWaveDirection_
                );
            }
            if (m_pulseWaveDirectionMotionSecondary_.enabled_) {
                m_pulseWaveDirectionSecondary_ = sampleLinearDirectionMotion(
                    m_pulseWaveDirectionMotionSecondary_,
                    delta,
                    m_pulseWaveDirectionSecondary_
                );
            }
            m_timeSincePulseUpdate += delta;
            if (m_hasPulseFrame && m_timeSincePulseUpdate < m_minPulseUpdateSeconds) {
                return;
            }
            m_timeSincePulseUpdate = 0.0f;
            m_hasPulseFrame = true;

            const Color colorA = m_colors[0];
            const Color colorB = m_colors[1];

            const float zoomMultiplierRaw = IRMath::max(0.01f, m_patternZoomMultiplier);
            // Quantize to powers-of-two to keep trixel intersections stable.
            const float zoomMultiplier = quantizePowerOfTwo(zoomMultiplierRaw);
            const float effectiveScale =
                IRMath::max(1.0f, static_cast<float>(m_patternScale) * zoomMultiplier);
            const float invScale = 1.0f / effectiveScale;
            if (zoomMultiplier != m_quantizedZoomApplied_) {
                m_quantizedZoomApplied_ = zoomMultiplier;
                m_patternMaskInitialized = false;
            }

            if (!m_patternMaskInitialized) {
                for (int y = 0; y < m_size.y; y++) {
                    for (int x = 0; x < m_size.x; x++) {
                        int index = index2DtoIndex1D(ivec2(x, y), m_size);
                        const float shiftedX = static_cast<float>(x);
                        const float shiftedY = static_cast<float>(y);
                        // Author pattern in triangle-index iso space so the trixel->framebuffer
                        // snap step preserves clean triangular structure.
                        const int isoRow =
                            static_cast<int>(IRMath::floor((shiftedX + shiftedY) * invScale));
                        const int isoCol =
                            static_cast<int>(IRMath::floor((shiftedX - shiftedY) * invScale));
                        // Alternate vertically in iso rows and stagger every other iso column.
                        const int isoColParity = ((isoCol % 2) + 2) % 2;
                        const int mask = (isoRow + isoColParity) & 1;
                        m_patternMask[index] = static_cast<uint8_t>(mask);
                    }
                }
                m_patternMaskInitialized = true;
            }

            // TODO(perf): Move pulse/interference color transform to a trixel->trixel compute pass.
            // Notes for future migration:
            // 1) Keep this CPU side responsible only for low-frequency state (pattern mask / parameters),
            //    then dispatch compute once per frame for animated color evaluation in trixel space.
            // 2) Use ping-pong textures (read from A, write to B, then swap) to avoid undefined read/write
            //    hazards on the same image in one dispatch.
            // 3) Preserve current iso-space phase projection and timing params so visual output matches:
            //    - primary/secondary directions, phase scales, speed multipliers, start offsets, mix.
            // 4) Insert proper barriers after dispatch (image/texture fetch visibility) before the
            //    framebuffer pass samples the output trixel color texture.
            // 5) Prefer one invocation per trixel texel (not per-fragment) since each trixel resolves
            //    to a uniform color in this effect.
            for (int y = 0; y < m_size.y; y++) {
                for (int x = 0; x < m_size.x; x++) {
                    int index = index2DtoIndex1D(ivec2(x, y), m_size);
                    uint8_t mask = m_patternMask[index];
                    const float shiftedX = static_cast<float>(x);
                    const float shiftedY = static_cast<float>(y);
                    // Sample waves in iso-space so motion aligns with rendered trixel orientation.
                    const vec2 isoPos(
                        (shiftedX + shiftedY) * invScale,
                        -(shiftedX - shiftedY) * invScale
                    );
                    const float phaseOffset =
                        IRMath::dot(isoPos, m_pulseWaveDirection_) * m_pulseWavePhaseScale_;
                    const float phaseOffsetSecondary =
                        IRMath::dot(isoPos, m_pulseWaveDirectionSecondary_) *
                        m_pulseWavePhaseScaleSecondary_;
                    const float primary = IRMath::sin(
                        m_pulsePhase * m_pulseWavePrimarySpeedMultiplier_ +
                        m_pulseWavePrimaryStartOffset_ + phaseOffset
                    );
                    const float secondary = IRMath::sin(
                        m_pulsePhase * m_pulseWaveSecondarySpeedMultiplier_ +
                        m_pulseWaveSecondaryStartOffset_ + phaseOffsetSecondary
                    );
                    const float combined = IRMath::mix(primary, secondary, m_pulseWaveInterferenceMix_);
                    const float wave = 0.5f + 0.5f * combined;
                    const Color colorPulseA = IRMath::lerpColor(colorA, colorB, wave);
                    const Color colorPulseB = IRMath::lerpColor(colorB, colorA, wave);
                    m_randomColorData[index] = (mask == 0) ? colorPulseA : colorPulseB;
                }
            }
            textures.clearWithColorData(m_size, m_randomColorData);
        }
    }

    void zoomPatternIn() {
        m_patternZoomMultiplier = quantizePowerOfTwo(
            IRMath::clamp(
                m_patternZoomMultiplier * 2.0f,
                IRConstants::kTrixelCanvasZoomMin.x,
                IRConstants::kTrixelCanvasZoomMax.x
            )
        );
        m_patternMaskInitialized = false;
    }

    void zoomPatternOut() {
        m_patternZoomMultiplier = quantizePowerOfTwo(
            IRMath::clamp(
                m_patternZoomMultiplier / 2.0f,
                IRConstants::kTrixelCanvasZoomMin.x,
                IRConstants::kTrixelCanvasZoomMax.x
            )
        );
        m_patternMaskInitialized = false;
    }

    void setPatternZoomMultiplier(float multiplier) {
        m_patternZoomMultiplier = quantizePowerOfTwo(
            IRMath::clamp(
                multiplier,
                IRConstants::kTrixelCanvasZoomMin.x,
                IRConstants::kTrixelCanvasZoomMax.x
            )
        );
        m_patternMaskInitialized = false;
    }

    void setPulseWaveDirection(float x, float y, float phaseScale = 1.0f) {
        const vec2 direction(x, y);
        const float length = IRMath::length(direction);
        m_pulseWaveDirection_ = (length > 0.0001f) ? (direction / length) : vec2(1.0f, 1.0f);
        m_pulseWavePhaseScale_ = IRMath::max(0.0f, phaseScale);
        m_pulseWaveDirectionMotionPrimary_.enabled_ = false;
    }

    void setPulseWavePrimaryTiming(float speedMultiplier = 1.0f, float startOffset = 0.0f) {
        m_pulseWavePrimarySpeedMultiplier_ = IRMath::max(0.0f, speedMultiplier);
        m_pulseWavePrimaryStartOffset_ = startOffset;
    }

    void setPulseWaveInterference(
        float x, float y, float phaseScale = 1.0f, float interferenceMix = 0.5f
    ) {
        const vec2 direction(x, y);
        const float length = IRMath::length(direction);
        m_pulseWaveDirectionSecondary_ =
            (length > 0.0001f) ? (direction / length) : vec2(-1.0f, 1.0f);
        m_pulseWavePhaseScaleSecondary_ = IRMath::max(0.0f, phaseScale);
        m_pulseWaveInterferenceMix_ = IRMath::clamp(interferenceMix, 0.0f, 1.0f);
        m_pulseWaveDirectionMotionSecondary_.enabled_ = false;
    }

    void setPulseWaveSecondaryTiming(float speedMultiplier = 1.0f, float startOffset = 0.0f) {
        m_pulseWaveSecondarySpeedMultiplier_ = IRMath::max(0.0f, speedMultiplier);
        m_pulseWaveSecondaryStartOffset_ = startOffset;
    }

    void setPulseWaveDirectionLinearMotion(
        float startX,
        float startY,
        float endX,
        float endY,
        float periodSeconds,
        IREasingFunctions forwardEasing = IREasingFunctions::kLinearInterpolation,
        IREasingFunctions backwardEasing = IREasingFunctions::kLinearInterpolation
    ) {
        m_pulseWaveDirectionMotionPrimary_.enabled_ = true;
        m_pulseWaveDirectionMotionPrimary_.timeSeconds_ = 0.0f;
        m_pulseWaveDirectionMotionPrimary_.periodSeconds_ = IRMath::max(0.0001f, periodSeconds);
        m_pulseWaveDirectionMotionPrimary_.forwardEasing_ = forwardEasing;
        m_pulseWaveDirectionMotionPrimary_.backwardEasing_ = backwardEasing;
        m_pulseWaveDirectionMotionPrimary_.startDirection_ =
            normalizeDirectionOrFallback(startX, startY, vec2(1.0f, 1.0f));
        m_pulseWaveDirectionMotionPrimary_.endDirection_ = normalizeDirectionOrFallback(
            endX,
            endY,
            m_pulseWaveDirectionMotionPrimary_.startDirection_
        );
    }

    void clearPulseWaveDirectionLinearMotion() {
        m_pulseWaveDirectionMotionPrimary_.enabled_ = false;
        m_pulseWaveDirectionMotionPrimary_.timeSeconds_ = 0.0f;
    }

    void setPulseWaveSecondaryDirectionLinearMotion(
        float startX,
        float startY,
        float endX,
        float endY,
        float periodSeconds,
        IREasingFunctions forwardEasing = IREasingFunctions::kLinearInterpolation,
        IREasingFunctions backwardEasing = IREasingFunctions::kLinearInterpolation
    ) {
        m_pulseWaveDirectionMotionSecondary_.enabled_ = true;
        m_pulseWaveDirectionMotionSecondary_.timeSeconds_ = 0.0f;
        m_pulseWaveDirectionMotionSecondary_.periodSeconds_ = IRMath::max(0.0001f, periodSeconds);
        m_pulseWaveDirectionMotionSecondary_.forwardEasing_ = forwardEasing;
        m_pulseWaveDirectionMotionSecondary_.backwardEasing_ = backwardEasing;
        m_pulseWaveDirectionMotionSecondary_.startDirection_ =
            normalizeDirectionOrFallback(startX, startY, vec2(-1.0f, 1.0f));
        m_pulseWaveDirectionMotionSecondary_.endDirection_ = normalizeDirectionOrFallback(
            endX,
            endY,
            m_pulseWaveDirectionMotionSecondary_.startDirection_
        );
    }

    void clearPulseWaveSecondaryDirectionLinearMotion() {
        m_pulseWaveDirectionMotionSecondary_.enabled_ = false;
        m_pulseWaveDirectionMotionSecondary_.timeSeconds_ = 0.0f;
    }

  private:
    ivec2 m_size;
    std::vector<Color> m_colors;
    std::vector<Color> m_randomColorData;
    std::vector<uint8_t> m_patternMask;
    BackgroundTypes m_type;
    float m_pulseSpeed;
    int m_patternScale;
    float m_pulsePhase;
    float m_timeSincePulseUpdate;
    bool m_hasPulseFrame;
    float m_patternZoomMultiplier = 1.0f;
    float m_quantizedZoomApplied_ = -1.0f;
    vec2 m_pulseWaveDirection_ = IRMath::normalize(vec2(1.0f, 1.0f));
    float m_pulseWavePhaseScale_ = 4.0f;
    vec2 m_pulseWaveDirectionSecondary_ = IRMath::normalize(vec2(-1.0f, 1.0f));
    float m_pulseWavePhaseScaleSecondary_ = 6.0f;
    float m_pulseWaveInterferenceMix_ = 0.5f;
    float m_pulseWavePrimarySpeedMultiplier_ = 1.0f;
    float m_pulseWavePrimaryStartOffset_ = 0.0f;
    float m_pulseWaveSecondarySpeedMultiplier_ = 1.0f;
    float m_pulseWaveSecondaryStartOffset_ = 0.0f;
    LinearDirectionMotion m_pulseWaveDirectionMotionPrimary_;
    LinearDirectionMotion m_pulseWaveDirectionMotionSecondary_;
    bool m_patternMaskInitialized = false;
    float m_minPulseUpdateSeconds = 1.0f / 60.0f;

    void setRandomColor(ivec2 position, Color color) {
        m_randomColorData[index2DtoIndex1D(position, m_size)] = color;
    }

    float quantizePowerOfTwo(float value) const {
        const float clamped = IRMath::max(0.01f, value);
        const float exponent = std::round(std::log2(clamped));
        return std::pow(2.0f, exponent);
    }

    vec2 normalizeDirectionOrFallback(float x, float y, const vec2 &fallback) const {
        const vec2 direction(x, y);
        const float length = IRMath::length(direction);
        if (length > 0.0001f) {
            return direction / length;
        }
        return fallback;
    }

    vec2 sampleLinearDirectionMotion(
        LinearDirectionMotion &motion, float dt, const vec2 &fallbackDirection
    ) {
        if (!motion.enabled_) {
            return fallbackDirection;
        }
        motion.timeSeconds_ += dt;
        const float wrapped = std::fmod(motion.timeSeconds_, motion.periodSeconds_);
        const float normalized = wrapped / motion.periodSeconds_;
        const bool forward = normalized < 0.5f;
        const float phaseT = forward ? (normalized * 2.0f) : ((normalized - 0.5f) * 2.0f);
        const IREasingFunctions easing = forward ? motion.forwardEasing_ : motion.backwardEasing_;
        const float eased = IRMath::kEasingFunctions.at(easing)(phaseT);
        const vec2 fromDir = forward ? motion.startDirection_ : motion.endDirection_;
        const vec2 toDir = forward ? motion.endDirection_ : motion.startDirection_;
        return normalizeDirectionOrFallback(
            IRMath::mix(fromDir.x, toDir.x, eased),
            IRMath::mix(fromDir.y, toDir.y, eased),
            fallbackDirection
        );
    }

};

} // namespace IRComponents

#endif /* COMPONENT_TRIANGLE_CANVAS_BACKGROUND_H */
