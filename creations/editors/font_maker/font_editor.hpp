#ifndef FONT_MAKER_FONT_EDITOR_H
#define FONT_MAKER_FONT_EDITOR_H

#include "font_source.hpp"

#include <irreden/render/trixel_font.hpp>
#include <irreden/render/trixel_font_validation.hpp>

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

// The editing model behind EditorFontMaker: two loaded glyph sets (the LARGE
// slot and the SMALL slot), each a parsed header plus a mutable working copy
// of every literal's rows, and the selected character. Nothing here touches
// the ECS or the GPU.
namespace FontMaker {

enum class GlyphState : int { OK = 0, EMPTY = 1, MISSING = 2, MALFORMED = 3 };

constexpr const char *glyphStateName(GlyphState state) {
    switch (state) {
    case GlyphState::OK:
        return "ok";
    case GlyphState::EMPTY:
        return "empty";
    case GlyphState::MISSING:
        return "missing";
    case GlyphState::MALFORMED:
        return "malformed";
    }
    return "unknown";
}

// Every character the engine's text path draws with its own glyph, in table
// order: A-Z, 0-9, then the rest by ASCII. Lowercase letters fold onto the
// uppercase glyphs and an unknown byte draws '?', so neither gets a cell.
inline std::vector<char> canonicalCharacters() {
    std::vector<char> chars;
    const auto drawable = [](char c) {
        return c == '?' || IRRender::getGlyph(c) != &IRRender::kGlyph_Question;
    };
    for (char c = 'A'; c <= 'Z'; ++c) {
        chars.push_back(c);
    }
    for (char c = '0'; c <= '9'; ++c) {
        chars.push_back(c);
    }
    for (int ascii = 32; ascii < 127; ++ascii) {
        const char c = static_cast<char>(ascii);
        const bool letterOrDigit =
            (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9');
        if (!letterOrDigit && drawable(c)) {
            chars.push_back(c);
        }
    }
    return chars;
}

inline std::string characterName(char c) {
    if (c == ' ') {
        return "SPACE";
    }
    return std::string(1, c);
}

struct GlyphSet {
    FontSource source_;
    std::string loadError_;
    std::vector<std::vector<std::uint8_t>> working_;

    bool loaded() const {
        return loadError_.empty();
    }

    void resetWorking() {
        working_.clear();
        for (const GlyphLiteral &glyph : source_.glyphs_) {
            working_.push_back(glyph.rows_);
        }
    }

    // The literal a character draws with: its own case, else the switch's
    // default, else -1.
    int drawGlyph(char c) const {
        const int glyph = source_.glyphFor(c);
        return glyph >= 0 ? glyph : source_.fallbackGlyph_;
    }

    IRRender::GlyphBitmap bitmap(int glyph) const {
        return {source_.width_, source_.height_, working_[glyph]};
    }

    GlyphState state(char c) const {
        const int glyph = source_.glyphFor(c);
        if (!loaded() || glyph < 0) {
            return GlyphState::MISSING;
        }
        if (!source_.glyphs_[glyph].malformed_.empty()) {
            return GlyphState::MALFORMED;
        }
        return bitmap(glyph).empty() ? GlyphState::EMPTY : GlyphState::OK;
    }

    bool dirty(int glyph) const {
        return glyph >= 0 && working_[glyph] != source_.glyphs_[glyph].rows_;
    }
};

inline void loadGlyphSet(GlyphSet &set, const std::filesystem::path &path) {
    set.source_ = FontSource{};
    set.loadError_ = loadFontSource(path, set.source_);
    if (!set.loadError_.empty()) {
        set.source_.glyphs_.clear();
        set.source_.charGlyph_.fill(-1);
        set.source_.fallbackGlyph_ = -1;
    }
    set.resetWorking();
}

// One table cell: a character, the literal it maps to, and its state.
struct TableCell {
    char char_;
    int glyph_;
    GlyphState state_;
};

struct Editor {
    static constexpr int kLargeSlot = 0;
    static constexpr int kSmallSlot = 1;

    std::array<GlyphSet, 2> sets_;
    std::array<bool, 2> custom_{false, false};
    int active_ = kLargeSlot;
    char selected_ = 'A';
    std::vector<char> canonical_ = canonicalCharacters();
    std::string status_;

    GlyphSet &activeSet() {
        return sets_[active_];
    }
    const GlyphSet &activeSet() const {
        return sets_[active_];
    }

    int selectedGlyph() const {
        return activeSet().source_.glyphFor(selected_);
    }

    std::vector<TableCell> tableCells() const {
        std::vector<TableCell> cells;
        const GlyphSet &set = activeSet();
        for (char c : canonical_) {
            cells.push_back({c, set.source_.glyphFor(c), set.state(c)});
        }
        return cells;
    }

    // Opens `path` into the slot its glyph-set prefix selects and makes it
    // active. A header that fails to parse still occupies the active slot so
    // its error is shown.
    void openHeader(const std::filesystem::path &path, bool custom) {
        GlyphSet set;
        loadGlyphSet(set, path);
        const int slot = set.loaded() && set.source_.ruleSet() == IRRender::TextSize::SMALL
                             ? kSmallSlot
                             : (set.loaded() ? kLargeSlot : active_);
        sets_[slot] = std::move(set);
        custom_[slot] = custom;
        active_ = slot;
        status_ = sets_[slot].loaded() ? "OPENED " + path.filename().string()
                                       : "CANNOT OPEN: " + sets_[slot].loadError_;
    }

    // Flips one source bit of the selected glyph; false when it has no
    // editable literal or (col, row) is outside the cell.
    bool toggle(int col, int row) {
        GlyphSet &set = activeSet();
        const int glyph = selectedGlyph();
        if (glyph < 0 || !set.source_.glyphs_[glyph].malformed_.empty() || col < 0 || row < 0 ||
            col >= set.source_.width_ || row >= set.source_.height_) {
            return false;
        }
        set.working_[glyph][row] ^= static_cast<std::uint8_t>(1u << (set.source_.width_ - 1 - col));
        return true;
    }

    std::vector<IRRender::GlyphTexelFinding> selectedFindings() const {
        const GlyphSet &set = activeSet();
        const int glyph = selectedGlyph();
        if (glyph < 0 || !set.source_.glyphs_[glyph].malformed_.empty()) {
            return {};
        }
        return IRRender::glyphTexelFindings(set.source_.ruleSet(), set.bitmap(glyph));
    }

    // Other table characters whose literal holds the same non-empty bitmap
    // as `c`'s.
    std::vector<char> duplicatesOf(char c) const {
        const GlyphSet &set = activeSet();
        const int glyph = set.source_.glyphFor(c);
        std::vector<char> others;
        if (glyph < 0 || set.state(c) != GlyphState::OK) {
            return others;
        }
        for (char other : canonical_) {
            const int otherGlyph = set.source_.glyphFor(other);
            if (other == c || otherGlyph < 0 || set.state(other) != GlyphState::OK) {
                continue;
            }
            const IRRender::GlyphBitmap pair[] = {set.bitmap(glyph), set.bitmap(otherGlyph)};
            if (!IRRender::duplicateBitmapPairs(pair).empty()) {
                others.push_back(other);
            }
        }
        return others;
    }

    bool selectedHasFindings() const {
        return !selectedFindings().empty() || !duplicatesOf(selected_).empty();
    }

    // Saves the selected glyph into its header; false with the refusal in
    // status_ otherwise.
    bool saveSelected() {
        GlyphSet &set = activeSet();
        const int glyph = selectedGlyph();
        if (!set.loaded()) {
            status_ = "SAVE REFUSED: " + set.loadError_;
            return false;
        }
        const std::vector<std::uint8_t> rows =
            glyph >= 0 ? set.working_[glyph] : std::vector<std::uint8_t>{};
        const std::string error = saveGlyph(set.source_, glyph, rows);
        if (!error.empty()) {
            status_ = "SAVE REFUSED: " + error;
            return false;
        }
        status_ =
            "SAVED " + characterName(selected_) + " TO " + set.source_.path_.filename().string();
        return true;
    }

    void revertSelected() {
        GlyphSet &set = activeSet();
        const int glyph = selectedGlyph();
        if (glyph >= 0) {
            set.working_[glyph] = set.source_.glyphs_[glyph].rows_;
        }
    }
};

} // namespace FontMaker

#endif /* FONT_MAKER_FONT_EDITOR_H */
