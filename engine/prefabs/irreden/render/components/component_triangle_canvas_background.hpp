#ifndef COMPONENT_TRIANGLE_CANVAS_BACKGROUND_H
#define COMPONENT_TRIANGLE_CANVAS_BACKGROUND_H

#include <irreden/ir_math.hpp>
#include <irreden/ir_constants.hpp>
#include <irreden/ir_time.hpp>
#include "component_triangle_canvas_textures.hpp"
#include <cmath>
#include <glm/gtc/constants.hpp>

using namespace IRMath;

namespace IRComponents {

// UNUSED FOR NOW

enum class BackgroundTypes { kSingleColor, kGradient, kGradientRandom, kPulsePattern };

struct C_TriangleCanvasBackground {
    C_TriangleCanvasBackground(BackgroundTypes type, const std::vector<Color> &colors, ivec2 size,
                               float pulseSpeed = 1.0f, int patternScale = 10)
        : m_size{size}, m_type{type}, m_colors{colors}, m_randomColorData{} {
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

    // Default
    C_TriangleCanvasBackground()
        : C_TriangleCanvasBackground{
              BackgroundTypes::kSingleColor, {IRColors::kInvisable}, ivec2{1, 1}} {
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
            m_timeSincePulseUpdate += delta;
            if (m_hasPulseFrame && m_timeSincePulseUpdate < m_minPulseUpdateSeconds) {
                return;
            }
            m_timeSincePulseUpdate = 0.0f;
            m_hasPulseFrame = true;

            const Color colorA = m_colors[0];
            const Color colorB = m_colors[1];
            const float wave = 0.5f + 0.5f * IRMath::sin(m_pulsePhase);
            const Color colorPulseA = IRMath::lerpColor(colorA, colorB, wave);
            const Color colorPulseB = IRMath::lerpColor(colorB, colorA, wave);

            if (!m_patternMaskInitialized) {
                const float invScale = 1.0f / static_cast<float>(m_patternScale);
                for (int y = 0; y < m_size.y; y++) {
                    for (int x = 0; x < m_size.x; x++) {
                        int index = index2DtoIndex1D(ivec2(x, y), m_size);
                        // Author pattern in triangle-index iso space so the trixel->framebuffer
                        // snap step preserves clean triangular structure.
                        const int isoRow =
                            static_cast<int>(IRMath::floor(static_cast<float>(x + y) * invScale));
                        const int isoCol =
                            static_cast<int>(IRMath::floor(static_cast<float>(x - y) * invScale));
                        // Alternate vertically in iso rows and stagger every other iso column.
                        const int isoColParity = ((isoCol % 2) + 2) % 2;
                        const int mask = (isoRow + isoColParity) & 1;
                        m_patternMask[index] = static_cast<uint8_t>(mask);
                    }
                }
                m_patternMaskInitialized = true;
            }

            for (int y = 0; y < m_size.y; y++) {
                for (int x = 0; x < m_size.x; x++) {
                    int index = index2DtoIndex1D(ivec2(x, y), m_size);
                    uint8_t mask = m_patternMask[index];
                    m_randomColorData[index] = (mask == 0) ? colorPulseA : colorPulseB;
                }
            }
            textures.clearWithColorData(m_size, m_randomColorData);
        }
    }

    void zoomPatternIn() {
        m_patternScale = IRMath::min(128, m_patternScale + 1);
        m_patternMaskInitialized = false;
    }

    void zoomPatternOut() {
        m_patternScale = IRMath::max(1, m_patternScale - 1);
        m_patternMaskInitialized = false;
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
    bool m_patternMaskInitialized = false;
    float m_minPulseUpdateSeconds = 1.0f / 60.0f;

    void setRandomColor(ivec2 position, Color color) {
        m_randomColorData[index2DtoIndex1D(position, m_size)] = color;
    }
};

} // namespace IRComponents

#endif /* COMPONENT_TRIANGLE_CANVAS_BACKGROUND_H */
