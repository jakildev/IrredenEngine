#include <gtest/gtest.h>

#include <irreden/render/gui_text_batch.hpp>
#include <irreden/render/voxel_text.hpp>

#include <array>
#include <map>
#include <set>
#include <stdexcept>
#include <vector>

namespace {

using IRMath::ivec2;
using IRRender::VoxelGlyph;
using IRRender::VoxelTextFacing;
using IRRender::VoxelTextLayout;
using IRRender::VoxelTextScratch;
using IRRender::VoxelTextStyle;

using FaceMap = std::map<std::pair<int, int>, int>;

// The face drawn at each texel of one glyph's box: `glyph` painted at unit
// size, then lattice-scaled by `scale`.
FaceMap glyphFaces(const VoxelGlyph &glyph, VoxelTextFacing facing, int depth, int scale) {
    const auto bounds = IRRender::detail::voxelTextCellBounds(facing, depth);
    std::vector<IRMath::ivec4> voxels;
    std::vector<std::int8_t> unit;
    std::vector<std::int8_t> scaled;
    IRRender::detail::paintUnitGlyph(glyph, facing, depth, bounds, voxels, unit);
    IRRender::detail::latticeScaleFaces(unit, bounds.size_, scale, scaled);
    const ivec2 size = IRRender::detail::voxelTextScaledSize(bounds.size_, scale);
    FaceMap faces;
    for (int y = 0; y < size.y; ++y) {
        for (int x = 0; x < size.x; ++x) {
            const std::int8_t face = scaled[static_cast<std::size_t>(y * size.x + x)];
            if (face != IRRender::detail::kNoFace) {
                faces[{x, y}] = face;
            }
        }
    }
    return faces;
}

// `faces` translated so its first texel (row-major) sits at the origin.
FaceMap normalized(const FaceMap &faces) {
    std::pair<int, int> first{INT32_MAX, INT32_MAX};
    for (const auto &[texel, face] : faces) {
        if (texel.second < first.second ||
            (texel.second == first.second && texel.first < first.first)) {
            first = texel;
        }
    }
    FaceMap result;
    for (const auto &[texel, face] : faces) {
        result[{texel.first - first.first, texel.second - first.second}] = face;
    }
    return result;
}

std::map<int, int> faceCounts(const FaceMap &faces) {
    std::map<int, int> counts;
    for (const auto &[texel, face] : faces) {
        ++counts[face];
    }
    return counts;
}

constexpr VoxelGlyph kOnePixel = IRRender::makeVoxelGlyph(
    "X...."
    "....."
    "....."
    "....."
    "....."
    "....."
    "....."
);

constexpr VoxelGlyph kTwoByTwo = IRRender::makeVoxelGlyph(
    "XX..."
    "XX..."
    "....."
    "....."
    "....."
    "....."
    "....."
);

TEST(VoxelText, OneVoxelIsAHexagonOfTwoTexelsPerFace) {
    // X_FACE takes one voxel row per glyph row, so one pixel is one voxel.
    const FaceMap faces = normalized(glyphFaces(kOnePixel, VoxelTextFacing::X_FACE, 1, 1));
    const FaceMap expected{
        {{0, 0}, IRMath::kZFace},
        {{1, 0}, IRMath::kZFace},
        {{0, 1}, IRMath::kXFace},
        {{0, 2}, IRMath::kXFace},
        {{1, 1}, IRMath::kYFace},
        {{1, 2}, IRMath::kYFace},
    };
    EXPECT_EQ(faces, expected);
}

TEST(VoxelText, NearerVoxelsCoverFartherFaces) {
    // A camera-facing pixel is two voxels stacked in z: the lower one's top
    // hides under the upper one's sides.
    const FaceMap faces = glyphFaces(kOnePixel, VoxelTextFacing::CAMERA, 1, 1);
    const std::map<int, int> expected{
        {IRMath::kXFace, 4},
        {IRMath::kYFace, 4},
        {IRMath::kZFace, 2},
    };
    EXPECT_EQ(faceCounts(faces), expected);
}

TEST(VoxelText, LatticeScaleEqualsTheBiggerCube) {
    // One pixel of size 2 against a 2x2-pixel glyph two deep at size 1: both
    // are a 2x2x2 voxel cube, so their face maps agree texel for texel.
    for (const VoxelTextFacing facing : {VoxelTextFacing::X_FACE, VoxelTextFacing::Y_FACE}) {
        const FaceMap scaled = normalized(glyphFaces(kOnePixel, facing, 1, 2));
        const FaceMap built = normalized(glyphFaces(kTwoByTwo, facing, 2, 1));
        EXPECT_EQ(scaled, built) << "facing " << static_cast<int>(facing);
        const std::map<int, int> expected{
            {IRMath::kXFace, 8},
            {IRMath::kYFace, 8},
            {IRMath::kZFace, 8},
        };
        EXPECT_EQ(faceCounts(scaled), expected);
    }
}

TEST(VoxelText, RunsTileEveryFaceTexelExactlyOnce) {
    VoxelTextLayout layout;
    VoxelTextScratch scratch;
    for (const VoxelTextFacing facing :
         {VoxelTextFacing::CAMERA, VoxelTextFacing::X_FACE, VoxelTextFacing::Y_FACE}) {
        for (const int size : {1, 3}) {
            const VoxelTextStyle style{size, 2, facing};
            IRRender::layoutVoxelText("B", style, layout, scratch);
            const FaceMap faces = glyphFaces(*IRRender::getVoxelGlyph('B'), facing, 2, size);
            FaceMap covered;
            for (const IRRender::VoxelTextRun &run : layout.runs_) {
                EXPECT_GE(run.size_.x, 1);
                EXPECT_GE(run.size_.y, 1);
                EXPECT_LE(run.size_.x, IRRender::kVoxelTextMaxRun.x);
                EXPECT_LE(run.size_.y, IRRender::kVoxelTextMaxRun.y);
                for (int dy = 0; dy < run.size_.y; ++dy) {
                    for (int dx = 0; dx < run.size_.x; ++dx) {
                        const std::pair<int, int> texel{run.pos_.x + dx, run.pos_.y + dy};
                        EXPECT_EQ(covered.count(texel), 0u) << "texel drawn twice";
                        covered[texel] = run.face_;
                    }
                }
            }
            EXPECT_EQ(covered, faces) << "facing " << static_cast<int>(facing) << " size " << size;
        }
    }
}

TEST(VoxelText, LettersAdvanceScreenRightOnOneBaseline) {
    VoxelTextLayout layout;
    VoxelTextScratch scratch;
    for (const VoxelTextFacing facing :
         {VoxelTextFacing::CAMERA, VoxelTextFacing::X_FACE, VoxelTextFacing::Y_FACE}) {
        const VoxelTextStyle style{2, 1, facing};
        const ivec2 box = IRRender::voxelGlyphBoxSize(style);
        const int advance = box.x + IRRender::voxelTextLetterGap(style);
        EXPECT_EQ(advance % 2, 0);
        EXPECT_EQ(layout.lineStep_ % 2, 0);

        IRRender::layoutVoxelText("I", style, layout, scratch);
        std::set<std::pair<int, int>> single;
        for (const auto &run : layout.runs_) {
            single.insert({run.pos_.x, run.pos_.y});
        }

        // "I I": the second I is the first moved by two advances, same rows.
        IRRender::layoutVoxelText("I I", style, layout, scratch);
        std::set<std::pair<int, int>> second;
        for (const auto &run : layout.runs_) {
            if (run.pos_.x >= 2 * advance) {
                second.insert({run.pos_.x - 2 * advance, run.pos_.y});
            }
        }
        EXPECT_EQ(second, single) << "facing " << static_cast<int>(facing);
        ASSERT_EQ(layout.lineWidths_.size(), 1u);
        EXPECT_EQ(layout.lineWidths_[0], 3 * advance - IRRender::voxelTextLetterGap(style));
        EXPECT_EQ(layout.size_, ivec2(layout.lineWidths_[0], box.y));
    }
}

TEST(VoxelText, NewlinesStackLinesAndUnknownCharactersDrawAsQuestionMarks) {
    VoxelTextLayout layout;
    VoxelTextScratch scratch;
    const VoxelTextStyle style{1, 1, VoxelTextFacing::CAMERA};
    IRRender::layoutVoxelText("AB\nC", style, layout, scratch);
    ASSERT_EQ(layout.lineWidths_.size(), 2u);
    EXPECT_GT(layout.lineWidths_[0], layout.lineWidths_[1]);
    bool sawSecondLine = false;
    for (const auto &run : layout.runs_) {
        if (run.line_ == 1) {
            sawSecondLine = true;
            EXPECT_GE(run.pos_.y, layout.lineStep_);
        } else {
            EXPECT_LT(run.pos_.y, layout.lineStep_);
        }
    }
    EXPECT_TRUE(sawSecondLine);

    IRRender::layoutVoxelText("\x01", style, layout, scratch);
    const std::vector<IRRender::VoxelTextRun> unknown = layout.runs_;
    IRRender::layoutVoxelText("?", style, layout, scratch);
    ASSERT_EQ(unknown.size(), layout.runs_.size());
    for (std::size_t i = 0; i < unknown.size(); ++i) {
        EXPECT_EQ(unknown[i].pos_, layout.runs_[i].pos_);
        EXPECT_EQ(unknown[i].size_, layout.runs_[i].size_);
    }
}

TEST(VoxelText, CommandsPaintEachRunWithItsFaceColourThroughTheSolidCell) {
    VoxelTextLayout layout;
    VoxelTextScratch scratch;
    IRRender::layoutVoxelText("HI", VoxelTextStyle{1, 1, VoxelTextFacing::CAMERA}, layout, scratch);
    const IRRender::VoxelFaceColors colors{
        IRMath::Color{255, 0, 0, 255},
        IRMath::Color{0, 255, 0, 255},
        IRMath::Color{0, 0, 255, 255},
    };
    const ivec2 canvas(400, 300);
    std::vector<IRRender::GlyphDrawCommand> commands;
    IRPrefab::GuiText::expandVoxelTextToCommands(commands, layout, ivec2(10, 20), canvas, colors);
    ASSERT_EQ(commands.size(), layout.runs_.size());
    const ivec2 origin = IRRender::parityAlignedPosition(ivec2(10, 20), canvas);
    for (std::size_t i = 0; i < commands.size(); ++i) {
        const auto &cmd = commands[i];
        const auto &run = layout.runs_[i];
        EXPECT_EQ(cmd.firstFontRow, static_cast<std::uint32_t>(IRPrefab::GuiText::kSolidCellStart));
        EXPECT_EQ(cmd.colorPacked, colors.forFace(run.face_).toPackedRGBA());
        EXPECT_EQ(cmd.styleFlags & 0xFFu, 1u);
        EXPECT_EQ((cmd.styleFlags >> IRRender::kGlyphStyleCellWidthShift) & 0xFu, run.size_.x);
        EXPECT_EQ((cmd.styleFlags >> IRRender::kGlyphStyleCellHeightShift) & 0xFu, run.size_.y);
        const ivec2 pos(
            static_cast<int>(cmd.positionPacked & 0xFFFFu),
            static_cast<int>(cmd.positionPacked >> 16u)
        );
        EXPECT_EQ(pos, origin + run.pos_);
    }

    // Centering moves every run by one even offset.
    commands.clear();
    IRPrefab::GuiText::expandVoxelTextToCommands(
        commands,
        layout,
        ivec2(0, 0),
        canvas,
        colors,
        IRComponents::TextAlignH::CENTER,
        IRComponents::TextAlignV::CENTER
    );
    const ivec2 first(
        static_cast<int>(commands[0].positionPacked & 0xFFFFu),
        static_cast<int>(commands[0].positionPacked >> 16u)
    );
    const ivec2 shift =
        first - IRRender::parityAlignedPosition(ivec2(0), canvas) - layout.runs_[0].pos_;
    EXPECT_EQ(shift.x % 2, 0);
    EXPECT_EQ(shift.y % 2, 0);
    EXPECT_NEAR(shift.x, (canvas.x - layout.size_.x) / 2, 1);
    EXPECT_NEAR(shift.y, (canvas.y - layout.size_.y) / 2, 1);
}

TEST(VoxelText, FontBufferCarriesALitSolidCell) {
    const std::vector<std::uint32_t> font = IRPrefab::GuiText::buildFontBuffer();
    ASSERT_EQ(static_cast<int>(font.size()), IRPrefab::GuiText::kFontBufferSize);
    for (int row = 0; row < IRRender::kVoxelTextMaxRun.y; ++row) {
        const std::uint32_t bits = font[IRPrefab::GuiText::kSolidCellStart + row];
        for (int width = 1; width <= IRRender::kVoxelTextMaxRun.x; ++width) {
            for (int col = 0; col < width; ++col) {
                EXPECT_NE((bits >> (width - 1 - col)) & 1u, 0u);
            }
        }
    }
}

#ifndef IR_RELEASE
TEST(VoxelText, OutOfRangeStylesTripTheLayoutGuards) {
    VoxelTextLayout layout;
    VoxelTextScratch scratch;
    for (const VoxelTextStyle style : {
             VoxelTextStyle{0, 1, VoxelTextFacing::CAMERA},
             VoxelTextStyle{IRRender::kMaxVoxelTextSize + 1, 1, VoxelTextFacing::CAMERA},
             VoxelTextStyle{1, 0, VoxelTextFacing::CAMERA},
             VoxelTextStyle{1, IRRender::kMaxVoxelTextDepth + 1, VoxelTextFacing::CAMERA},
         }) {
        EXPECT_THROW(IRRender::layoutVoxelText("A", style, layout, scratch), std::runtime_error);
    }
}
#endif

TEST(VoxelText, ShadedFacesKeepTheTopAndDarkenTheSides) {
    const IRMath::Color top{200, 100, 50, 128};
    const IRRender::VoxelFaceColors faces = IRRender::shadedVoxelFaceColors(top);
    EXPECT_EQ(faces.z_.toPackedRGBA(), top.toPackedRGBA());
    EXPECT_LT(faces.y_.red_, faces.x_.red_);
    EXPECT_LT(faces.x_.red_, top.red_);
    EXPECT_EQ(faces.x_.alpha_, 128);
    EXPECT_EQ(faces.y_.alpha_, 128);
}

} // namespace
