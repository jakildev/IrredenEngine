#ifndef VOXEL_FONT_H
#define VOXEL_FONT_H

#include <irreden/render/trixel_font_small.hpp>

#include <array>
#include <cstdint>

namespace IRRender {

// The voxel glyph set: a 5x7 bitmap per character with 1-voxel strokes,
// covering exactly the characters of the trixel sets (trixel_font.hpp).
// Unlike those sets it draws on the voxel lattice, not the triangle lattice,
// so it has no parity rules: any lit pixel is one voxel column of a letter.
//   - a bitmap column is one voxel column along the letter's reading
//     direction; a bitmap row is kVoxelGlyphRowVoxels voxel rows, because one
//     reading-diagonal voxel column projects twice as wide as one voxel row
//     is tall, and two rows keep the pixels square on screen;
//   - no two characters share a bitmap.
// One column of spacing separates letters on a straight path.
constexpr int kVoxelGlyphWidth = 5;
constexpr int kVoxelGlyphHeight = 7;
constexpr int kVoxelGlyphSpacingX = 1;
constexpr int kVoxelGlyphStepX = kVoxelGlyphWidth + kVoxelGlyphSpacingX; // 6
constexpr int kVoxelGlyphRowVoxels = 2;

using VoxelGlyph = std::array<uint8_t, kVoxelGlyphHeight>;

// kVoxelGlyphHeight rows of kVoxelGlyphWidth chars; 'X' or '#' is lit.
constexpr VoxelGlyph makeVoxelGlyph(const char *pattern) {
    return detail::packGlyphPattern<kVoxelGlyphWidth, kVoxelGlyphHeight>(pattern);
}

constexpr VoxelGlyph kVoxelGlyph_A = makeVoxelGlyph(
    ".XXX."
    "X...X"
    "X...X"
    "XXXXX"
    "X...X"
    "X...X"
    "X...X"
);

constexpr VoxelGlyph kVoxelGlyph_B = makeVoxelGlyph(
    "XXXX."
    "X...X"
    "X...X"
    "XXXX."
    "X...X"
    "X...X"
    "XXXX."
);

constexpr VoxelGlyph kVoxelGlyph_C = makeVoxelGlyph(
    ".XXX."
    "X...X"
    "X...."
    "X...."
    "X...."
    "X...X"
    ".XXX."
);

constexpr VoxelGlyph kVoxelGlyph_D = makeVoxelGlyph(
    "XXX.."
    "X..X."
    "X...X"
    "X...X"
    "X...X"
    "X..X."
    "XXX.."
);

constexpr VoxelGlyph kVoxelGlyph_E = makeVoxelGlyph(
    "XXXXX"
    "X...."
    "X...."
    "XXXX."
    "X...."
    "X...."
    "XXXXX"
);

constexpr VoxelGlyph kVoxelGlyph_F = makeVoxelGlyph(
    "XXXXX"
    "X...."
    "X...."
    "XXXX."
    "X...."
    "X...."
    "X...."
);

constexpr VoxelGlyph kVoxelGlyph_G = makeVoxelGlyph(
    ".XXX."
    "X...X"
    "X...."
    "X.XXX"
    "X...X"
    "X...X"
    ".XXXX"
);

constexpr VoxelGlyph kVoxelGlyph_H = makeVoxelGlyph(
    "X...X"
    "X...X"
    "X...X"
    "XXXXX"
    "X...X"
    "X...X"
    "X...X"
);

constexpr VoxelGlyph kVoxelGlyph_I = makeVoxelGlyph(
    ".XXX."
    "..X.."
    "..X.."
    "..X.."
    "..X.."
    "..X.."
    ".XXX."
);

constexpr VoxelGlyph kVoxelGlyph_J = makeVoxelGlyph(
    "..XXX"
    "...X."
    "...X."
    "...X."
    "...X."
    "X..X."
    ".XX.."
);

constexpr VoxelGlyph kVoxelGlyph_K = makeVoxelGlyph(
    "X...X"
    "X..X."
    "X.X.."
    "XX..."
    "X.X.."
    "X..X."
    "X...X"
);

constexpr VoxelGlyph kVoxelGlyph_L = makeVoxelGlyph(
    "X...."
    "X...."
    "X...."
    "X...."
    "X...."
    "X...."
    "XXXXX"
);

constexpr VoxelGlyph kVoxelGlyph_M = makeVoxelGlyph(
    "X...X"
    "XX.XX"
    "X.X.X"
    "X.X.X"
    "X...X"
    "X...X"
    "X...X"
);

constexpr VoxelGlyph kVoxelGlyph_N = makeVoxelGlyph(
    "X...X"
    "X...X"
    "XX..X"
    "X.X.X"
    "X..XX"
    "X...X"
    "X...X"
);

constexpr VoxelGlyph kVoxelGlyph_O = makeVoxelGlyph(
    ".XXX."
    "X...X"
    "X...X"
    "X...X"
    "X...X"
    "X...X"
    ".XXX."
);

constexpr VoxelGlyph kVoxelGlyph_P = makeVoxelGlyph(
    "XXXX."
    "X...X"
    "X...X"
    "XXXX."
    "X...."
    "X...."
    "X...."
);

constexpr VoxelGlyph kVoxelGlyph_Q = makeVoxelGlyph(
    ".XXX."
    "X...X"
    "X...X"
    "X...X"
    "X.X.X"
    "X..X."
    ".XX.X"
);

constexpr VoxelGlyph kVoxelGlyph_R = makeVoxelGlyph(
    "XXXX."
    "X...X"
    "X...X"
    "XXXX."
    "X.X.."
    "X..X."
    "X...X"
);

constexpr VoxelGlyph kVoxelGlyph_S = makeVoxelGlyph(
    ".XXXX"
    "X...."
    "X...."
    ".XXX."
    "....X"
    "....X"
    "XXXX."
);

constexpr VoxelGlyph kVoxelGlyph_T = makeVoxelGlyph(
    "XXXXX"
    "..X.."
    "..X.."
    "..X.."
    "..X.."
    "..X.."
    "..X.."
);

constexpr VoxelGlyph kVoxelGlyph_U = makeVoxelGlyph(
    "X...X"
    "X...X"
    "X...X"
    "X...X"
    "X...X"
    "X...X"
    ".XXX."
);

constexpr VoxelGlyph kVoxelGlyph_V = makeVoxelGlyph(
    "X...X"
    "X...X"
    "X...X"
    "X...X"
    "X...X"
    ".X.X."
    "..X.."
);

constexpr VoxelGlyph kVoxelGlyph_W = makeVoxelGlyph(
    "X...X"
    "X...X"
    "X...X"
    "X.X.X"
    "X.X.X"
    "X.X.X"
    ".X.X."
);

constexpr VoxelGlyph kVoxelGlyph_X = makeVoxelGlyph(
    "X...X"
    "X...X"
    ".X.X."
    "..X.."
    ".X.X."
    "X...X"
    "X...X"
);

constexpr VoxelGlyph kVoxelGlyph_Y = makeVoxelGlyph(
    "X...X"
    "X...X"
    ".X.X."
    "..X.."
    "..X.."
    "..X.."
    "..X.."
);

constexpr VoxelGlyph kVoxelGlyph_Z = makeVoxelGlyph(
    "XXXXX"
    "....X"
    "...X."
    "..X.."
    ".X..."
    "X...."
    "XXXXX"
);

constexpr VoxelGlyph kVoxelGlyph_0 = makeVoxelGlyph(
    ".XXX."
    "X...X"
    "X..XX"
    "X.X.X"
    "XX..X"
    "X...X"
    ".XXX."
);

constexpr VoxelGlyph kVoxelGlyph_1 = makeVoxelGlyph(
    "..X.."
    ".XX.."
    "..X.."
    "..X.."
    "..X.."
    "..X.."
    ".XXX."
);

constexpr VoxelGlyph kVoxelGlyph_2 = makeVoxelGlyph(
    ".XXX."
    "X...X"
    "....X"
    "...X."
    "..X.."
    ".X..."
    "XXXXX"
);

constexpr VoxelGlyph kVoxelGlyph_3 = makeVoxelGlyph(
    "XXXXX"
    "...X."
    "..X.."
    "...X."
    "....X"
    "X...X"
    ".XXX."
);

constexpr VoxelGlyph kVoxelGlyph_4 = makeVoxelGlyph(
    "...X."
    "..XX."
    ".X.X."
    "X..X."
    "XXXXX"
    "...X."
    "...X."
);

constexpr VoxelGlyph kVoxelGlyph_5 = makeVoxelGlyph(
    "XXXXX"
    "X...."
    "XXXX."
    "....X"
    "....X"
    "X...X"
    ".XXX."
);

constexpr VoxelGlyph kVoxelGlyph_6 = makeVoxelGlyph(
    "..XX."
    ".X..."
    "X...."
    "XXXX."
    "X...X"
    "X...X"
    ".XXX."
);

constexpr VoxelGlyph kVoxelGlyph_7 = makeVoxelGlyph(
    "XXXXX"
    "....X"
    "...X."
    "..X.."
    ".X..."
    ".X..."
    ".X..."
);

constexpr VoxelGlyph kVoxelGlyph_8 = makeVoxelGlyph(
    ".XXX."
    "X...X"
    "X...X"
    ".XXX."
    "X...X"
    "X...X"
    ".XXX."
);

constexpr VoxelGlyph kVoxelGlyph_9 = makeVoxelGlyph(
    ".XXX."
    "X...X"
    "X...X"
    ".XXXX"
    "....X"
    "...X."
    ".XX.."
);

constexpr VoxelGlyph kVoxelGlyph_Space = makeVoxelGlyph(
    "....."
    "....."
    "....."
    "....."
    "....."
    "....."
    "....."
);

constexpr VoxelGlyph kVoxelGlyph_Period = makeVoxelGlyph(
    "....."
    "....."
    "....."
    "....."
    "....."
    ".XX.."
    ".XX.."
);

constexpr VoxelGlyph kVoxelGlyph_Comma = makeVoxelGlyph(
    "....."
    "....."
    "....."
    "....."
    ".XX.."
    "..X.."
    ".X..."
);

constexpr VoxelGlyph kVoxelGlyph_Colon = makeVoxelGlyph(
    "....."
    ".XX.."
    ".XX.."
    "....."
    ".XX.."
    ".XX.."
    "....."
);

constexpr VoxelGlyph kVoxelGlyph_Semicolon = makeVoxelGlyph(
    "....."
    ".XX.."
    ".XX.."
    "....."
    ".XX.."
    "..X.."
    ".X..."
);

constexpr VoxelGlyph kVoxelGlyph_Exclamation = makeVoxelGlyph(
    "..X.."
    "..X.."
    "..X.."
    "..X.."
    "..X.."
    "....."
    "..X.."
);

constexpr VoxelGlyph kVoxelGlyph_Question = makeVoxelGlyph(
    ".XXX."
    "X...X"
    "....X"
    "...X."
    "..X.."
    "....."
    "..X.."
);

constexpr VoxelGlyph kVoxelGlyph_Dash = makeVoxelGlyph(
    "....."
    "....."
    "....."
    "XXXXX"
    "....."
    "....."
    "....."
);

constexpr VoxelGlyph kVoxelGlyph_Underscore = makeVoxelGlyph(
    "....."
    "....."
    "....."
    "....."
    "....."
    "....."
    "XXXXX"
);

constexpr VoxelGlyph kVoxelGlyph_Slash = makeVoxelGlyph(
    "....."
    "....X"
    "...X."
    "..X.."
    ".X..."
    "X...."
    "....."
);

constexpr VoxelGlyph kVoxelGlyph_Backslash = makeVoxelGlyph(
    "....."
    "X...."
    ".X..."
    "..X.."
    "...X."
    "....X"
    "....."
);

constexpr VoxelGlyph kVoxelGlyph_ParenOpen = makeVoxelGlyph(
    "...X."
    "..X.."
    ".X..."
    ".X..."
    ".X..."
    "..X.."
    "...X."
);

constexpr VoxelGlyph kVoxelGlyph_ParenClose = makeVoxelGlyph(
    ".X..."
    "..X.."
    "...X."
    "...X."
    "...X."
    "..X.."
    ".X..."
);

constexpr VoxelGlyph kVoxelGlyph_BracketOpen = makeVoxelGlyph(
    ".XXX."
    ".X..."
    ".X..."
    ".X..."
    ".X..."
    ".X..."
    ".XXX."
);

constexpr VoxelGlyph kVoxelGlyph_BracketClose = makeVoxelGlyph(
    ".XXX."
    "...X."
    "...X."
    "...X."
    "...X."
    "...X."
    ".XXX."
);

constexpr VoxelGlyph kVoxelGlyph_Equals = makeVoxelGlyph(
    "....."
    "....."
    "XXXXX"
    "....."
    "XXXXX"
    "....."
    "....."
);

constexpr VoxelGlyph kVoxelGlyph_Plus = makeVoxelGlyph(
    "....."
    "..X.."
    "..X.."
    "XXXXX"
    "..X.."
    "..X.."
    "....."
);

constexpr VoxelGlyph kVoxelGlyph_At = makeVoxelGlyph(
    ".XXX."
    "X...X"
    "....X"
    ".XX.X"
    "X.X.X"
    "X.X.X"
    ".XXX."
);

constexpr VoxelGlyph kVoxelGlyph_Hash = makeVoxelGlyph(
    ".X.X."
    ".X.X."
    "XXXXX"
    ".X.X."
    "XXXXX"
    ".X.X."
    ".X.X."
);

constexpr VoxelGlyph kVoxelGlyph_Asterisk = makeVoxelGlyph(
    "....."
    "..X.."
    "X.X.X"
    ".XXX."
    "X.X.X"
    "..X.."
    "....."
);

constexpr VoxelGlyph kVoxelGlyph_Percent = makeVoxelGlyph(
    "XX..."
    "XX..X"
    "...X."
    "..X.."
    ".X..."
    "X..XX"
    "...XX"
);

constexpr VoxelGlyph kVoxelGlyph_Pipe = makeVoxelGlyph(
    "..X.."
    "..X.."
    "..X.."
    "..X.."
    "..X.."
    "..X.."
    "..X.."
);

constexpr VoxelGlyph kVoxelGlyph_LessThan = makeVoxelGlyph(
    "...X."
    "..X.."
    ".X..."
    "X...."
    ".X..."
    "..X.."
    "...X."
);

constexpr VoxelGlyph kVoxelGlyph_GreaterThan = makeVoxelGlyph(
    ".X..."
    "..X.."
    "...X."
    "....X"
    "...X."
    "..X.."
    ".X..."
);

constexpr VoxelGlyph kVoxelGlyph_Tilde = makeVoxelGlyph(
    "....."
    "....."
    ".X..."
    "X.X.X"
    "...X."
    "....."
    "....."
);

constexpr VoxelGlyph kVoxelGlyph_Apostrophe = makeVoxelGlyph(
    "..X.."
    "..X.."
    ".X..."
    "....."
    "....."
    "....."
    "....."
);

constexpr VoxelGlyph kVoxelGlyph_Quote = makeVoxelGlyph(
    ".X.X."
    ".X.X."
    ".X.X."
    "....."
    "....."
    "....."
    "....."
);

// Lowercase folds to uppercase; a character the set lacks draws as '?'.
// clang-format off
constexpr const VoxelGlyph *getVoxelGlyph(char c) {
    switch (c) {
    case 'A': case 'a': return &kVoxelGlyph_A;
    case 'B': case 'b': return &kVoxelGlyph_B;
    case 'C': case 'c': return &kVoxelGlyph_C;
    case 'D': case 'd': return &kVoxelGlyph_D;
    case 'E': case 'e': return &kVoxelGlyph_E;
    case 'F': case 'f': return &kVoxelGlyph_F;
    case 'G': case 'g': return &kVoxelGlyph_G;
    case 'H': case 'h': return &kVoxelGlyph_H;
    case 'I': case 'i': return &kVoxelGlyph_I;
    case 'J': case 'j': return &kVoxelGlyph_J;
    case 'K': case 'k': return &kVoxelGlyph_K;
    case 'L': case 'l': return &kVoxelGlyph_L;
    case 'M': case 'm': return &kVoxelGlyph_M;
    case 'N': case 'n': return &kVoxelGlyph_N;
    case 'O': case 'o': return &kVoxelGlyph_O;
    case 'P': case 'p': return &kVoxelGlyph_P;
    case 'Q': case 'q': return &kVoxelGlyph_Q;
    case 'R': case 'r': return &kVoxelGlyph_R;
    case 'S': case 's': return &kVoxelGlyph_S;
    case 'T': case 't': return &kVoxelGlyph_T;
    case 'U': case 'u': return &kVoxelGlyph_U;
    case 'V': case 'v': return &kVoxelGlyph_V;
    case 'W': case 'w': return &kVoxelGlyph_W;
    case 'X': case 'x': return &kVoxelGlyph_X;
    case 'Y': case 'y': return &kVoxelGlyph_Y;
    case 'Z': case 'z': return &kVoxelGlyph_Z;
    case '0': return &kVoxelGlyph_0;
    case '1': return &kVoxelGlyph_1;
    case '2': return &kVoxelGlyph_2;
    case '3': return &kVoxelGlyph_3;
    case '4': return &kVoxelGlyph_4;
    case '5': return &kVoxelGlyph_5;
    case '6': return &kVoxelGlyph_6;
    case '7': return &kVoxelGlyph_7;
    case '8': return &kVoxelGlyph_8;
    case '9': return &kVoxelGlyph_9;
    case ' ': return &kVoxelGlyph_Space;
    case '.': return &kVoxelGlyph_Period;
    case ',': return &kVoxelGlyph_Comma;
    case ':': return &kVoxelGlyph_Colon;
    case ';': return &kVoxelGlyph_Semicolon;
    case '!': return &kVoxelGlyph_Exclamation;
    case '?': return &kVoxelGlyph_Question;
    case '-': return &kVoxelGlyph_Dash;
    case '_': return &kVoxelGlyph_Underscore;
    case '/': return &kVoxelGlyph_Slash;
    case '\\': return &kVoxelGlyph_Backslash;
    case '(': return &kVoxelGlyph_ParenOpen;
    case ')': return &kVoxelGlyph_ParenClose;
    case '[': return &kVoxelGlyph_BracketOpen;
    case ']': return &kVoxelGlyph_BracketClose;
    case '=': return &kVoxelGlyph_Equals;
    case '+': return &kVoxelGlyph_Plus;
    case '@': return &kVoxelGlyph_At;
    case '#': return &kVoxelGlyph_Hash;
    case '*': return &kVoxelGlyph_Asterisk;
    case '%': return &kVoxelGlyph_Percent;
    case '|': return &kVoxelGlyph_Pipe;
    case '<': return &kVoxelGlyph_LessThan;
    case '>': return &kVoxelGlyph_GreaterThan;
    case '~': return &kVoxelGlyph_Tilde;
    case '\'': return &kVoxelGlyph_Apostrophe;
    case '"': return &kVoxelGlyph_Quote;
    default:  return &kVoxelGlyph_Question;
    }
}
// clang-format on

// Pixel (col, row) of a glyph, row 0 at the top and col 0 at the left.
constexpr bool voxelGlyphPixel(const VoxelGlyph &glyph, int col, int row) {
    return ((glyph[row] >> (kVoxelGlyphWidth - 1 - col)) & 1u) != 0u;
}

constexpr bool voxelGlyphIsBlank(const VoxelGlyph &glyph) {
    for (const uint8_t row : glyph) {
        if (row != 0) {
            return false;
        }
    }
    return true;
}

} // namespace IRRender

#endif /* VOXEL_FONT_H */
