/*
 * Project: Irreden Engine
 * File: \irreden-engine\src\game_components\component_rendering_triangle_canvas_textures.hpp
 * Author: Evin Killian jakildev@gmail.com
 * Created Date: October 2023
 * -----
 * Modified By: <your_name> <Month> <YYYY>
 */

#ifndef COMPONENT_RENDERING_TRIANGLE_CANVAS_TEXTURES_H
#define COMPONENT_RENDERING_TRIANGLE_CANVAS_TEXTURES_H

#include "../math/ir_math.hpp"
#include "../rendering/ir_rendering.hpp"
#include "../rendering/rendering_rm.hpp"
#include "../rendering/texture.hpp"

using namespace IRMath;
using namespace IRRendering;

namespace IRComponents {

    struct C_TriangleCanvasTextures {
        ivec2 size_;
        std::pair<ResourceId, Texture2D*> textureTriangleColors_;
        std::pair<ResourceId, Texture2D*> textureTriangleDistances_;

        C_TriangleCanvasTextures(
            ivec2 size
        )
        :   size_{size}
        ,   textureTriangleColors_{
                global.renderingResourceManager_->create<
                    IRRendering::Texture2D
                >(
                    GL_TEXTURE_2D,
                    size.x,
                    size.y,
                    GL_RGBA8,
                    GL_REPEAT,
                    GL_NEAREST
                )
            }
        ,   textureTriangleDistances_{
                global.renderingResourceManager_->create<
                    IRRendering::Texture2D
                >(
                    GL_TEXTURE_2D,
                    size.x,
                    size.y,
                    GL_R32I,
                    GL_REPEAT,
                    GL_NEAREST
                )
            }
        {

        }

        C_TriangleCanvasTextures() {}

        void onDestroy() {
            global.renderingResourceManager_->destroy<Texture2D>(
                textureTriangleColors_.first
            );
            global.renderingResourceManager_->destroy<Texture2D>(
                textureTriangleDistances_.first
            );
        }

        const Texture2D* getTextureColors() const {
            return textureTriangleColors_.second;
        }

        const Texture2D* getTextureDistances() const {
            return textureTriangleDistances_.second;
        }

        void bind(int textureUnitColors, int textureUnitDistances) const {
            textureTriangleColors_.second->bind(textureUnitColors);
            textureTriangleDistances_.second->bind(textureUnitDistances);
        }

        void clear() const {
            textureTriangleColors_.second->clear(
                GL_RGBA,
                GL_UNSIGNED_BYTE,
                &u8vec4(0, 0, 0, 0)[0]
            );
            textureTriangleDistances_.second->clear(
                GL_RED_INTEGER,
                GL_INT,
                &ivec1(IRConstants::kTriangleDistanceMaxDistance)[0]
            );
        }

        void clearWithColor(const Color& color) const {
            textureTriangleColors_.second->clear(
                GL_RGBA,
                GL_UNSIGNED_BYTE,
                &color
            );
            clearDistanceTexture();
        }

        void clearWithColorData(
            ivec2 size,
            const std::vector<Color>& colorData
        ) const
        {
            textureTriangleColors_.second->subImage2D(
                0,
                0,
                size.x,
                size.y,
                GL_RGBA,
                GL_UNSIGNED_BYTE,
                colorData.data()
            );
            clearDistanceTexture();
        }
    private:
        void clearDistanceTexture() const {
            textureTriangleDistances_.second->clear(
                GL_RED_INTEGER,
                GL_INT,
                &ivec1(IRConstants::kTriangleDistanceMaxDistance - 1)[0]
            );
        }
    };

} // IRComponents

#endif /* COMPONENT_RENDERING_TRIANGLE_CANVAS_TEXTURES_H */
