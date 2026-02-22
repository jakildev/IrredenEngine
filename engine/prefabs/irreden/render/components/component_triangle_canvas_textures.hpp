#ifndef COMPONENT_TRIANGLE_CANVAS_TEXTURES_H
#define COMPONENT_TRIANGLE_CANVAS_TEXTURES_H

#include <irreden/ir_math.hpp>
#include <irreden/ir_render.hpp>
#include <irreden/ir_asset.hpp>

#include <irreden/render/texture.hpp>

using namespace IRMath;
using namespace IRRender;

namespace IRComponents {

struct C_TriangleCanvasTextures {
    ivec2 size_;
    std::pair<ResourceId, Texture2D *> textureTriangleColors_;
    std::pair<ResourceId, Texture2D *> textureTriangleDistances_;

    C_TriangleCanvasTextures(ivec2 size)
        : size_{size}
        , textureTriangleColors_{IRRender::createResource<IRRender::Texture2D>(
              GL_TEXTURE_2D, size.x, size.y, GL_RGBA8, GL_REPEAT, GL_NEAREST
          )}
        , textureTriangleDistances_{IRRender::createResource<IRRender::Texture2D>(
              GL_TEXTURE_2D, size.x, size.y, GL_R32I, GL_REPEAT, GL_NEAREST
          )} {}

    C_TriangleCanvasTextures() {}

    void onDestroy() {
        IRRender::destroyResource<Texture2D>(textureTriangleColors_.first);
        IRRender::destroyResource<Texture2D>(textureTriangleDistances_.first);
    }

    const Texture2D *getTextureColors() const {
        return textureTriangleColors_.second;
    }

    const Texture2D *getTextureDistances() const {
        return textureTriangleDistances_.second;
    }

    void bind(int textureUnitColors, int textureUnitDistances) const {
        textureTriangleColors_.second->bind(textureUnitColors);
        textureTriangleDistances_.second->bind(textureUnitDistances);
    }

    void clear() const {
        textureTriangleColors_.second->clear(GL_RGBA, GL_UNSIGNED_BYTE, &u8vec4(0, 0, 0, 0)[0]);
        textureTriangleDistances_.second
            ->clear(GL_RED_INTEGER, GL_INT, &ivec1(IRConstants::kTrixelDistanceMaxDistance)[0]);
    }

    void clearWithColor(const Color &color) const {
        textureTriangleColors_.second->clear(GL_RGBA, GL_UNSIGNED_BYTE, &color);
        clearDistanceTexture();
    }

    void clearWithColorData(ivec2 size, const std::vector<Color> &colorData) const {
        textureTriangleColors_.second
            ->subImage2D(0, 0, size.x, size.y, GL_RGBA, GL_UNSIGNED_BYTE, colorData.data());
        clearDistanceTexture();
    }

    void setTrixel(ivec2 index, Color color, int distance = 0) {
        textureTriangleColors_.second
            ->subImage2D(index.x, index.y, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, &color);
        textureTriangleDistances_.second
            ->subImage2D(index.x, index.y, 1, 1, GL_RED_INTEGER, GL_INT, &distance);
    }

    void saveToFile(std::string name) const {
        std::vector<Color> colorData;
        std::vector<Distance> distanceData;

        colorData.resize(size_.x * size_.y);
        distanceData.resize(size_.x * size_.y);

        textureTriangleColors_.second
            ->getSubImage2D(0, 0, size_.x, size_.y, GL_RGBA, GL_UNSIGNED_BYTE, colorData.data());
        textureTriangleDistances_.second
            ->getSubImage2D(0, 0, size_.x, size_.y, GL_RED_INTEGER, GL_INT, distanceData.data());
        IRAsset::saveTrixelTextureData(name, "../save_files/", size_, colorData, distanceData);
    }

    void loadFromFile(const std::string &filename) {
        std::vector<Color> colorData;
        std::vector<Distance> distanceData;

        IRAsset::loadTrixelTextureData(filename, "", size_, colorData, distanceData);

        textureTriangleColors_.second
            ->subImage2D(0, 0, size_.x, size_.y, GL_RGBA, GL_UNSIGNED_BYTE, colorData.data());
        textureTriangleDistances_.second
            ->subImage2D(0, 0, size_.x, size_.y, GL_RED_INTEGER, GL_INT, distanceData.data());
    }

    void saveAsPNG() {}

  private:
    void clearDistanceTexture() const {
        textureTriangleDistances_.second
            ->clear(GL_RED_INTEGER, GL_INT, &ivec1(IRConstants::kTrixelDistanceMaxDistance - 1)[0]);
    }
};

} // namespace IRComponents

#endif /* COMPONENT_TRIANGLE_CANVAS_TEXTURES_H */
