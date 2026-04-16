#ifndef IR_ASSET_H
#define IR_ASSET_H

#include <irreden/ir_math.hpp>

#include <string>

using namespace IRMath;

namespace IRAsset {

/// Asset file type discriminant.
/// Note: only `kTrixelImage` has corresponding I/O routines today.
/// `kSpriteImage` and `kVoxelImage` are aspirational stubs with no
/// load/save implementation yet.
enum FileTypes { kSpriteImage, kTrixelImage, kVoxelImage };

/// Writes trixel texture data to a raw binary file at @p path / @p name.txt.
/// The name is embedded in the file header; @p path is the output directory.
/// Format: `ivec2 size | size.x*size.y Color entries | size.x*size.y Distance entries`.
/// No checksum or magic bytes — treat files as volatile.
void saveTrixelTextureData(
    const std::string &name,
    const std::string &path,
    ivec2 size,
    const std::vector<Color> &colors,
    const std::vector<Distance> &distances
);

/// Reads trixel texture data from a raw binary file previously written by
/// @ref saveTrixelTextureData.  `fread` return values are unchecked — a
/// truncated file loads as garbage with no error.
void loadTrixelTextureData(
    const std::string &name,
    const std::string &path,
    ivec2 &size,
    std::vector<Color> &colors,
    std::vector<Distance> &distances
);
}; // namespace IRAsset

#endif /* IR_ASSET_H */
