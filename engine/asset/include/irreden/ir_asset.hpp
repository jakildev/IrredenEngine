#ifndef IR_ASSET_H
#define IR_ASSET_H

#include <irreden/ir_math.hpp>

#include <string>

using namespace IRMath;

namespace IRAsset {

enum FileTypes { kSpriteImage, kTrixelImage, kVoxelImage };

void saveTrixelTextureData(const std::string &name, const std::string &path, ivec2 size,
                           const std::vector<Color> &colors,
                           const std::vector<Distance> &distances);

void loadTrixelTextureData(const std::string &name, const std::string &path, ivec2 &size,
                           std::vector<Color> &colors, std::vector<Distance> &distances);
}; // namespace IRAsset

#endif /* IR_ASSET_H */
