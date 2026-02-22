#ifndef COMPONENT_TRIANGLES_ONLY_SET_H
#define COMPONENT_TRIANGLES_ONLY_SET_H

#include <irreden/ir_math.hpp>
#include <irreden/ir_profile.hpp>

#include <vector>

using namespace IRMath;

namespace IRComponents {
constexpr Color kBackgroundColor = Color{255, 255, 255, 255};

// Primarily will be used for gui elements but could potench
// be for a 2d game or something.
struct C_TrianglesOnlySet {

    // TODO: How can these be const
    ivec2 size_;
    ivec2 origin_;
    std::vector<Color> triangleColors_;
    std::vector<Distance> triangleDistances_;

    C_TrianglesOnlySet(ivec2 size, ivec2 origin)
        : size_{size}, origin_{origin}, triangleColors_{}

    {
        triangleColors_.resize(size_.x * size_.y);
        triangleDistances_.resize(size_.x * size_.y);

        std::fill(triangleDistances_.begin(), triangleDistances_.end(),
                  IRConstants::kTrixelDistanceMaxDistance);
    }

    // Default
    C_TrianglesOnlySet()
        : size_{ivec2(0, 0)}, origin_{ivec2(0, 0)}, triangleColors_{}, triangleDistances_{} {}

    void resize(ivec2 size) {
        size_ = size;
        triangleColors_.resize(size_.x * size_.y);
        triangleDistances_.resize(size_.x * size_.y);
    }

    void resize() {
        this->resize(size_);
    }

    void setTriangle(ivec2 pos, Color color, uint32_t distance = 0) {
        IR_ASSERT(IRMath::all(IRMath::lessThan(pos, size_)) &&
                      IRMath::all(IRMath::greaterThanEqual(pos, ivec2(0))),
                  "TRIANGLE INDEX OUT OF BOUNDS.");
        Color &triangleColorCurrent = this->atTriangleColor(pos);
        Distance &triangleDistanceCurrent = this->atTriangleDistance(pos);

        if (distance <= triangleDistanceCurrent) {
            triangleColorCurrent = color;
            triangleDistanceCurrent = distance;
        }
    }

    void pasteSet(const C_TrianglesOnlySet &set, ivec2 pos) {
        for (int y = 0; y < set.size_.y; ++y) {
            for (int x = 0; x < set.size_.x; ++x) {
                ivec2 destIndex = pos + ivec2(x, y);
                if (IRMath::all(IRMath::lessThan(destIndex, size_)) &&
                    IRMath::all(IRMath::greaterThanEqual(destIndex, ivec2(0)))) {
                    this->atTriangleColor(destIndex) = set.atTriangleColor(ivec2(x, y));
                    this->atTriangleDistance(destIndex) = set.atTriangleDistance(ivec2(x, y));
                }
            }
        }
    }

    // overloads
    // void setTriangle(ivec2 pos, vec3 colorHSV, uint32_t distance = 0) {
    //     this->setTriangle(pos, IRMath::colorHSVToColor(colorHSV), distance);
    // }

    void moveSelection(ivec2 startIndex, ivec2 endIndex, ivec2 travelDistance) {
        ivec2 selectionSize = endIndex + ivec2(1, 1) - startIndex;
        std::vector<Color> selectionColors{};
        selectionColors.resize(selectionSize.x * selectionSize.y);

        std::vector<uint32_t> selectionDistance{};
        selectionDistance.resize(selectionSize.x * selectionSize.y);

        // Copy selection
        for (int y = 0; y < selectionSize.y; ++y) {
            for (int x = 0; x < selectionSize.x; ++x) {
                int selectionIndex = x + y * selectionSize.x;
                selectionColors[selectionIndex] = this->atTriangleColor(startIndex + ivec2(x, y));
                selectionDistance[selectionIndex] =
                    this->atTriangleDistance(startIndex + ivec2(x, y));
                this->atTriangleColor(startIndex + ivec2(x, y)) = kBackgroundColor;
                this->atTriangleDistance(startIndex + ivec2(x, y)) =
                    IRConstants::kTrixelDistanceMaxDistance;
            }
        }

        // Write selection to new destination
        for (int y = 0; y < selectionSize.y; ++y) {
            for (int x = 0; x < selectionSize.x; ++x) {
                int selectionIndex = x + y * selectionSize.x;
                ivec2 destIndex = startIndex + ivec2(x, y) + travelDistance;
                if (IRMath::all(IRMath::lessThan(destIndex, size_)) &&
                    IRMath::all(IRMath::greaterThanEqual(destIndex, ivec2(0)))) {
                    this->atTriangleColor(destIndex) = selectionColors[selectionIndex];
                    this->atTriangleDistance(destIndex) = selectionDistance[selectionIndex];
                }
            }
        }
    }

    Color &atTriangleColor(uvec2 index) {
        return triangleColors_[IRMath::index2DtoIndex1D(index, size_)];
    }

    const Color &atTriangleColor(uvec2 index) const {
        return triangleColors_[IRMath::index2DtoIndex1D(index, size_)];
    }

    Distance &atTriangleDistance(uvec2 index) {
        return triangleDistances_[IRMath::index2DtoIndex1D(index, size_)];
    }

    const Distance &atTriangleDistance(uvec2 index) const {
        return triangleDistances_[IRMath::index2DtoIndex1D(index, size_)];
    }

    int calcTriangleColorIndex(ivec2 index) {
        return index.y * size_.x + index.x;
    }
};

} // namespace IRComponents

#endif /* COMPONENT_TRIANGLES_ONLY_SET_H */
