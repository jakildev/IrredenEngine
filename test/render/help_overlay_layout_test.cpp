#include <gtest/gtest.h>

#include <irreden/render/help_overlay_layout.hpp>

#include <cstddef>
#include <string>
#include <vector>

// The help overlay wraps its command rows to the GUI canvas. These pin the
// layout's two promises: no emitted line exceeds the budget, and no character
// of a row is lost to the wrap.

namespace {

using IRPrefab::HelpOverlay::WrappedText;

constexpr int kBodyColumn = 16;

std::vector<std::string> linesOf(const WrappedText &wrapped) {
    std::vector<std::string> lines;
    std::size_t start = 0;
    while (start < wrapped.text_.size()) {
        const std::size_t end = wrapped.text_.find('\n', start);
        lines.push_back(wrapped.text_.substr(start, end - start));
        start = end + 1;
    }
    return lines;
}

std::string withoutSpaces(const std::string &text) {
    std::string out;
    for (const char c : text) {
        if (c != ' ' && c != '\n') {
            out += c;
        }
    }
    return out;
}

TEST(HelpOverlayLayoutTest, UnlimitedBudgetKeepsTheRowOnOneAlignedLine) {
    WrappedText wrapped;
    wrapped.appendRow("W", "CAMERA UP - PAN THE CAMERA UP WHILE HELD", kBodyColumn, 0);

    ASSERT_EQ(wrapped.lineCount_, 1);
    EXPECT_EQ(wrapped.text_, "W               CAMERA UP - PAN THE CAMERA UP WHILE HELD\n");
    EXPECT_EQ(wrapped.maxLineChars_, 56);
}

TEST(HelpOverlayLayoutTest, RowThatFitsTheBudgetIsNotWrapped) {
    WrappedText wrapped;
    wrapped.appendRow("F1", "TOGGLE HELP", kBodyColumn, 27);

    EXPECT_EQ(wrapped.text_, "F1              TOGGLE HELP\n");
}

// The overlay's header row: no binding, no column, so nothing indents it.
TEST(HelpOverlayLayoutTest, EmptyBindingAtColumnZeroStartsFlushLeft) {
    WrappedText wrapped;
    wrapped.appendRow("", "COMMANDS", 0, 30);

    EXPECT_EQ(wrapped.text_, "COMMANDS\n");
}

TEST(HelpOverlayLayoutTest, WrappedRowHangsAtTheBodyColumnWithinTheBudget) {
    constexpr int kBudget = 30;
    const std::string body = "CAMERA UP - PAN THE CAMERA UP WHILE HELD";
    WrappedText wrapped;
    wrapped.appendRow("W", body, kBodyColumn, kBudget);

    const std::vector<std::string> lines = linesOf(wrapped);
    ASSERT_EQ(static_cast<int>(lines.size()), wrapped.lineCount_);
    ASSERT_GT(lines.size(), 1u);
    EXPECT_EQ(lines[0], "W               CAMERA UP -");
    for (std::size_t i = 0; i < lines.size(); ++i) {
        EXPECT_LE(static_cast<int>(lines[i].size()), kBudget) << "line " << i;
        if (i > 0) {
            EXPECT_EQ(lines[i].substr(0, kBodyColumn), std::string(kBodyColumn, ' '))
                << "line " << i;
            EXPECT_NE(lines[i][kBodyColumn], ' ') << "line " << i;
        }
    }
    EXPECT_LE(wrapped.maxLineChars_, kBudget);
    EXPECT_EQ(withoutSpaces(wrapped.text_), "W" + withoutSpaces(body));
}

// Below kMinHangingLineChars beside the column, a hanging indent would leave
// a column too narrow to read, so continuation lines start at the left edge.
TEST(HelpOverlayLayoutTest, NarrowBudgetWrapsFlushLeft) {
    constexpr int kBudget = kBodyColumn + IRPrefab::HelpOverlay::kMinHangingLineChars - 1;
    const std::string body = "TOGGLE SETTINGS - OPEN OR CLOSE THE SETTINGS MENU";
    WrappedText wrapped;
    wrapped.appendRow("ESC", body, kBodyColumn, kBudget);

    const std::vector<std::string> lines = linesOf(wrapped);
    ASSERT_GT(lines.size(), 1u);
    for (std::size_t i = 0; i < lines.size(); ++i) {
        EXPECT_LE(static_cast<int>(lines[i].size()), kBudget) << "line " << i;
        if (i > 0) {
            EXPECT_NE(lines[i][0], ' ') << "line " << i;
        }
    }
    EXPECT_EQ(withoutSpaces(wrapped.text_), "ESC" + withoutSpaces(body));
}

TEST(HelpOverlayLayoutTest, WordLongerThanALineIsSplitNotDropped) {
    constexpr int kBudget = 26;
    const std::string body = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 END";
    WrappedText wrapped;
    wrapped.appendRow("F9", body, kBodyColumn, kBudget);

    for (const std::string &line : linesOf(wrapped)) {
        EXPECT_LE(static_cast<int>(line.size()), kBudget);
    }
    EXPECT_EQ(withoutSpaces(wrapped.text_), "F9" + withoutSpaces(body));
}

// A binding as wide as the whole line cannot share it with any body text.
TEST(HelpOverlayLayoutTest, BindingWiderThanTheBudgetWrapsLosslessly) {
    constexpr int kBudget = 10;
    WrappedText wrapped;
    wrapped.appendRow("CTRL+SHIFT+BACKSPACE", "DELETE WORD", kBodyColumn, kBudget);

    const std::vector<std::string> lines = linesOf(wrapped);
    ASSERT_GT(lines.size(), 2u);
    for (const std::string &line : lines) {
        EXPECT_LE(static_cast<int>(line.size()), kBudget);
        EXPECT_FALSE(line.empty());
    }
    EXPECT_EQ(withoutSpaces(wrapped.text_), "CTRL+SHIFT+BACKSPACEDELETEWORD");
}

TEST(HelpOverlayLayoutTest, ClearResetsTheMeasurements) {
    WrappedText wrapped;
    wrapped.appendRow("W", "CAMERA UP", kBodyColumn, 0);
    wrapped.appendLine("");
    ASSERT_EQ(wrapped.lineCount_, 2);

    wrapped.clear();

    EXPECT_TRUE(wrapped.text_.empty());
    EXPECT_EQ(wrapped.lineCount_, 0);
    EXPECT_EQ(wrapped.maxLineChars_, 0);
}

} // namespace
