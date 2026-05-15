#ifndef IR_ASSET_H
#define IR_ASSET_H

#include <irreden/ir_math.hpp>

#include <string>
#include <vector>

using namespace IRMath;

namespace IRAsset {

/// Asset file type discriminant.
/// `kSpriteImage` has `saveSpriteSheetMeta` / `loadSpriteSheetMeta` for
/// the `.irsprite` sidecar (the paired PNG is handled by render-module
/// `ImageData`). `kVoxelImage` is an aspirational stub with no I/O yet.
enum FileTypes { kSpriteImage, kVoxelImage };

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
