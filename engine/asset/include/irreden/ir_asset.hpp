#ifndef IR_ASSET_H
#define IR_ASSET_H

#include <irreden/ir_math.hpp>

#include <string>
#include <vector>

using namespace IRMath;

namespace IRAsset {

/// Asset file type discriminant.
/// `kTrixelImage` has `saveTrixelTextureData` / `loadTrixelTextureData`.
/// `kSpriteImage` has `saveSpriteSheetMeta` / `loadSpriteSheetMeta` for
/// the `.irsprite` sidecar (the paired PNG is handled by render-module
/// `ImageData`). `kVoxelImage` is an aspirational stub with no I/O yet.
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

// ---- Sprite-sheet sidecar ----

/// Per-animation descriptor in a .irsprite sidecar file.
struct SpriteAnimationDesc {
    std::string name_;
    int firstFrame_ = 0;
    int frameCount_ = 0;
    float fps_ = 0.0f;
};

/// Sidecar metadata for a PNG atlas sprite sheet.
/// Atlas pixel dimensions are NOT stored here — they are read from the PNG
/// at load time and used to compute normalized frame UV rects.
///
/// .irsprite plain-text format (lines, order-independent):
///   # comment line (ignored)
///   cellWidth  <pixels>
///   cellHeight <pixels>
///   margin     <pixels>   (optional outer border, default 0)
///   padding    <pixels>   (optional inter-cell gap, default 0)
///   anim <name> <firstFrame> <frameCount> <fps>
///
/// Animation names must not contain whitespace — the loader scans them
/// with `%127s`, which stops at the first space/tab and would silently
/// truncate. `saveSpriteSheetMeta` asserts this at write time.
struct SpriteSheetMeta {
    uvec2 cellSizePx_ = uvec2{0, 0};
    int margin_ = 0;
    int padding_ = 0;
    std::vector<SpriteAnimationDesc> animations_;
};

/// Writes sprite-sheet sidecar metadata to @p path / @p name .irsprite.
void saveSpriteSheetMeta(
    const std::string &name, const std::string &path, const SpriteSheetMeta &meta
);

/// Reads sprite-sheet sidecar metadata from @p path / @p name .irsprite.
/// Returns a default-constructed SpriteSheetMeta if the file cannot be opened.
SpriteSheetMeta loadSpriteSheetMeta(const std::string &name, const std::string &path);

}; // namespace IRAsset

#endif /* IR_ASSET_H */
