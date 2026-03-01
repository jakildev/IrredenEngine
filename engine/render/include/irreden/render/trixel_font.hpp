#ifndef TRIXEL_FONT_H
#define TRIXEL_FONT_H

#include <array>
#include <cstdint>

namespace IRRender {

constexpr int kGlyphWidth = 7;
constexpr int kGlyphHeight = 11;
constexpr int kGlyphSpacingX = 1;
constexpr int kGlyphSpacingY = 1;
constexpr int kGlyphStepX = kGlyphWidth + kGlyphSpacingX;   // 8, even -> parity-safe
constexpr int kGlyphStepY = kGlyphHeight + kGlyphSpacingY;  // 12, even -> parity-safe

using Glyph = std::array<uint8_t, kGlyphHeight>;

// Converts a visual string pattern into a packed Glyph at compile time.
// Pattern is kGlyphHeight rows of kGlyphWidth chars (77 chars total).
// 'X' or '#' = filled trixel, anything else = empty.
// Adjacent string literals auto-concatenate: "row1" "row2" "row3" ...
constexpr Glyph makeGlyph(const char* pattern) {
    Glyph g{};
    int i = 0;
    for (int row = 0; row < kGlyphHeight; ++row) {
        uint8_t rowBits = 0;
        for (int col = 0; col < kGlyphWidth; ++col) {
            if (pattern[i] == 'X' || pattern[i] == '#') {
                rowBits |= static_cast<uint8_t>(1 << (kGlyphWidth - 1 - col));
            }
            ++i;
        }
        g[row] = rowBits;
    }
    return g;
}

// --- Old hex definitions (kept for reference) ---
// A={0x1C,0x1C,0x36,0x63,0x6B,0x7F,0x77,0x63,0x63,0x63,0x63}
// B={0x7E,0x7E,0x63,0x6B,0x7E,0x77,0x63,0x63,0x63,0x7E,0x7E}
// C={0x3E,0x3E,0x73,0x60,0x60,0x60,0x60,0x60,0x73,0x3E,0x3E}
// D={0x7C,0x7C,0x66,0x63,0x63,0x63,0x63,0x63,0x66,0x7C,0x7C}
// E={0x7F,0x7F,0x60,0x60,0x68,0x7C,0x74,0x60,0x60,0x7F,0x7F}
// F={0x7F,0x7F,0x60,0x60,0x68,0x7C,0x74,0x60,0x60,0x60,0x60}
// G={0x3E,0x3E,0x73,0x60,0x60,0x67,0x63,0x63,0x77,0x3E,0x3E}
// H={0x63,0x63,0x63,0x63,0x6B,0x7F,0x77,0x63,0x63,0x63,0x63}
// I={0x3E,0x3E,0x0C,0x0C,0x0C,0x0C,0x0C,0x0C,0x0C,0x3E,0x3E}
// J={0x1F,0x1F,0x06,0x06,0x06,0x06,0x06,0x66,0x66,0x3C,0x3C}
// K={0x63,0x63,0x66,0x6C,0x78,0x78,0x6C,0x66,0x63,0x63,0x63}
// L={0x60,0x60,0x60,0x60,0x60,0x60,0x60,0x60,0x60,0x7F,0x7F}
// M={0x63,0x63,0x77,0x7F,0x6B,0x6B,0x63,0x63,0x63,0x63,0x63}
// N={0x63,0x63,0x73,0x7B,0x6F,0x67,0x63,0x63,0x63,0x63,0x63}
// O={0x3E,0x3E,0x77,0x63,0x63,0x63,0x63,0x63,0x77,0x3E,0x3E}
// P={0x7E,0x7E,0x63,0x63,0x63,0x7E,0x60,0x60,0x60,0x60,0x60}
// Q={0x3E,0x3E,0x77,0x63,0x63,0x63,0x63,0x6F,0x3E,0x06,0x06}
// R={0x7E,0x7E,0x63,0x63,0x63,0x7E,0x6C,0x66,0x63,0x63,0x63}
// S={0x3E,0x3E,0x73,0x70,0x3C,0x0F,0x07,0x03,0x77,0x3E,0x3E}
// T={0x7F,0x7F,0x3E,0x0C,0x0C,0x0C,0x0C,0x0C,0x0C,0x0C,0x0C}
// U={0x63,0x63,0x63,0x63,0x63,0x63,0x63,0x63,0x63,0x3E,0x3E}
// V={0x63,0x63,0x63,0x63,0x63,0x63,0x36,0x36,0x1C,0x1C,0x1C}
// W={0x63,0x63,0x63,0x63,0x63,0x6B,0x6B,0x7F,0x77,0x63,0x63}
// X={0x63,0x63,0x63,0x36,0x1C,0x1C,0x1C,0x36,0x63,0x63,0x63}
// Y={0x63,0x63,0x63,0x36,0x1C,0x0C,0x0C,0x0C,0x0C,0x0C,0x0C}
// Z={0x7F,0x7F,0x07,0x0E,0x1C,0x18,0x30,0x70,0x60,0x7F,0x7F}
// 0-9, symbols: see git history

// --- Letters A-Z ---

constexpr Glyph kGlyph_A = makeGlyph(
    "..XX..."
    "..XXX.."
    ".XX.XX."
    "XX...XX"
    "XX.X.XX"
    "XXXXXXX"
    "XXX.XXX"
    "XX...XX"
    "XX...XX"
    "XX...XX"
    "XX...XX"
);

constexpr Glyph kGlyph_B = makeGlyph(
    "XXXXXX."
    "XXXXXX."
    "XX...XX"
    "XX.X.XX"
    "XXXXXX."
    "XXX.XXX"
    "XX...XX"
    "XX...XX"
    "XX...XX"
    "XXXXXX."
    "XXXXXX."
);

constexpr Glyph kGlyph_C = makeGlyph(
    ".XXXXX."
    ".XXXXX."
    "XXX..XX"
    "XX....."
    "XX....."
    "XX....."
    "XX....."
    "XX....."
    "XXX..XX"
    ".XXXXX."
    ".XXXXX."
);

constexpr Glyph kGlyph_D = makeGlyph(
    "XXXXX.."
    "XXXXX.."
    "XX..XX."
    "XX...XX"
    "XX...XX"
    "XX...XX"
    "XX...XX"
    "XX...XX"
    "XX..XX."
    "XXXXX.."
    "XXXXX.."
);

constexpr Glyph kGlyph_E = makeGlyph(
    "XXXXXXX"
    "XXXXXXX"
    "XX....."
    "XX....."
    "XX.X..."
    "XXXXX.."
    "XXX.X.."
    "XX....."
    "XX....."
    "XXXXXXX"
    "XXXXXXX"
);

constexpr Glyph kGlyph_F = makeGlyph(
    "XXXXXXX"
    "XXXXXXX"
    "XX....."
    "XX....."
    "XX.X..."
    "XXXXX.."
    "XXX.X.."
    "XX....."
    "XX....."
    "XX....."
    "XX....."
);

constexpr Glyph kGlyph_G = makeGlyph(
    ".XXXXX."
    ".XXXXX."
    "XXX..XX"
    "XX....."
    "XX....."
    "XX..XXX"
    "XX...XX"
    "XX...XX"
    "XXX.XXX"
    ".XXXXX."
    ".XXXXX."
);

constexpr Glyph kGlyph_H = makeGlyph(
    "XX...XX"
    "XX...XX"
    "XX...XX"
    "XX...XX"
    "XX.X.XX"
    "XXXXXXX"
    "XXX.XXX"
    "XX...XX"
    "XX...XX"
    "XX...XX"
    "XX...XX"
);

constexpr Glyph kGlyph_I = makeGlyph(
    ".XXXXX."
    ".XXXXX."
    "...XX.."
    "...XX.."
    "...XX.."
    "...XX.."
    "...XX.."
    "...XX.."
    "...XX.."
    ".XXXXX."
    ".XXXXX."
);

constexpr Glyph kGlyph_J = makeGlyph(
    "..XXXXX"
    "..XXXXX"
    "....XX."
    "....XX."
    "....XX."
    "....XX."
    "....XX."
    "XX..XX."
    "XX..XX."
    ".XXXX.."
    ".XXXX.."
);

constexpr Glyph kGlyph_K = makeGlyph(
    "XX...XX"
    "XX...XX"
    "XX..XX."
    "XX.XX.."
    "XXXX..."
    "XXXX..."
    "XX.XX.."
    "XX..XX."
    "XX...XX"
    "XX...XX"
    "XX...XX"
);

constexpr Glyph kGlyph_L = makeGlyph(
    "XX....."
    "XX....."
    "XX....."
    "XX....."
    "XX....."
    "XX....."
    "XX....."
    "XX....."
    "XX....."
    "XXXXXXX"
    "XXXXXXX"
);

constexpr Glyph kGlyph_M = makeGlyph(
    "XX...XX"
    "XX...XX"
    "XXX.XXX"
    "XXXXXXX"
    "XX.X.XX"
    "XX.X.XX"
    "XX...XX"
    "XX...XX"
    "XX...XX"
    "XX...XX"
    "XX...XX"
);

constexpr Glyph kGlyph_N = makeGlyph(
    "XX...XX"
    "XX...XX"
    "XXX..XX"
    "XXXX.XX"
    "XX.XXXX"
    "XX..XXX"
    "XX...XX"
    "XX...XX"
    "XX...XX"
    "XX...XX"
    "XX...XX"
);

constexpr Glyph kGlyph_O = makeGlyph(
    ".XXXXX."
    ".XXXXX."
    "XXX.XXX"
    "XX...XX"
    "XX...XX"
    "XX...XX"
    "XX...XX"
    "XX...XX"
    "XXX.XXX"
    ".XXXXX."
    ".XXXXX."
);

constexpr Glyph kGlyph_P = makeGlyph(
    "XXXXXX."
    "XXXXXX."
    "XX...XX"
    "XX...XX"
    "XX...XX"
    "XXXXXX."
    "XX....."
    "XX....."
    "XX....."
    "XX....."
    "XX....."
);

constexpr Glyph kGlyph_Q = makeGlyph(
    ".XXXXX."
    ".XXXXX."
    "XXX.XXX"
    "XX...XX"
    "XX...XX"
    "XX...XX"
    "XX...XX"
    "XX.XXXX"
    ".XXXXX."
    "....XX."
    "....XX."
);

constexpr Glyph kGlyph_R = makeGlyph(
    "XXXXXX."
    "XXXXXX."
    "XX...XX"
    "XX...XX"
    "XX...XX"
    "XXXXXX."
    "XX.XX.."
    "XX..XX."
    "XX...XX"
    "XX...XX"
    "XX...XX"
);

constexpr Glyph kGlyph_S = makeGlyph(
    ".XXXXX."
    ".XXXXX."
    "XXX..XX"
    "XXX...."
    ".XXXX.."
    "...XXXX"
    "....XXX"
    ".....XX"
    "XXX.XXX"
    ".XXXXX."
    ".XXXXX."
);

constexpr Glyph kGlyph_T = makeGlyph(
    "XXXXXXX"
    "XXXXXXX"
    ".XXXXX."
    "...XX.."
    "...XX.."
    "...XX.."
    "...XX.."
    "...XX.."
    "...XX.."
    "...XX.."
    "...XX.."
);

constexpr Glyph kGlyph_U = makeGlyph(
    "XX...XX"
    "XX...XX"
    "XX...XX"
    "XX...XX"
    "XX...XX"
    "XX...XX"
    "XX...XX"
    "XX...XX"
    "XX...XX"
    ".XXXXX."
    ".XXXXX."
);

constexpr Glyph kGlyph_V = makeGlyph(
    "XX...XX"
    "XX...XX"
    "XX...XX"
    "XX...XX"
    "XX...XX"
    "XX...XX"
    ".XX.XX."
    ".XX.XX."
    "..XXX.."
    "..XXX.."
    "..XXX.."
);

constexpr Glyph kGlyph_W = makeGlyph(
    "XX...XX"
    "XX...XX"
    "XX...XX"
    "XX...XX"
    "XX...XX"
    "XX.X.XX"
    "XX.X.XX"
    "XXXXXXX"
    "XXX.XXX"
    "XX...XX"
    "XX...XX"
);

constexpr Glyph kGlyph_X = makeGlyph(
    "XX...XX"
    "XX...XX"
    "XX...XX"
    ".XX.XX."
    "..XXX.."
    "..XXX.."
    "..XXX.."
    ".XX.XX."
    "XX...XX"
    "XX...XX"
    "XX...XX"
);

constexpr Glyph kGlyph_Y = makeGlyph(
    "XX...XX"
    "XX...XX"
    "XX...XX"
    ".XX.XX."
    "..XXX.."
    "...XX.."
    "...XX.."
    "...XX.."
    "...XX.."
    "...XX.."
    "...XX.."
);

constexpr Glyph kGlyph_Z = makeGlyph(
    "XXXXXXX"
    "XXXXXXX"
    "....XXX"
    "...XXX."
    "..XXX.."
    "..XX..."
    ".XX...."
    "XXX...."
    "XX....."
    "XXXXXXX"
    "XXXXXXX"
);

// --- Digits 0-9 ---

constexpr Glyph kGlyph_0 = makeGlyph(
    ".XXXXX."
    ".XXXXX."
    "XXX.XXX"
    "XX..XXX"
    "XX.X.XX"
    "XXX..XX"
    "XX...XX"
    "XX...XX"
    "XXX.XXX"
    ".XXXXX."
    ".XXXXX."
);

constexpr Glyph kGlyph_1 = makeGlyph(
    "...XX.."
    "...XX.."
    "..XXX.."
    ".XXXX.."
    "...XX.."
    "...XX.."
    "...XX.."
    "...XX.."
    "...XX.."
    ".XXXXX."
    ".XXXXX."
);

constexpr Glyph kGlyph_2 = makeGlyph(
    ".XXXXX."
    ".XXXXX."
    "XX...XX"
    ".....XX"
    "....XX."
    "...XX.."
    "..XX..."
    ".XX...."
    "XX....."
    "XXXXXXX"
    "XXXXXXX"
);

constexpr Glyph kGlyph_3 = makeGlyph(
    ".XXXXX."
    ".XXXXX."
    "XXX.XXX"
    ".....XX"
    "....XXX"
    "..XXXX."
    "....XXX"
    ".....XX"
    "XXX.XXX"
    ".XXXXX."
    ".XXXXX."
);

constexpr Glyph kGlyph_4 = makeGlyph(
    "...XXX."
    "...XXX."
    "..XXXX."
    ".XX.XX."
    "XX..XX."
    "XX..XX."
    "XXXXXXX"
    "....XX."
    "....XX."
    "....XX."
    "....XX."
);

constexpr Glyph kGlyph_5 = makeGlyph(
    "XXXXXXX"
    "XXXXXXX"
    "XX....."
    "XX....."
    "XXXXXX."
    "....XXX"
    ".....XX"
    ".....XX"
    "XX..XXX"
    ".XXXXX."
    ".XXXXX."
);

constexpr Glyph kGlyph_6 = makeGlyph(
    ".XXXXX."
    ".XXXXX."
    "XX...XX"
    "XX....."
    "XX....."
    "XXXXXX."
    "XX...XX"
    "XX...XX"
    "XXX.XXX"
    ".XXXXX."
    ".XXXXX."
);

constexpr Glyph kGlyph_7 = makeGlyph(
    "XXXXXXX"
    "XXXXXXX"
    ".....XX"
    "....XX."
    "...XX.."
    "..XX..."
    "..XX..."
    "..XX..."
    "..XX..."
    "..XX..."
    "..XX..."
);

constexpr Glyph kGlyph_8 = makeGlyph(
    ".XXXXX."
    ".XXXXX."
    "XXX.XXX"
    "XX...XX"
    "XX...XX"
    ".XXXXX."
    "XX...XX"
    "XX...XX"
    "XXX.XXX"
    ".XXXXX."
    ".XXXXX."
);

constexpr Glyph kGlyph_9 = makeGlyph(
    ".XXXXX."
    ".XXXXX."
    "XXX.XXX"
    "XX...XX"
    "XX...XX"
    ".XXXXXX"
    ".....XX"
    ".....XX"
    "XX..XXX"
    ".XXXXX."
    ".XXXXX."
);

// --- Symbols ---

constexpr Glyph kGlyph_Space = makeGlyph(
    "......."
    "......."
    "......."
    "......."
    "......."
    "......."
    "......."
    "......."
    "......."
    "......."
    "......."
);

constexpr Glyph kGlyph_Period = makeGlyph(
    "......."
    "......."
    "......."
    "......."
    "......."
    "......."
    "......."
    "......."
    "..XX..."
    "..XX..."
    "..XX..."
);

constexpr Glyph kGlyph_Comma = makeGlyph(
    "......."
    "......."
    "......."
    "......."
    "......."
    "......."
    "......."
    "..XX..."
    "..XX..."
    ".XX...."
    ".XX...."
);

constexpr Glyph kGlyph_Colon = makeGlyph(
    "......."
    "......."
    "......."
    "..XX..."
    "..XX..."
    "......."
    "......."
    "..XX..."
    "..XX..."
    "......."
    "......."
);

constexpr Glyph kGlyph_Semicolon = makeGlyph(
    "......."
    "......."
    "......."
    "..XX..."
    "..XX..."
    "......."
    "......."
    "..XX..."
    "..XX..."
    ".XX...."
    ".XX...."
);

constexpr Glyph kGlyph_Exclamation = makeGlyph(
    "..XX..."
    "..XX..."
    "..XX..."
    "..XX..."
    "..XX..."
    "..XX..."
    "..XX..."
    "......."
    "..XX..."
    "..XX..."
    "..XX..."
);

constexpr Glyph kGlyph_Question = makeGlyph(
    ".XXXXX."
    ".XXXXX."
    "XX...XX"
    ".....XX"
    "....XX."
    "...XX.."
    "...XX.."
    "......."
    "...XX.."
    "...XX.."
    "...XX.."
);

constexpr Glyph kGlyph_Dash = makeGlyph(
    "......."
    "......."
    "......."
    "......."
    "......."
    "XXXXXXX"
    "......."
    "......."
    "......."
    "......."
    "......."
);

constexpr Glyph kGlyph_Underscore = makeGlyph(
    "......."
    "......."
    "......."
    "......."
    "......."
    "......."
    "......."
    "......."
    "......."
    "XXXXXXX"
    "XXXXXXX"
);

constexpr Glyph kGlyph_Slash = makeGlyph(
    ".....XX"
    ".....XX"
    ".....XX"
    "....XX."
    "...XX.."
    "..XX..."
    ".XX...."
    "XX....."
    "XX....."
    "......."
    "......."
);

constexpr Glyph kGlyph_Backslash = makeGlyph(
    "XX....."
    "XX....."
    "XX....."
    ".XX...."
    "..XX..."
    "...XX.."
    "....XX."
    ".....XX"
    ".....XX"
    "......."
    "......."
);

constexpr Glyph kGlyph_ParenOpen = makeGlyph(
    "...XX.."
    "...XX.."
    "..XX..."
    ".XX...."
    "XX....."
    "XX....."
    "XX....."
    ".XX...."
    "..XX..."
    "...XX.."
    "...XX.."
);

constexpr Glyph kGlyph_ParenClose = makeGlyph(
    "..XX..."
    "..XX..."
    "...XX.."
    "....XX."
    ".....XX"
    ".....XX"
    ".....XX"
    "....XX."
    "...XX.."
    "..XX..."
    "..XX..."
);

constexpr Glyph kGlyph_BracketOpen = makeGlyph(
    ".XXXX.."
    ".XXXX.."
    ".XX...."
    ".XX...."
    ".XX...."
    ".XX...."
    ".XX...."
    ".XX...."
    ".XX...."
    ".XXXX.."
    ".XXXX.."
);

constexpr Glyph kGlyph_BracketClose = makeGlyph(
    "..XXXX."
    "..XXXX."
    "....XX."
    "....XX."
    "....XX."
    "....XX."
    "....XX."
    "....XX."
    "....XX."
    "..XXXX."
    "..XXXX."
);

constexpr Glyph kGlyph_Equals = makeGlyph(
    "......."
    "......."
    "......."
    "XXXXXXX"
    "......."
    "......."
    "......."
    "XXXXXXX"
    "......."
    "......."
    "......."
);

constexpr Glyph kGlyph_Plus = makeGlyph(
    "......."
    "......."
    "...XX.."
    "...XX.."
    "...XX.."
    "XXXXXXX"
    "...XX.."
    "...XX.."
    "...XX.."
    "......."
    "......."
);

constexpr Glyph kGlyph_At = makeGlyph(
    ".XXXXX."
    ".XXXXX."
    "XX...XX"
    "XX.XXXX"
    "XX.X.XX"
    "XX.X.XX"
    "XX.XXX."
    "XX....."
    "XX..XX."
    ".XXXX.."
    ".XXXX.."
);

constexpr Glyph kGlyph_Hash = makeGlyph(
    ".XX.XX."
    ".XX.XX."
    ".XX.XX."
    "XXXXXXX"
    ".XX.XX."
    ".XX.XX."
    "XXXXXXX"
    ".XX.XX."
    ".XX.XX."
    "......."
    "......."
);

constexpr Glyph kGlyph_Asterisk = makeGlyph(
    "......."
    "......."
    "XX.X.XX"
    ".XXXXX."
    "..XXX.."
    ".XXXXX."
    "XX.X.XX"
    "......."
    "......."
    "......."
    "......."
);

constexpr Glyph kGlyph_Percent = makeGlyph(
    "XX...XX"
    "XX...XX"
    "XX..XX."
    "....XX."
    "...XX.."
    "..XX..."
    ".XX...."
    ".XX..XX"
    "XX...XX"
    "......."
    "......."
);

constexpr Glyph kGlyph_Pipe = makeGlyph(
    "...XX.."
    "...XX.."
    "...XX.."
    "...XX.."
    "...XX.."
    "...XX.."
    "...XX.."
    "...XX.."
    "...XX.."
    "...XX.."
    "...XX.."
);

// < shifted down 1 row so parity matches > for consistent diagonal appearance
constexpr Glyph kGlyph_LessThan = makeGlyph(
    "......."
    ".....X."
    ".....XX"
    "....XX."
    "...XX.."
    "..XX..."
    ".XX...."
    "..XX..."
    "...XX.."
    "....XX."
    ".....XX"
);

constexpr Glyph kGlyph_GreaterThan = makeGlyph(
    "XX....."
    "XX....."
    ".XX...."
    "..XX..."
    "...XX.."
    "....XX."
    "...XX.."
    "..XX..."
    ".XX...."
    "XX....."
    "XX....."
);

constexpr Glyph kGlyph_Tilde = makeGlyph(
    "......."
    "......."
    "......."
    "......."
    ".XXX.XX"
    "X.XXX.."
    "......."
    "......."
    "......."
    "......."
    "......."
);

constexpr Glyph kGlyph_Apostrophe = makeGlyph(
    "..XX..."
    "..XX..."
    "..XX..."
    ".XX...."
    "......."
    "......."
    "......."
    "......."
    "......."
    "......."
    "......."
);

constexpr Glyph kGlyph_Quote = makeGlyph(
    ".XX.XX."
    ".XX.XX."
    ".XX.XX."
    ".XX.XX."
    "......."
    "......."
    "......."
    "......."
    "......."
    "......."
    "......."
);

constexpr const Glyph *getGlyph(char c) {
    switch (c) {
    case 'A': case 'a': return &kGlyph_A;
    case 'B': case 'b': return &kGlyph_B;
    case 'C': case 'c': return &kGlyph_C;
    case 'D': case 'd': return &kGlyph_D;
    case 'E': case 'e': return &kGlyph_E;
    case 'F': case 'f': return &kGlyph_F;
    case 'G': case 'g': return &kGlyph_G;
    case 'H': case 'h': return &kGlyph_H;
    case 'I': case 'i': return &kGlyph_I;
    case 'J': case 'j': return &kGlyph_J;
    case 'K': case 'k': return &kGlyph_K;
    case 'L': case 'l': return &kGlyph_L;
    case 'M': case 'm': return &kGlyph_M;
    case 'N': case 'n': return &kGlyph_N;
    case 'O': case 'o': return &kGlyph_O;
    case 'P': case 'p': return &kGlyph_P;
    case 'Q': case 'q': return &kGlyph_Q;
    case 'R': case 'r': return &kGlyph_R;
    case 'S': case 's': return &kGlyph_S;
    case 'T': case 't': return &kGlyph_T;
    case 'U': case 'u': return &kGlyph_U;
    case 'V': case 'v': return &kGlyph_V;
    case 'W': case 'w': return &kGlyph_W;
    case 'X': case 'x': return &kGlyph_X;
    case 'Y': case 'y': return &kGlyph_Y;
    case 'Z': case 'z': return &kGlyph_Z;
    case '0': return &kGlyph_0;
    case '1': return &kGlyph_1;
    case '2': return &kGlyph_2;
    case '3': return &kGlyph_3;
    case '4': return &kGlyph_4;
    case '5': return &kGlyph_5;
    case '6': return &kGlyph_6;
    case '7': return &kGlyph_7;
    case '8': return &kGlyph_8;
    case '9': return &kGlyph_9;
    case ' ': return &kGlyph_Space;
    case '.': return &kGlyph_Period;
    case ',': return &kGlyph_Comma;
    case ':': return &kGlyph_Colon;
    case ';': return &kGlyph_Semicolon;
    case '!': return &kGlyph_Exclamation;
    case '?': return &kGlyph_Question;
    case '-': return &kGlyph_Dash;
    case '_': return &kGlyph_Underscore;
    case '/': return &kGlyph_Slash;
    case '\\': return &kGlyph_Backslash;
    case '(': return &kGlyph_ParenOpen;
    case ')': return &kGlyph_ParenClose;
    case '[': return &kGlyph_BracketOpen;
    case ']': return &kGlyph_BracketClose;
    case '=': return &kGlyph_Equals;
    case '+': return &kGlyph_Plus;
    case '@': return &kGlyph_At;
    case '#': return &kGlyph_Hash;
    case '*': return &kGlyph_Asterisk;
    case '%': return &kGlyph_Percent;
    case '|': return &kGlyph_Pipe;
    case '<': return &kGlyph_LessThan;
    case '>': return &kGlyph_GreaterThan;
    case '~': return &kGlyph_Tilde;
    case '\'': return &kGlyph_Apostrophe;
    case '"': return &kGlyph_Quote;
    default:  return &kGlyph_Question;
    }
}

constexpr bool glyphPixel(const Glyph &glyph, int col, int row) {
    return (glyph[row] >> (kGlyphWidth - 1 - col)) & 1;
}

} // namespace IRRender

#endif /* TRIXEL_FONT_H */
