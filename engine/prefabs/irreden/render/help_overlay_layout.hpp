#ifndef HELP_OVERLAY_LAYOUT_H
#define HELP_OVERLAY_LAYOUT_H

#include <irreden/ir_math.hpp>

#include <cstddef>
#include <string>
#include <string_view>

// Character-grid line layout for the help overlay's command list. Pure string
// work with no canvas or GPU dependency, so a headless test can pin it.
namespace IRPrefab::HelpOverlay {

// Fewest characters a hanging-indented continuation line may hold. A line
// budget that leaves less than this beside the body column wraps flush left
// instead of into an unreadably narrow column.
inline constexpr int kMinHangingLineChars = 8;

// A block of '\n'-terminated lines and the measurements a panel sized around
// it needs.
struct WrappedText {
    std::string text_;
    int lineCount_ = 0;
    int maxLineChars_ = 0;

    void clear() {
        text_.clear();
        lineCount_ = 0;
        maxLineChars_ = 0;
    }

    // Trailing spaces draw nothing and would overstate the measured width.
    void appendLine(std::string_view line) {
        line = line.substr(0, line.find_last_not_of(' ') + 1);
        text_ += line;
        text_ += '\n';
        ++lineCount_;
        maxLineChars_ = IRMath::max(maxLineChars_, static_cast<int>(line.size()));
    }

    // Appends one `binding  body` row: @p binding padded to @p bodyColumn
    // (always at least one space after a non-empty binding, so a longer one
    // pushes its own body right), then @p body. With @p maxLineChars > 0 no emitted line is longer
    // than that: the body breaks at spaces, continuation lines hang at
    // @p bodyColumn while at least @c kMinHangingLineChars fit beside it, and
    // a word longer than a whole line is split rather than dropped. Every
    // non-space character of both arguments is emitted, in order.
    void
    appendRow(std::string_view binding, std::string_view body, int bodyColumn, int maxLineChars) {
        std::string line(binding);
        const std::size_t gap = binding.empty() ? 0 : 1;
        line.resize(IRMath::max(binding.size() + gap, static_cast<std::size_t>(bodyColumn)), ' ');
        if (maxLineChars <= 0 || static_cast<int>(line.size() + body.size()) <= maxLineChars) {
            line += body;
            appendLine(line);
            return;
        }

        const int indent = maxLineChars - bodyColumn >= kMinHangingLineChars ? bodyColumn : 0;
        bool lineHasWord = false;
        if (static_cast<int>(line.size()) >= maxLineChars) {
            line.clear();
            placeWord(line, lineHasWord, binding, indent, maxLineChars);
        }
        std::size_t wordStart = 0;
        while (wordStart < body.size()) {
            const std::size_t wordEnd = IRMath::min(body.find(' ', wordStart), body.size());
            if (wordEnd > wordStart) {
                placeWord(
                    line,
                    lineHasWord,
                    body.substr(wordStart, wordEnd - wordStart),
                    indent,
                    maxLineChars
                );
            }
            wordStart = wordEnd + 1;
        }
        appendLine(line);
    }

  private:
    // Appends @p word to @p line, emitting @p line and starting an
    // @p indent-space continuation each time the word does not fit. Requires
    // `indent < maxLineChars`, which guarantees progress on a bare line.
    void placeWord(
        std::string &line, bool &lineHasWord, std::string_view word, int indent, int maxLineChars
    ) {
        while (true) {
            const std::size_t separator = lineHasWord ? 1 : 0;
            const int room =
                maxLineChars - static_cast<int>(line.size()) - static_cast<int>(separator);
            if (static_cast<int>(word.size()) <= room) {
                line.append(separator, ' ');
                line.append(word);
                lineHasWord = true;
                return;
            }
            // Breaking again cannot make more room on a line that holds only
            // its indent: fill it and carry the rest of the word.
            if (!lineHasWord && static_cast<int>(line.size()) <= indent) {
                const std::size_t take = static_cast<std::size_t>(maxLineChars) - line.size();
                line.append(word.substr(0, take));
                word.remove_prefix(take);
            }
            appendLine(line);
            line.assign(static_cast<std::size_t>(indent), ' ');
            lineHasWord = false;
        }
    }
};

} // namespace IRPrefab::HelpOverlay

#endif /* HELP_OVERLAY_LAYOUT_H */
