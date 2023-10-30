/*
 * Project: Irreden Engine
 * File: \irreden-engine\engine\src\math\ir_math.hpp
 * Author: Evin Killian jakildev@gmail.com
 * Created Date: October 2023
 * -----
 * Modified By: <your_name> <Month> <YYYY>
 */

#ifndef IR_MATH_H
#define IR_MATH_H

#include <glm/common.hpp>
#include <glm/vec2.hpp>
#include <glm/mat2x2.hpp>
#include <glm/vector_relational.hpp>
#include <glm/gtx/color_space.hpp>
#include <vector>

// TODO: The game engine needs transformations for voxel sets
// rotation in 3D space

namespace IRMath {

    using vec2 = glm::vec2;
    using vec3 = glm::vec3;
    using vec4 = glm::vec4;

    using dvec2 = glm::dvec2;
    using dvec3 = glm::dvec3;
    using dvec4 = glm::dvec4;

    using ivec1 = glm::ivec1;
    using ivec2 = glm::ivec2;
    using ivec3 = glm::ivec3;
    using ivec4 = glm::ivec4;

    using uvec2 = glm::uvec2;
    using uvec3 = glm::uvec3;
    using uvec4 = glm::uvec4;

    using u8vec2 = glm::u8vec2;
    using u8vec3 = glm::u8vec3;
    using u8vec4 = glm::u8vec4;

    using u32vec1 = glm::u32vec1;
    using u32vec2 = glm::u32vec2;

    using mat2 = glm::mat2;
    using mat3 = glm::mat3;
    using mat4 = glm::mat4;

    using Distance = int32_t;

    enum FaceType {
        NONE_FACE,
        X_FACE,
        Y_FACE,
        Z_FACE
    };

    // const std::unordered_map<FaceType, ivec3> kFaceCameraRotations {
    //     {NONE_FACE, ivec3(0, 0, 0)},
    //     {X_FACE, }
    // }

    enum Faces {
        X1,
        X2,
        Y1,
        Y2,
        Z1,
        Z2
    };

    enum class CoordinateAxis {
        XAxis,
        YAxis,
        ZAzis
    };


    enum Shape2D {
        RECTANGLE,
        TRIANGLE,
        CIRCLE,
        TRAPEZOID,
        NONE_SHAPE_2D
    };

    enum class Shape3D {
        RECTANGULAR_PRISM,
        SPHERE,
        NONE_SHAPE_3D
    };

    struct ColorHSV {
        float hue_;
        float saturation_;
        float value_;
        float alpha_;
    };

    struct Color {
        uint8_t red_;
        uint8_t green_;
        uint8_t blue_;
        uint8_t alpha_;
    };

    namespace IRColors {
        constexpr Color kInvisable = Color{0x00, 0x00, 0x00, 0x00};
        constexpr Color kBlack = Color{0x00, 0x00, 0x00, 0xFF};
        constexpr Color kWhite = Color{0xFF, 0xFF, 0xFF, 0xFF};
        constexpr Color kRed = Color{0xFF, 0x00, 0x00, 0xFF};
        constexpr Color kGreen = Color{0x00, 0xFF, 0x00, 0xFF};
        constexpr Color kBlue = Color{0x00, 0x00, 0xFF, 0xFF};
        constexpr Color kYellow = Color{0xFF, 0xFF, 0x00, 0xFF};
    }

    std::vector<Color> createColorPaletteFromFile(const char* filename);

    const bool randomBool();
    const int randomInt(const int min, const int max);
    const float randomFloat(const float min, const float max);
    const Color randomColor();
    const Color randomColor(const std::vector<Color>& colorPalette);


    const vec3 randomVec(const vec3 min, const vec3 max);

    constexpr int round(float value) {
        return glm::round(value);
    }

    constexpr ivec2 roundVec(vec2 value) {
        return ivec2(round(value.x), round(value.y));
    }

    template <typename VecType>
    constexpr VecType max(const VecType& vector1, const VecType& vector2) {
        return glm::max(vector1, vector2);
    }

    template <typename VecType>
    constexpr VecType normalize(const VecType& vector) {
        return glm::normalize(vector);
    }

    template <typename VecType>
    constexpr VecType length(const VecType& vector) {
        return glm::length(vector);
    }

    template <typename T>
    constexpr T min(const T& value1, const T& value2) {
        return glm::min(value1, value2);
    }

    constexpr int sumVecComponents(const ivec2 value) {
        return value.x + value.y;
    }

    constexpr int sumVecComponents(const ivec3 value) {
        return value.x + value.y + value.z;
    }

    constexpr int sumVecComponents(const vec3 value) {
        return value.x + value.y + value.z;
    }

    constexpr int sumVecComponents(const vec2 value) {
        return value.x + value.y;
    }

    constexpr int multVecComponents(const ivec3 value) {
        return value.x * value.y * value.z;
    }

    constexpr ivec2 size3DtoOriginOffset2DX1(const uvec3 size) {
        return ivec2(
            size.x,
            size.x + size.y - 1
        );
    }
    constexpr ivec2 size3DtoOriginOffset2DY1(const uvec3 size) {
        return size3DtoOriginOffset2DX1(size) - ivec2(1, 0);
    }
    constexpr ivec2 size3DtoOriginOffset2DZ1(const uvec3 size) {
        return size3DtoOriginOffset2DX1(size) - ivec2(1, 1);
    }

    constexpr ivec2 pos3DtoPos2DIso(const ivec3 position) {
        return ivec2(
            - position.x + position.y,
            - position.x - position.y + (2 * position.z)
        );
    }

    constexpr vec2 pos3DtoPos2DIso(const vec3 position) {
        return vec2(
            - position.x + position.y,
            - position.x - position.y + (2 * position.z)
        );
    }

    constexpr vec2 pos3DtoPos2DScreen(
        const vec3 position,
        const vec2 triangleStepSizeScreen
    )
    {
        return pos3DtoPos2DIso(position) *
            triangleStepSizeScreen *
            vec2(-1.0f);
    }

    // constexpr vec2 pos3DtoPos2DScreenOffset(const vec3 position) {
    //     return pos3DtoPos2DIso(position) ...
    // }

    // all positions are from screen center (0, 0, 0)
    constexpr vec2 pos2DScreenToPos2DIso(
        const vec2 screenPos,
        const vec2 triangleStepSizeScreen
    )
    {
        return screenPos / triangleStepSizeScreen;
    }

    constexpr vec2 offsetScreenToIsoTriangles(
        const vec2 offsetScreen,
        const vec2 triangleStepSizeScreen
    )
    {
        return offsetScreen / triangleStepSizeScreen;
    }

    // constexpr vec2 pos3DtoPos2DScreen(
    //     const vec3 position,
    //     const vec2 triangleStepSize
    // )
    // {

    // }

    vec2 symmetricRound(const vec2& input);

    constexpr Distance pos3DtoDistance(const ivec3 position) {
        return sumVecComponents(position);
    }
    constexpr Distance pos3DtoDistance(const vec3 position) {
        return round(sumVecComponents(position));
    }

    template <ivec3 size>
    constexpr FaceType calcFaceTypeFromTriangleIndexAndSize(
        const ivec2 index
    )
    {
        ivec2 origin = size3DtoOriginOffset2DX1(size);
        ivec2 offsetPosition = index - origin;

        if(
            offsetPosition.x >= 0 &&
            offsetPosition.x >= -offsetPosition.y &&
            (offsetPosition.x + offsetPosition.y) < size.z * 2
        )
        {
            return FaceType::X_FACE;
        }

        if(
            offsetPosition.x < 0 &&
            offsetPosition.x < offsetPosition.y &&
            (offsetPosition.y - offsetPosition.x) <= size.z * 2
        )
        {
            return FaceType::Y_FACE;
        }


        if(
            (
                ((index.x >= size.x) && ((offsetPosition.x + offsetPosition.y) < 0)) ||
                ((index.x < size.x) && ((- offsetPosition.x + offsetPosition.y) <= 0))
            )
            &&
            (
                ((index.x >= size.y) && ((index.x - index.y) < size.y)) ||
                ((index.x < size.y) && ((index.x + index.y) >= size.y))
            )
        )
        {
            return FaceType::Z_FACE;
        }

        return FaceType::NONE_FACE;
    }

    template <uvec3 size>
    constexpr ivec3 pos2DIsoToPos3DRectSurface(const ivec2 position) {
        ivec2 origin = size3DtoOriginOffset2DX1(size);
        ivec2 positionFromOrigin = position - origin;
        FaceType faceType = calcFaceTypeFromTriangleIndexAndSize<size>(position);

        if(faceType == FaceType::X_FACE) {
            return ivec3(
                0,
                positionFromOrigin.x,
                (positionFromOrigin.x + positionFromOrigin.y) / 2
            );
        }

        if(faceType == FaceType::Y_FACE) {
            return ivec3(
                - positionFromOrigin.x - 1,
                0,
                (- positionFromOrigin.x - 1 + positionFromOrigin.y) / 2
            );
        }

        if(faceType == FaceType::Z_FACE) {
            return ivec3(
                - (positionFromOrigin.x + positionFromOrigin.y + 1) / 2,
                (positionFromOrigin.x - positionFromOrigin.y) / 2,
                0
            );
        }

        return ivec3(-1, -1, -1);
    }
    // Selects from bottom Z face
    template <ivec3 size>
    constexpr ivec3 pos2DIsoToPos3DAtZLevel(
        const ivec2 position,
        const int zLevel
    )
    {
        // Origin set to lower right Z face at correct Z level
        ivec2 origin =
            size3DtoOriginOffset2DX1(size) +
            ivec2(-1, 0) +
            ivec2(0, zLevel * 2);
        ivec2 positionFromOrigin = position - origin;
        return ivec3(
            glm::ceil(-(positionFromOrigin.x + positionFromOrigin.y) / 2.0),
            (positionFromOrigin.x - positionFromOrigin.y) / 2,
            zLevel
        );
    }

    // Selects from bottom Z face
    constexpr ivec3 pos2DIsoToPos3DAtZLevelNew(
        const ivec2 positionFromOrigin,
        const int zLevel
    )
    {
        ivec2 positionZLevelAdjusted = positionFromOrigin - ivec2(0, zLevel * 2);
        return ivec3(
            glm::ceil(-(positionZLevelAdjusted.x + positionZLevelAdjusted.y) / 2.0),
            (positionZLevelAdjusted.x - positionZLevelAdjusted.y) / 2,
            zLevel
        );
    }

    // Alt from above TODO remove above
    constexpr ivec3 pos2DIsoToPos3DAtZLevelAlt(
        const ivec2 position,
        const int zLevel
    )
    {
        return ivec3(
            glm::ceil(-(position.x + position.y) / 2.0),
            (position.x - position.y) / 2,
            zLevel
        );
    }

    constexpr int index2DtoIndex1D(const ivec2 index, const ivec2 size) {
        return index.y * size.x + index.x;
    }

    constexpr int index3DtoIndex1D(const ivec3 index, const ivec3 size) {
        return index.z * size.x * size.y + index.y * size.x + index.x;
    }

    // ISOMETRIC THINGS

    template <FaceType faceType>
    constexpr ivec2 calculatePartnerTriangleIndex(ivec2 index);

    template <>
    constexpr ivec2 calculatePartnerTriangleIndex<FaceType::X_FACE>(
        ivec2 index
    )
    {
        if(IRMath::sumVecComponents(index) % 2 == 0) {
            return index + ivec2(0, -1);
        }

        return index + ivec2(0, 1);
    }

    template <>
    constexpr ivec2 calculatePartnerTriangleIndex<FaceType::Y_FACE>(
        ivec2 index
    )
    {
        if(IRMath::sumVecComponents(index) % 2 == 0) {
            return index + ivec2(0, 1);
        }

        return index + ivec2(0, -1);
    }

    template <>
    constexpr ivec2 calculatePartnerTriangleIndex<FaceType::Z_FACE>(
        ivec2 index
    )
    {
        if(IRMath::sumVecComponents(index) % 2 == 0) {
            return index + ivec2(-1, 0);
        }

        return index + ivec2(1, 0);
    }

    constexpr ivec2 size3DtoSize2DIso(const ivec3 size) {
        return ivec2(
            size.x + size.y,
            (size.x + size.y) + (size.z * 2) - 1 // TODO: check this
        );
    }

    constexpr uvec2 gameResolutionToSize2DIso(
        const uvec2 gameResolution,
        const uvec2 scaleFactor
    )
    {
        // Floor division
        return gameResolution / uvec2(2, 1) / scaleFactor;
    }

    constexpr uvec2 size2DIsoToGameResolution(
        const uvec2 size,
        const uvec2 scaleFactor
    )
    {
        // Floor division (THIS IS UNTESTED)
        return size / uvec2(1, 2) * scaleFactor;
    }

    // TODO: make constexpr somehow
    vec2 calculateTrianglePositionOffsetIso(const vec2 position);

    constexpr int calcResolutionWidthFromHeightAndAspectRatio(
        const int height,
        const ivec2 aspectRatio
    )
    {
        return static_cast<int>(height * static_cast<float>(aspectRatio.y) / aspectRatio.x);
    }

    constexpr int calcResolutionHeightFromWidthAndAspectRatio(
        const int width,
        const ivec2 aspectRatio
    )
    {
        return static_cast<int>(width * static_cast<float>(aspectRatio.x) / aspectRatio.y);
    }


    // Float to byte rounding
    constexpr uint8_t roundFloatToByte(const float value) {
        return (uint8_t)round(value * 255.0f);
    }

    constexpr float roundByteToFloat(const uint8_t value) {
        return (float)value / 255.0f;
    }

    constexpr ivec3 roundVec3ToIVec3(vec3 value) {
        return ivec3(
            round(value.x),
            round(value.y),
            round(value.z)
        );
    }

    Color colorHSVToColor(
        const ColorHSV& colorHSV
    );

    ColorHSV colorToColorHSV(const Color& color);

    // 2D

    // i^ grid to screen
    template<vec2 objSize>
    constexpr vec2 kIHatGridToScreenIso =
        vec2(1.0f, 0.5f) * (objSize / 2.0f)
    ;

    // j^ grid to screen
    template<vec2 objSize>
    constexpr vec2 kJHatGridToScreenIso =
        vec2(-1.0f, 0.5f) * (objSize / 2.0f)
    ;

    // xy matrix grid to screen
    template<vec2 iHat, vec2 jHat>
    constexpr mat2 k2DGridToScreenIsoTransform =
        // TODO: is this order correct??
        mat2(
            iHat.x, jHat.x,
            iHat.y, jHat.y
        );

    // 3D

    // Isometric traversal (these cycle)

    // X = 0 face (left)

    constexpr ivec3 kRaymarchStepsXFaceLower[] = {
        ivec3(0, 1, 0),
        ivec3(1, 0, 0),
        ivec3(0, 0, 1),
        ivec3(0, 1, 0),
        ivec3(1, 0, 0),
    };

    constexpr ivec3 kRaymarchStepXFaceUpper[] = {
        ivec3(0, 1, 0),
        ivec3(1, 0, 0),
        ivec3(0, 1, 0),
        ivec3(0, 0, 1),
        ivec3(1, 0, 0),
    };

    constexpr ivec3 kRaymarchStepYFaceUpper[] = {
        ivec3(1, 0, 0),
        ivec3(0, 1, 0),
        ivec3(1, 0, 0),
        ivec3(0, 0, 1),
        ivec3(0, 1, 0),
    };

    constexpr ivec3 kRaymarchStepsYFaceLower[] = {
        ivec3(1, 0, 0),
        ivec3(0, 1, 0),
        ivec3(0, 0, 1),
        ivec3(1, 0, 0),
        ivec3(0, 1, 0)
    };

    constexpr ivec3 kRaymarchStepsZFaceLeft[] = {
        ivec3(1, 0, 0),
        ivec3(0, 1, 0),
        ivec3(1, 0, 0),
        ivec3(0, 1, 0),
        ivec3(0, 0, 1),
    };

    constexpr ivec3 kRaymarchStepsZFaceRight[] = {
        ivec3(0, 1, 0),
        ivec3(1, 0, 0),
        ivec3(0, 1, 0),
        ivec3(1, 0, 0),
        ivec3(0, 0, 1),
    };

    template <int FPS>
    constexpr int secondsToFrames(float seconds) {
        return ceil(seconds * FPS);
    }


}


#endif /* IR_MATH_H */
