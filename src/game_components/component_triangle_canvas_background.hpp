/*
 * Project: Irreden Engine
 * File: \irreden-engine\src\game_components\component_triangle_canvas_background.hpp
 * Author: Evin Killian jakildev@gmail.com
 * Created Date: October 2023
 * -----
 * Modified By: <your_name> <Month> <YYYY>
 */

#include "../math/ir_math.hpp"
#include "../world/ir_constants.hpp"
#include "component_rendering_triangle_canvas_textures.hpp"

using namespace IRMath;

namespace IRComponents {

    // UNUSED FOR NOW

    enum class BackgroundTypes {
        kSingleColor,
        kGradient,
        kGradientRandom
    };

    struct C_TriangleCanvasBackground {
        C_TriangleCanvasBackground(
            BackgroundTypes type,
            const std::vector<Color>& colors,
            ivec2 size
        )
        :   m_size{size}
        ,   m_type{type}
        ,   m_colors{colors}
        ,   m_randomColorData{}
        {
            m_randomColorData.resize(size.x * size.y);
            for(auto& color : m_colors) {
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
        :   C_TriangleCanvasBackground{
                BackgroundTypes::kSingleColor,
                {IRColors::kInvisable},
                ivec2{1, 1}
            }
        {
            for(int y = 0; y < m_size.y; y++) {
                for(int x = 0; x < m_size.x; x ++) {
                    setRandomColor(ivec2(x, y), randomColor(m_colors));
                }
            }
        }

        void clearCanvasWithBackground(
            const C_TriangleCanvasTextures& textures
        )
        {
            if(m_type == BackgroundTypes::kSingleColor) {
                textures.clearWithColor(
                    m_colors[0]
                );
                return;
            }

            if(m_type == BackgroundTypes::kGradient) {
                // TODO
            }

            if(m_type == BackgroundTypes::kGradientRandom) {
                // randomize a certain amount each render frame i guess
                // for now.
                int size1D = m_size.x * m_size.y;
                int randomizeAmount = size1D / 2000;
                for(int y = 0; y < randomizeAmount; y++) {
                    m_randomColorData[
                        randomInt(0, size1D - 1)
                    ] = randomColor(m_colors);
                }
                textures.clearWithColorData(
                    m_size,
                    m_randomColorData
                );
            }
        }

    private:
        ivec2 m_size;
        std::vector<Color> m_colors;
        std::vector<Color> m_randomColorData;
        BackgroundTypes m_type;

        void setRandomColor(ivec2 position, Color color) {
            m_randomColorData[
                index2DtoIndex1D(position, m_size)
            ] = color;
        }

    };

} // namespace IRComponents


