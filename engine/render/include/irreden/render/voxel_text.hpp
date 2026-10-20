#ifndef VOXEL_TEXT_H
#define VOXEL_TEXT_H

#include <irreden/ir_math.hpp>
#include <irreden/ir_profile.hpp>
#include <irreden/render/voxel_font.hpp>

#include <algorithm>
#include <cstdint>
#include <string_view>
#include <vector>

namespace IRRender {

// Isometric voxel text for the GUI canvas. Each character of the voxel glyph
// set (voxel_font.hpp) becomes a small block of unit voxels seen from the
// fixed iso camera, projected onto the canvas's triangle lattice; the letters
// then sit side by side along screen-horizontal lines like ordinary text, so
// only each letter is isometric, never the word's path.
//
// A voxel at (x, y, z) projects to the 2x3 texel cell at
// voxelTextCellOrigin(x, y, z) of a parity-aligned origin (texel (c, r) a
// left-pointing triangle when c + r is even), and its three visible faces
// take these texels of that cell:
//   Z (top, -z):          (0,0) (1,0)   -> a diamond
//   X (left side, -x):    (0,1) (0,2)   -> a parallelogram rising leftwards
//   Y (right side, -y):   (1,1) (1,2)   -> a parallelogram rising rightwards
// World +x is screen right-up, +y screen left-up and +z screen down, as in
// the world render (engine/math/CLAUDE.md), so each face keeps the role it
// has on a world voxel. x + y + z is depth (larger is farther); faces of a
// nearer voxel cover a farther one's.

enum class VoxelTextFacing : int {
    // Letter plane spans world (1,-1,0) and +z: upright and unsheared on
    // screen, each lit pixel a cube showing both side faces and a top.
    CAMERA = 0,
    // Letter printed on the X face plane: reads down to the right.
    X_FACE = 1,
    // Letter printed on the Y face plane: reads up to the right.
    Y_FACE = 2,
};

// The three face indices match IRMath::kXFace / kYFace / kZFace.
struct VoxelFaceColors {
    IRMath::Color x_ = IRMath::Color{200, 200, 200, 255};
    IRMath::Color y_ = IRMath::Color{150, 150, 150, 255};
    IRMath::Color z_ = IRMath::IRColors::kWhite;

    constexpr IRMath::Color forFace(int face) const {
        if (face == IRMath::kXFace) {
            return x_;
        }
        if (face == IRMath::kYFace) {
            return y_;
        }
        return z_;
    }
};

// `top` on Z, then two darker side faces at fixed ratios (left lit more
// than right). Alpha is kept.
constexpr VoxelFaceColors shadedVoxelFaceColors(IRMath::Color top) {
    const auto scaled = [top](int percent) {
        return IRMath::Color{
            static_cast<std::uint8_t>(top.red_ * percent / 100),
            static_cast<std::uint8_t>(top.green_ * percent / 100),
            static_cast<std::uint8_t>(top.blue_ * percent / 100),
            top.alpha_
        };
    };
    return VoxelFaceColors{scaled(78), scaled(56), top};
}

constexpr int kMaxVoxelTextSize = 16;
constexpr int kMaxVoxelTextDepth = 16;

struct VoxelTextStyle {
    // Edge of one glyph pixel's cube, in unit voxels (one unit voxel is a
    // 2x4-texel hexagon). 0 means flat trixel-font text (no voxel style).
    int voxelSize_ = 0;
    // Letter thickness in glyph pixels, extruded away from the camera.
    int depth_ = 1;
    VoxelTextFacing facing_ = VoxelTextFacing::CAMERA;
};

// One axis-aligned block of texels that share a face, in layout-local
// texels; `line_` indexes VoxelTextLayout::lineWidths_.
struct VoxelTextRun {
    IRMath::ivec2 pos_;
    IRMath::ivec2 size_;
    int face_;
    int line_;
};

// Runs are capped so one fits a text-shader glyph cell.
constexpr IRMath::ivec2 kVoxelTextMaxRun = IRMath::ivec2(6, 11);

struct VoxelTextLayout {
    std::vector<VoxelTextRun> runs_;
    std::vector<int> lineWidths_;
    IRMath::ivec2 size_ = IRMath::ivec2(0);
    int lineStep_ = 0;
};

// Reusable buffers for layoutVoxelText; hold one across calls to avoid
// per-call allocation.
struct VoxelTextScratch {
    std::vector<IRMath::ivec4> voxels_;
    std::vector<std::int8_t> unitFaces_;
    std::vector<std::int8_t> faces_;
    std::vector<std::uint8_t> claimed_;
    std::vector<VoxelTextRun> glyphRuns_;
};

constexpr IRMath::ivec2 voxelTextCellOrigin(int x, int y, int z) {
    return IRMath::ivec2(x - y, -x - y + 2 * z);
}

namespace detail {

constexpr std::int8_t kNoFace = -1;

// The unit voxel for glyph pixel (col, row) of extrusion layer `layer` (0 is
// the front), sub-voxel `row` already scaled by the facing's row voxels.
constexpr IRMath::ivec3 voxelTextVoxel(VoxelTextFacing facing, int col, int row, int layer) {
    switch (facing) {
    case VoxelTextFacing::X_FACE:
        return IRMath::ivec3(layer, -col, row);
    case VoxelTextFacing::Y_FACE:
        return IRMath::ivec3(col, layer, row);
    case VoxelTextFacing::CAMERA:
    default:
        return IRMath::ivec3(col + layer, -col + layer, row);
    }
}

constexpr int voxelTextRowVoxels(VoxelTextFacing facing) {
    return facing == VoxelTextFacing::CAMERA ? kVoxelGlyphRowVoxels : 1;
}

// Texel bounds of every voxel cell a full glyph box can occupy, shifted so
// the minimum lands on an even-parity origin.
struct VoxelCellBounds {
    IRMath::ivec2 min_;
    IRMath::ivec2 size_;
};

inline VoxelCellBounds voxelTextCellBounds(VoxelTextFacing facing, int depth) {
    const int rows = kVoxelGlyphHeight * voxelTextRowVoxels(facing);
    IRMath::ivec2 lo(INT32_MAX);
    IRMath::ivec2 hi(INT32_MIN);
    for (const int col : {0, kVoxelGlyphWidth - 1}) {
        for (const int row : {0, rows - 1}) {
            for (const int layer : {0, depth - 1}) {
                const IRMath::ivec3 v = voxelTextVoxel(facing, col, row, layer);
                const IRMath::ivec2 cell = voxelTextCellOrigin(v.x, v.y, v.z);
                lo = IRMath::min(lo, cell);
                hi = IRMath::max(hi, cell);
            }
        }
    }
    if (((lo.x + lo.y) & 1) != 0) {
        lo.x -= 1;
    }
    return {lo, hi - lo + IRMath::ivec2(2, 3)};
}

// Paints glyph `glyph` as unit voxels into `faces` (size.x * size.y, row
// major), farthest voxel first.
inline void paintUnitGlyph(
    const VoxelGlyph &glyph,
    VoxelTextFacing facing,
    int depth,
    const VoxelCellBounds &bounds,
    std::vector<IRMath::ivec4> &voxels,
    std::vector<std::int8_t> &faces
) {
    const int rowVoxels = voxelTextRowVoxels(facing);
    voxels.clear();
    for (int row = 0; row < kVoxelGlyphHeight; ++row) {
        for (int col = 0; col < kVoxelGlyphWidth; ++col) {
            if (!voxelGlyphPixel(glyph, col, row)) {
                continue;
            }
            for (int sub = 0; sub < rowVoxels; ++sub) {
                for (int layer = 0; layer < depth; ++layer) {
                    const IRMath::ivec3 v =
                        voxelTextVoxel(facing, col, row * rowVoxels + sub, layer);
                    voxels.emplace_back(v, v.x + v.y + v.z);
                }
            }
        }
    }
    std::sort(voxels.begin(), voxels.end(), [](const IRMath::ivec4 &a, const IRMath::ivec4 &b) {
        return a.w > b.w;
    });

    faces.assign(static_cast<std::size_t>(bounds.size_.x * bounds.size_.y), kNoFace);
    const auto put = [&](IRMath::ivec2 texel, int face) {
        faces[static_cast<std::size_t>(texel.y * bounds.size_.x + texel.x)] =
            static_cast<std::int8_t>(face);
    };
    for (const IRMath::ivec4 &v : voxels) {
        const IRMath::ivec2 cell = voxelTextCellOrigin(v.x, v.y, v.z) - bounds.min_;
        put(cell + IRMath::ivec2(0, 0), IRMath::kZFace);
        put(cell + IRMath::ivec2(1, 0), IRMath::kZFace);
        put(cell + IRMath::ivec2(0, 1), IRMath::kXFace);
        put(cell + IRMath::ivec2(0, 2), IRMath::kXFace);
        put(cell + IRMath::ivec2(1, 1), IRMath::kYFace);
        put(cell + IRMath::ivec2(1, 2), IRMath::kYFace);
    }
}

// Rows the lattice scale-up of a unit map reaches above its row 0, rounded
// up to even so the scaled map keeps an even-parity origin.
constexpr int voxelTextScalePad(int scale) {
    return scale / 2 * 2;
}

constexpr IRMath::ivec2 voxelTextScaledSize(IRMath::ivec2 unitSize, int scale) {
    return IRMath::ivec2(unitSize.x * scale, unitSize.y * scale + 2 * voxelTextScalePad(scale));
}

// Scales a unit face map by `scale` on the triangle lattice: texel (c, r)
// becomes the side-`scale` triangle of scale^2 texels the GUI text shader's
// lattice scaling draws for it, so cubes of edge `scale` stay exact
// hexagons.
inline void latticeScaleFaces(
    const std::vector<std::int8_t> &unit,
    IRMath::ivec2 unitSize,
    int scale,
    std::vector<std::int8_t> &scaled
) {
    const IRMath::ivec2 size = voxelTextScaledSize(unitSize, scale);
    const int pad = voxelTextScalePad(scale);
    scaled.assign(static_cast<std::size_t>(size.x * size.y), kNoFace);
    for (int row = 0; row < unitSize.y; ++row) {
        for (int col = 0; col < unitSize.x; ++col) {
            const std::int8_t face = unit[static_cast<std::size_t>(row * unitSize.x + col)];
            if (face == kNoFace) {
                continue;
            }
            const bool left = ((col + row) & 1) == 0;
            const IRMath::ivec2 center(col * scale, row * scale + pad);
            for (int k = 0; k < scale; ++k) {
                const int x = left ? center.x + k : center.x + scale - 1 - k;
                for (int dy = -k; dy <= k; ++dy) {
                    scaled[static_cast<std::size_t>((center.y + dy) * size.x + x)] = face;
                }
            }
        }
    }
}

// Greedy cover of `faces` by same-face blocks at most kVoxelTextMaxRun:
// grow down first (faces are tall strips), then right.
inline void mergeFaceRuns(
    const std::vector<std::int8_t> &faces,
    IRMath::ivec2 size,
    std::vector<std::uint8_t> &claimed,
    std::vector<VoxelTextRun> &runs
) {
    runs.clear();
    claimed.assign(faces.size(), 0);
    const auto at = [size](int x, int y) { return static_cast<std::size_t>(y * size.x + x); };
    const auto open = [&](int x, int y, std::int8_t face) {
        return faces[at(x, y)] == face && claimed[at(x, y)] == 0;
    };
    for (int y = 0; y < size.y; ++y) {
        for (int x = 0; x < size.x; ++x) {
            const std::int8_t face = faces[at(x, y)];
            if (face == kNoFace || claimed[at(x, y)] != 0) {
                continue;
            }
            int height = 1;
            while (height < kVoxelTextMaxRun.y && y + height < size.y &&
                   open(x, y + height, face)) {
                ++height;
            }
            int width = 1;
            while (width < kVoxelTextMaxRun.x && x + width < size.x) {
                bool columnMatches = true;
                for (int dy = 0; dy < height && columnMatches; ++dy) {
                    columnMatches = open(x + width, y + dy, face);
                }
                if (!columnMatches) {
                    break;
                }
                ++width;
            }
            for (int dy = 0; dy < height; ++dy) {
                for (int dx = 0; dx < width; ++dx) {
                    claimed[at(x + dx, y + dy)] = 1;
                }
            }
            runs.push_back({IRMath::ivec2(x, y), IRMath::ivec2(width, height), face, 0});
        }
    }
}

} // namespace detail

constexpr bool isVoxelTextStyle(const VoxelTextStyle &style) {
    return style.voxelSize_ > 0;
}

// Texel size of one glyph's box (every glyph shares it) for `style`.
inline IRMath::ivec2 voxelGlyphBoxSize(const VoxelTextStyle &style) {
    const detail::VoxelCellBounds bounds = detail::voxelTextCellBounds(style.facing_, style.depth_);
    return detail::voxelTextScaledSize(bounds.size_, style.voxelSize_);
}

// Horizontal gap between glyph boxes and vertical gap between lines, in
// texels; both even so every glyph box starts on an even-parity texel.
inline int voxelTextLetterGap(const VoxelTextStyle &style) {
    return 2 * style.voxelSize_;
}
inline int voxelTextLineGap(const VoxelTextStyle &style) {
    return 4 * style.voxelSize_;
}

// Lays `text` out as voxel letters: glyph boxes advance screen-right along a
// line, '\n' starts the next line, ' ' advances without drawing, and a
// character the set lacks draws as '?'. Output texels are relative to a
// parity-aligned origin (see parityAlignedPosition). Requires
// 1 <= voxelSize_ <= kMaxVoxelTextSize and 1 <= depth_ <= kMaxVoxelTextDepth.
inline void layoutVoxelText(
    std::string_view text,
    const VoxelTextStyle &style,
    VoxelTextLayout &layout,
    VoxelTextScratch &scratch
) {
    IR_ASSERT(
        style.voxelSize_ >= 1 && style.voxelSize_ <= kMaxVoxelTextSize,
        "layoutVoxelText: voxelSize_ out of range"
    );
    IR_ASSERT(
        style.depth_ >= 1 && style.depth_ <= kMaxVoxelTextDepth,
        "layoutVoxelText: depth_ out of range"
    );
    layout.runs_.clear();
    layout.lineWidths_.clear();

    const detail::VoxelCellBounds bounds = detail::voxelTextCellBounds(style.facing_, style.depth_);
    const IRMath::ivec2 box = detail::voxelTextScaledSize(bounds.size_, style.voxelSize_);
    const int advance = box.x + voxelTextLetterGap(style);
    layout.lineStep_ = box.y + voxelTextLineGap(style);

    int line = 0;
    int column = 0;
    const auto closeLine = [&]() {
        layout.lineWidths_.push_back(column > 0 ? column * advance - voxelTextLetterGap(style) : 0);
    };
    for (const char c : text) {
        if (c == '\n') {
            closeLine();
            ++line;
            column = 0;
            continue;
        }
        const VoxelGlyph &glyph = *getVoxelGlyph(c);
        if (!voxelGlyphIsBlank(glyph)) {
            detail::paintUnitGlyph(
                glyph,
                style.facing_,
                style.depth_,
                bounds,
                scratch.voxels_,
                scratch.unitFaces_
            );
            detail::latticeScaleFaces(
                scratch.unitFaces_,
                bounds.size_,
                style.voxelSize_,
                scratch.faces_
            );
            detail::mergeFaceRuns(scratch.faces_, box, scratch.claimed_, scratch.glyphRuns_);
            const IRMath::ivec2 origin(column * advance, line * layout.lineStep_);
            for (VoxelTextRun run : scratch.glyphRuns_) {
                run.pos_ += origin;
                run.line_ = line;
                layout.runs_.push_back(run);
            }
        }
        ++column;
    }
    closeLine();

    int widest = 0;
    for (const int width : layout.lineWidths_) {
        widest = IRMath::max(widest, width);
    }
    layout.size_ = IRMath::ivec2(
        widest,
        static_cast<int>(layout.lineWidths_.size()) * layout.lineStep_ - voxelTextLineGap(style)
    );
}

} // namespace IRRender

#endif /* VOXEL_TEXT_H */
