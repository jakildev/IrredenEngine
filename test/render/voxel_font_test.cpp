#include <gtest/gtest.h>

#include <irreden/render/trixel_font.hpp>
#include <irreden/render/voxel_font.hpp>

#include <vector>

namespace {

static_assert(IRRender::kVoxelGlyphStepX == 6);
static_assert(IRRender::kVoxelGlyphWidth == 5 && IRRender::kVoxelGlyphHeight == 7);

// Glyphs compare by value: each namespace-scope constexpr glyph has internal
// linkage, so its address differs between translation units.
bool trixelHasGlyph(char c) {
    return c == '?' || *IRRender::getGlyph(c) != IRRender::kGlyph_Question;
}

bool voxelHasGlyph(char c) {
    return c == '?' || *IRRender::getVoxelGlyph(c) != IRRender::kVoxelGlyph_Question;
}

// One entry per character the set covers, each the first byte reaching it.
std::vector<char> coveredCharacters() {
    std::vector<char> characters;
    for (int ascii = 0; ascii < 128; ++ascii) {
        const char c = static_cast<char>(ascii);
        if (voxelHasGlyph(c) && !(c >= 'a' && c <= 'z')) {
            characters.push_back(c);
        }
    }
    return characters;
}

TEST(VoxelFont, CoversExactlyTheTrixelFontsCharacters) {
    int covered = 0;
    for (int ascii = 0; ascii < 128; ++ascii) {
        const char c = static_cast<char>(ascii);
        EXPECT_EQ(voxelHasGlyph(c), trixelHasGlyph(c)) << "character " << ascii;
        covered += voxelHasGlyph(c) ? 1 : 0;
    }
    EXPECT_EQ(covered, 63 + 26) << "63 glyphs, 26 of them also reached by a lowercase letter";
}

TEST(VoxelFont, NoTwoCharactersShareABitmap) {
    const std::vector<char> characters = coveredCharacters();
    ASSERT_EQ(characters.size(), 63u);
    for (std::size_t i = 0; i < characters.size(); ++i) {
        for (std::size_t j = i + 1; j < characters.size(); ++j) {
            EXPECT_NE(
                *IRRender::getVoxelGlyph(characters[i]),
                *IRRender::getVoxelGlyph(characters[j])
            ) << "'"
              << characters[i] << "' and '" << characters[j] << "'";
        }
    }
    for (char lower = 'a'; lower <= 'z'; ++lower) {
        EXPECT_EQ(
            *IRRender::getVoxelGlyph(lower),
            *IRRender::getVoxelGlyph(static_cast<char>(lower - 'a' + 'A'))
        ) << lower;
    }
}

TEST(VoxelFont, SpaceIsTheOnlyBlankGlyph) {
    for (const char c : coveredCharacters()) {
        EXPECT_EQ(IRRender::voxelGlyphIsBlank(*IRRender::getVoxelGlyph(c)), c == ' ')
            << "'" << c << "'";
    }
}

TEST(VoxelFont, UnmappedBytesDrawTheQuestionMark) {
    for (const char c : {'\0', '\t', '\n', '$', '^', '`', '{', '}', '\x7f'}) {
        EXPECT_EQ(*IRRender::getVoxelGlyph(c), IRRender::kVoxelGlyph_Question)
            << static_cast<int>(c);
    }
}

TEST(VoxelFont, PixelsReadLeftToRightTopToBottom) {
    const IRRender::VoxelGlyph &l = IRRender::kVoxelGlyph_L;
    EXPECT_TRUE(IRRender::voxelGlyphPixel(l, 0, 0));
    EXPECT_FALSE(IRRender::voxelGlyphPixel(l, 4, 0));
    EXPECT_TRUE(IRRender::voxelGlyphPixel(l, 4, IRRender::kVoxelGlyphHeight - 1));
}

} // namespace
