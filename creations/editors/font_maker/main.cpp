#include <irreden/ir_engine.hpp>
#include <irreden/ir_input.hpp>
#include <irreden/ir_render.hpp>
#include <irreden/ir_system.hpp>
#include <irreden/ir_video.hpp>

#include <irreden/input/systems/system_input_key_mouse.hpp>
#include <irreden/input/systems/system_hitbox_mouse_test_gui.hpp>
#include <irreden/update/systems/system_propagate_transform.hpp>
#include <irreden/render/systems/system_voxel_to_trixel.hpp>
#include <irreden/render/systems/system_text_to_trixel.hpp>
#include <irreden/render/systems/system_trixel_to_framebuffer.hpp>
#include <irreden/render/systems/system_framebuffer_to_screen.hpp>
#include <irreden/render/systems/system_gui_canvas_to_screen.hpp>
#include <irreden/render/systems/system_widget_input.hpp>
#include <irreden/render/systems/system_widget_apply_text_input.hpp>
#include <irreden/render/systems/system_widget_render_panel.hpp>
#include <irreden/render/systems/system_widget_render_label.hpp>
#include <irreden/render/systems/system_widget_render_button.hpp>
#include <irreden/render/systems/system_widget_render_text_input.hpp>
#include <irreden/render/gui_test_assertions.hpp>
#include <irreden/render/help_overlay.hpp>
#include <irreden/render/perf_hud.hpp>
#include <irreden/common/command_suite_capture.hpp>
#include <irreden/input/commands/command_close_window.hpp>
#include <irreden/render/commands/command_gui_zoom.hpp>

#include "font_editor.hpp"
#include "systems/system_font_maker.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <iterator>
#include <list>
#include <span>
#include <string>
#include <vector>

namespace {

namespace fs = std::filesystem;
using FontMaker::Editor;
using FontMaker::GlyphState;
using IRVideo::GuiInputEvent;

FontMaker::App g_app;

// Scripted sessions selected by --font-maker-test. Each drives the editor
// through injected clicks and direct model mutations and asserts what the
// editor drew and reports.
enum class Session { NONE, TABLE, EDIT, SAMPLE };
Session g_session = Session::NONE;
int g_setupShot = -1;
std::uint64_t g_previewBaseline = 0;
IRPrefab::GuiTest::LatchState g_latch;

constexpr const char *kSampleText = "HELLO, WORLD 2468?";
constexpr int kToggleCol = 2;
constexpr int kToggleRow = 5;

GuiInputEvent g_selectEvents[] = {
    {1, GuiInputEvent::Type::MOVE, {}, {}, IRInput::kNullButton},
    {2, GuiInputEvent::Type::PRESS, {}, {}, IRInput::kMouseButtonLeft},
    {3, GuiInputEvent::Type::RELEASE, {}, {}, IRInput::kMouseButtonLeft},
};
GuiInputEvent g_toggleEvents[] = {
    {1, GuiInputEvent::Type::MOVE, {}, {}, IRInput::kNullButton},
    {2, GuiInputEvent::Type::PRESS, {}, {}, IRInput::kMouseButtonLeft},
    {3, GuiInputEvent::Type::RELEASE, {}, {}, IRInput::kMouseButtonLeft},
};

void aimClick(GuiInputEvent (&events)[3], IRMath::ivec2 texel) {
    const IRMath::ivec2 px =
        IRRender::guiTrixelToScreenPx(IRMath::vec2(texel) + IRMath::vec2(0.5f));
    for (GuiInputEvent &event : events) {
        event.screenPx_ = px;
    }
}

constexpr IRVideo::GuiTestShot shot(const char *label) {
    return {{1.0f, IRMath::vec2(0.0f), 0.0f, label}, nullptr, 0};
}

constexpr IRVideo::GuiTestShot kIdleShots[] = {shot("font_maker_idle")};

constexpr IRVideo::GuiTestShot kTableShots[] = {
    shot("table_large"),
    shot("table_small"),
    shot("table_missing_fixture"),
};

const IRVideo::GuiTestShot kEditShots[] = {
    {{1.0f, IRMath::vec2(0.0f), 0.0f, "edit_select_space"}, g_selectEvents, 3},
    {{1.0f, IRMath::vec2(0.0f), 0.0f, "edit_toggle_texel"}, g_toggleEvents, 3},
    shot("edit_grid_parity"),
    shot("edit_wrong_parity"),
    shot("edit_duplicate"),
};

constexpr IRVideo::GuiTestShot kSampleShots[] = {
    shot("sample_screen_scale1"),
    shot("sample_screen_scale2"),
    shot("sample_pixelated_scale1"),
    shot("sample_pixelated_scale2"),
};

std::span<const IRVideo::GuiTestShot> sessionShots() {
    switch (g_session) {
    case Session::TABLE:
        return kTableShots;
    case Session::EDIT:
        return kEditShots;
    case Session::SAMPLE:
        return kSampleShots;
    case Session::NONE:
        break;
    }
    return kIdleShots;
}

// --- Predicates ------------------------------------------------------------

bool uniqueGlyphCells(const void *, std::string &actual) {
    const std::vector<FontMaker::TableCell> cells = g_app.editor_.tableCells();
    std::vector<char> seen;
    for (const FontMaker::TableCell &cell : cells) {
        if (std::find(seen.begin(), seen.end(), cell.char_) == seen.end()) {
            seen.push_back(cell.char_);
        }
    }
    const int lowercase = static_cast<int>(std::count_if(seen.begin(), seen.end(), [](char c) {
        return c >= 'a' && c <= 'z';
    }));
    actual = "unique_glyph_cells=" + std::to_string(seen.size()) +
             " cells=" + std::to_string(cells.size()) + " lowercase=" + std::to_string(lowercase);
    return seen.size() == 63 && cells.size() == 63 && lowercase == 0;
}

struct CellExpect {
    char char_;
    GlyphState state_;
};

bool cellState(const void *context, std::string &actual) {
    const auto &expect = *static_cast<const CellExpect *>(context);
    const std::vector<FontMaker::TableCell> cells = g_app.editor_.tableCells();
    const int index = FontMaker::tableIndexOf(g_app.editor_, expect.char_);
    const GlyphState state = index >= 0 ? cells[index].state_ : GlyphState::MISSING;
    actual = "cell=" + std::to_string(index) + " char=" + FontMaker::characterName(expect.char_) +
             " state=" + FontMaker::glyphStateName(state);
    return index >= 0 && state == expect.state_;
}

bool missingCellCount(const void *context, std::string &actual) {
    const int expected = *static_cast<const int *>(context);
    int missing = 0;
    std::string which;
    for (const FontMaker::TableCell &cell : g_app.editor_.tableCells()) {
        if (cell.state_ == GlyphState::MISSING) {
            ++missing;
            which += FontMaker::characterName(cell.char_);
        }
    }
    actual = "missing_cells=" + std::to_string(missing) + " chars=" + (which.empty() ? "-" : which);
    return missing == expected;
}

bool selectedIsSpace(const void *, std::string &actual) {
    actual = "selected=" + FontMaker::characterName(g_app.editor_.selected_);
    return g_app.editor_.selected_ == ' ';
}

// Every bit that differs between the working and loaded bitmaps across the
// active set, as "<literal>@col,row".
std::vector<std::string> changedBits() {
    const FontMaker::GlyphSet &set = g_app.editor_.activeSet();
    std::vector<std::string> changed;
    for (std::size_t glyph = 0; glyph < set.working_.size(); ++glyph) {
        for (int row = 0; row < set.source_.height_; ++row) {
            const std::uint8_t diff =
                set.working_[glyph][row] ^ set.source_.glyphs_[glyph].rows_[row];
            for (int col = 0; col < set.source_.width_; ++col) {
                if (IRRender::glyphRowPixel(diff, set.source_.width_, col)) {
                    changed.push_back(
                        set.source_.glyphs_[glyph].name_ + "@" + std::to_string(col) + "," +
                        std::to_string(row)
                    );
                }
            }
        }
    }
    return changed;
}

bool oneSourceBitChanged(const void *, std::string &actual) {
    const std::vector<std::string> changed = changedBits();
    actual = "changed_bits=" + std::to_string(changed.size());
    for (const std::string &bit : changed) {
        actual += " " + bit;
    }
    const std::string expected =
        "Space@" + std::to_string(kToggleCol) + "," + std::to_string(kToggleRow);
    return changed.size() == 1 && changed[0] == expected;
}

bool previewChanged(const void *, std::string &actual) {
    const std::uint64_t now = FontMaker::sampleStats(g_app.frame_.sample_).largeHash_;
    actual = "baseline=" + std::to_string(g_previewBaseline) + " now=" + std::to_string(now);
    return g_previewBaseline != 0 && now != g_previewBaseline;
}

struct FindingExpect {
    IRRender::GlyphRule rule_;
    int col_;
    int row_;
    bool exclusive_;
};

bool gridFinding(const void *context, std::string &actual) {
    const auto &expect = *static_cast<const FindingExpect *>(context);
    const std::vector<IRRender::GlyphTexelFinding> &findings = g_app.frame_.findings_;
    bool found = false;
    actual = "findings=" + std::to_string(findings.size());
    for (const IRRender::GlyphTexelFinding &finding : findings) {
        actual += std::string(" ") + IRRender::glyphRuleName(finding.rule_) + "@" +
                  std::to_string(finding.col_) + "," + std::to_string(finding.row_);
        found = found || (finding.rule_ == expect.rule_ && finding.col_ == expect.col_ &&
                          finding.row_ == expect.row_);
    }
    const IRMath::ivec2 aim = FontMaker::gridAimTexel(g_app.layout_, expect.col_, expect.row_);
    const IRMath::Color drawn =
        g_app.frame_.grid_.colors_.empty()
            ? IRMath::Color{0, 0, 0, 0}
            : g_app.frame_.grid_.colors_[static_cast<std::size_t>(
                  (aim.y - g_app.frame_.grid_.origin_.y) * g_app.frame_.grid_.size_.x + aim.x -
                  g_app.frame_.grid_.origin_.x
              )];
    const bool flaggedOnGrid = drawn.toPackedRGBA() == FontMaker::kFlagColor.toPackedRGBA();
    actual += flaggedOnGrid ? " grid=flagged" : " grid=unflagged";
    return found && flaggedOnGrid && (!expect.exclusive_ || findings.size() == 1);
}

// Reads the drawn edit grid back: every source texel's footprint must widen
// from a one-texel apex column on the left when (col + row) is even and on
// the right when odd, the lattice orientation the GUI canvas displays.
bool gridParity(const void *, std::string &actual) {
    const FontMaker::LatticeImage &grid = g_app.frame_.grid_;
    const FontMaker::GlyphSet &set = g_app.editor_.activeSet();
    const int width = set.source_.width_;
    int checked = 0;
    int wrong = 0;
    for (int owner = 0; owner < width * set.source_.height_; ++owner) {
        int minX = grid.size_.x;
        int maxX = -1;
        std::vector<int> columnCount(static_cast<std::size_t>(grid.size_.x), 0);
        for (int y = 0; y < grid.size_.y; ++y) {
            for (int x = 0; x < grid.size_.x; ++x) {
                if (grid.owners_[static_cast<std::size_t>(y * grid.size_.x + x)] == owner) {
                    ++columnCount[static_cast<std::size_t>(x)];
                    minX = IRMath::min(minX, x);
                    maxX = IRMath::max(maxX, x);
                }
            }
        }
        if (maxX < 0) {
            continue;
        }
        ++checked;
        const bool apexLeft = columnCount[static_cast<std::size_t>(minX)] == 1 &&
                              columnCount[static_cast<std::size_t>(maxX)] > 1;
        const bool apexRight = columnCount[static_cast<std::size_t>(maxX)] == 1 &&
                               columnCount[static_cast<std::size_t>(minX)] > 1;
        const bool expectLeft = (owner % width + owner / width) % 2 == 0;
        wrong += (expectLeft ? apexLeft : apexRight) ? 0 : 1;
    }
    actual = "texels=" + std::to_string(checked) + " wrong_orientation=" + std::to_string(wrong) +
             " scale=" + std::to_string(g_app.layout_.gridScale_);
    return checked == width * set.source_.height_ && wrong == 0 && g_app.layout_.gridScale_ > 1;
}

bool duplicateFinding(const void *, std::string &actual) {
    const std::vector<char> &duplicates = g_app.frame_.duplicates_;
    actual = "selected=" + FontMaker::characterName(g_app.editor_.selected_) + " duplicate_of=";
    for (char c : duplicates) {
        actual += FontMaker::characterName(c);
    }
    const int cell = FontMaker::tableIndexOf(g_app.editor_, 'A');
    const IRMath::ivec2 marker =
        FontMaker::tableCellOrigin(cell) + IRMath::ivec2(1, FontMaker::kTableCell.y - 3);
    const FontMaker::LatticeImage &table = g_app.frame_.table_;
    const IRMath::ivec2 local = marker - table.origin_;
    const bool cellFlagged =
        table.colors_[static_cast<std::size_t>(local.y * table.size_.x + local.x)].toPackedRGBA() ==
        FontMaker::kFlagColor.toPackedRGBA();
    actual += cellFlagged ? " cell=flagged" : " cell=unflagged";
    return g_app.editor_.selected_ == 'A' && duplicates.size() == 1 && duplicates[0] == 'B' &&
           cellFlagged;
}

struct DisplayExpect {
    int guiScale_;
    bool pixelated_;
};

bool samplePreview(const void *context, std::string &actual) {
    const auto &expect = *static_cast<const DisplayExpect *>(context);
    const FontMaker::SampleStats stats = FontMaker::sampleStats(g_app.frame_.sample_);
    const int scale = IRRender::getGuiScale();
    const bool pixelated = IRPrefab::GuiCanvas::isPixelated();
    actual = "large_lit=" + std::to_string(stats.largeLit_) +
             " small_lit=" + std::to_string(stats.smallLit_) +
             " gui_scale=" + std::to_string(scale) + " pixelated=" + std::to_string(pixelated) +
             " control=" + std::to_string(g_previewBaseline) +
             " edited=" + std::to_string(stats.largeHash_);
    return stats.largeLit_ > 0 && stats.smallLit_ > 0 && scale == expect.guiScale_ &&
           pixelated == expect.pixelated_ && g_previewBaseline != 0 &&
           stats.largeHash_ != g_previewBaseline;
}

using IRPrefab::GuiTest::Assertion;
using IRPrefab::GuiTest::predicate;

constexpr CellExpect kSpaceEmpty{' ', GlyphState::EMPTY};
constexpr CellExpect kKMissing{'K', GlyphState::MISSING};
constexpr int kNoMissing = 0;
constexpr int kOneMissing = 1;
constexpr FindingExpect kLoneAtToggle{
    IRRender::GlyphRule::LONE_TRIANGLE, kToggleCol, kToggleRow, false
};
constexpr FindingExpect kStemNotch{IRRender::GlyphRule::WRONG_PARITY, 0, 1, true};
constexpr DisplayExpect kDisplay[] = {{1, false}, {2, false}, {1, true}, {2, true}};

const Assertion kTableShipped[] = {
    predicate(&uniqueGlyphCells, nullptr, "unique_glyph_cells"),
    predicate(&cellState, &kSpaceEmpty, "space_state"),
    predicate(&missingCellCount, &kNoMissing, "missing_cells"),
};
const Assertion kTableFixture[] = {
    predicate(&uniqueGlyphCells, nullptr, "unique_glyph_cells"),
    predicate(&cellState, &kKMissing, "removed_mapping_state"),
    predicate(&missingCellCount, &kOneMissing, "missing_cells"),
};
const Assertion kEditSelect[] = {predicate(&selectedIsSpace, nullptr, "selected_space")};
const Assertion kEditToggle[] = {
    predicate(&oneSourceBitChanged, nullptr, "one_source_bit"),
    predicate(&previewChanged, nullptr, "preview_changed"),
    predicate(&gridFinding, &kLoneAtToggle, "lone_triangle"),
};
const Assertion kEditParity[] = {predicate(&gridParity, nullptr, "grid_parity")};
const Assertion kEditWrongParity[] = {predicate(&gridFinding, &kStemNotch, "wrong_parity")};
const Assertion kEditDuplicate[] = {predicate(&duplicateFinding, nullptr, "duplicate_bitmap")};
const Assertion kSample[][1] = {
    {predicate(&samplePreview, &kDisplay[0], "sample_preview")},
    {predicate(&samplePreview, &kDisplay[1], "sample_preview")},
    {predicate(&samplePreview, &kDisplay[2], "sample_preview")},
    {predicate(&samplePreview, &kDisplay[3], "sample_preview")},
};

// --- Per-shot setup --------------------------------------------------------

fs::path shippedHeader(const char *file) {
    return fs::path(FONT_MAKER_SHIPPED_FONT_DIR) / file;
}

// A copy of the shipped LARGE header with the 'K' case removed, so 'K' has
// no mapping while its literal survives.
fs::path writeMissingMappingFixture() {
    std::string text;
    FontMaker::detail::readFile(shippedHeader("trixel_font.hpp"), text);
    const std::string line = "    case 'K': case 'k': return &kGlyph_K;\n";
    const std::size_t at = text.find(line);
    if (at != std::string::npos) {
        text.erase(at, line.size());
    }
    const fs::path dir = fs::temp_directory_path() / "font_maker_fixture";
    fs::create_directories(dir);
    const fs::path path = dir / "trixel_font_missing_k.hpp";
    std::ofstream(path, std::ios::binary | std::ios::trunc) << text;
    return path;
}

void setGlyphRows(char c, std::initializer_list<const char *> rows) {
    FontMaker::GlyphSet &set = g_app.editor_.activeSet();
    const int glyph = set.source_.glyphFor(c);
    std::size_t row = 0;
    for (const char *pattern : rows) {
        set.working_[glyph][row++] = IRRender::detail::packGlyphRow(pattern, set.source_.width_);
    }
}

void setupShot(int shotIndex) {
    Editor &editor = g_app.editor_;
    if (g_session == Session::TABLE) {
        if (shotIndex == 1) {
            editor.active_ = Editor::kSmallSlot;
        } else if (shotIndex == 2) {
            editor.openHeader(writeMissingMappingFixture(), true);
        }
    } else if (g_session == Session::EDIT) {
        if (shotIndex == 0) {
            editor.selected_ = 'A';
        } else if (shotIndex == 1) {
            g_previewBaseline = FontMaker::sampleStats(g_app.frame_.sample_).largeHash_;
        } else if (shotIndex == 3) {
            setGlyphRows(
                ' ',
                {"......",
                 "XX....",
                 "XX....",
                 "XX....",
                 "XX....",
                 "XX....",
                 "XX....",
                 "......",
                 "......",
                 "......",
                 "......"}
            );
        } else if (shotIndex == 4) {
            editor.selected_ = 'A';
            const int a = editor.activeSet().source_.glyphFor('A');
            const int b = editor.activeSet().source_.glyphFor('B');
            editor.activeSet().working_[a] = editor.activeSet().working_[b];
        }
    } else if (g_session == Session::SAMPLE) {
        if (shotIndex == 0) {
            IRPrefab::Widget::setTextInputValue(g_app.widgets_.sampleText_, kSampleText);
            g_previewBaseline = FontMaker::sampleStats(g_app.frame_.sample_).largeHash_;
            editor.selected_ = 'E';
            setGlyphRows(
                'E',
                {"XXXXXX",
                 "XXXXXX",
                 "XX....",
                 "XX....",
                 "XXX...",
                 "XXX...",
                 "XX....",
                 "XX....",
                 "XX....",
                 "XXXXXX",
                 "XXXXXX"}
            );
        }
        IRRender::setGuiScale(kDisplay[shotIndex].guiScale_);
        IRPrefab::GuiCanvas::setPixelated(kDisplay[shotIndex].pixelated_);
    }
}

void onAssertFrame(int shotIndex, bool isCaptureFrame) {
    if (shotIndex != g_setupShot) {
        g_setupShot = shotIndex;
        setupShot(shotIndex);
    }
    if (g_session == Session::EDIT) {
        aimClick(
            g_selectEvents,
            FontMaker::tableCellOrigin(FontMaker::tableIndexOf(g_app.editor_, ' ')) +
                FontMaker::kTableCell / 2
        );
        aimClick(g_toggleEvents, FontMaker::gridAimTexel(g_app.layout_, kToggleCol, kToggleRow));
    }

    const Assertion *assertions = nullptr;
    int count = 0;
    const auto use = [&](const auto &table) {
        assertions = table;
        count = static_cast<int>(std::size(table));
    };
    if (g_session == Session::TABLE) {
        if (shotIndex < 2) {
            use(kTableShipped);
        } else {
            use(kTableFixture);
        }
    } else if (g_session == Session::EDIT) {
        switch (shotIndex) {
        case 0:
            use(kEditSelect);
            break;
        case 1:
            use(kEditToggle);
            break;
        case 2:
            use(kEditParity);
            break;
        case 3:
            use(kEditWrongParity);
            break;
        default:
            use(kEditDuplicate);
            break;
        }
    } else if (g_session == Session::SAMPLE) {
        use(kSample[shotIndex]);
    }
    IRPrefab::GuiTest::onFrame(
        g_latch,
        shotIndex,
        isCaptureFrame,
        sessionShots()[static_cast<std::size_t>(shotIndex)].render_.label_,
        assertions,
        count
    );
}

Session sessionFromName(const std::string &name) {
    if (name == "table") {
        return Session::TABLE;
    }
    if (name == "edit") {
        return Session::EDIT;
    }
    if (name == "sample") {
        return Session::SAMPLE;
    }
    return Session::NONE;
}

} // namespace

void initSystems(int warmupFrames);
void initCommands();

int main(int argc, char **argv) {
    IREngine::args().string(
        "--font-header",
        "open this trixel-font header (the shipped schema: k<Prefix>Glyph* constants, "
        "make<Prefix>Glyph literals, a get<Prefix>Glyph switch) in the slot its prefix selects",
        ""
    );
    IREngine::args().enumValue(
        "--font-maker-test",
        "scripted GUI-test session: none | table | edit | sample (needs --auto-screenshot)",
        {"none", "table", "edit", "sample"},
        "none"
    );
    IREngine::init(argc, argv);
    g_session = sessionFromName(IREngine::args().getEnum("--font-maker-test"));

    Editor &editor = g_app.editor_;
    editor.openHeader(shippedHeader("trixel_font_small.hpp"), false);
    editor.openHeader(shippedHeader("trixel_font.hpp"), false);
    const std::string custom = IREngine::args().getString("--font-header");
    if (!custom.empty()) {
        editor.openHeader(fs::absolute(custom), true);
    }

    FontMaker::createWidgets(g_app);
    initSystems(IREngine::args().autoScreenshotWarmupFrames());
    initCommands();
    IREngine::gameLoop();
    return 0;
}

void initSystems(int warmupFrames) {
    IRSystem::registerPipeline(
        IRTime::Events::INPUT,
        {
            IRSystem::createSystem<IRSystem::INPUT_KEY_MOUSE>(),
            IRSystem::createSystem<IRSystem::HITBOX_MOUSE_TEST_GUI>(),
            IRSystem::createSystem<IRSystem::WIDGET_INPUT>(),
            IRSystem::createSystem<IRSystem::WIDGET_APPLY_TEXT_INPUT>(),
        }
    );
    IRSystem::registerPipeline(
        IRTime::Events::UPDATE,
        {FontMaker::createInputSystem(g_app),
         IRSystem::createSystem<IRSystem::PROPAGATE_TRANSFORM>()}
    );

    std::list<IRSystem::SystemId> renderPipeline = {
        IRSystem::createSystem<IRSystem::VOXEL_TO_TRIXEL_STAGE_1>(),
        IRSystem::createSystem<IRSystem::TEXT_TO_TRIXEL>(),
        IRSystem::createSystem<IRSystem::WIDGET_RENDER_PANEL>(),
        IRSystem::createSystem<IRSystem::WIDGET_RENDER_LABEL>(),
        IRSystem::createSystem<IRSystem::WIDGET_RENDER_BUTTON>(),
        IRSystem::createSystem<IRSystem::WIDGET_RENDER_TEXT_INPUT>(),
        FontMaker::createRenderSystem(g_app),
        IRSystem::System<IRSystem::PERF_HUD>::create(),
        IRSystem::System<IRSystem::HELP_OVERLAY>::create(),
        IRSystem::createSystem<IRSystem::TRIXEL_TO_FRAMEBUFFER>(),
        IRSystem::createSystem<IRSystem::FRAMEBUFFER_TO_SCREEN>(),
        IRSystem::createSystem<IRSystem::GUI_CANVAS_TO_SCREEN>(),
    };
    if (warmupFrames > 0) {
        const std::span<const IRVideo::GuiTestShot> shots = sessionShots();
        IRVideo::GuiTestConfig config{};
        config.warmupFrames_ = warmupFrames;
        config.settleFrames_ = 4;
        config.shots_ = shots.data();
        config.numShots_ = static_cast<int>(shots.size());
        config.onAssertFrame_ = &onAssertFrame;
        renderPipeline.push_back(IRVideo::createGuiTestSystem(config));
    }
    IRSystem::registerPipeline(IRTime::Events::RENDER, renderPipeline);
}

void initCommands() {
    IRCommand::createCommand<IRCommand::CLOSE_WINDOW>(
        InputTypes::KEY_MOUSE,
        ButtonStatuses::PRESSED,
        KeyMouseButtons::kKeyButtonEscape
    );
    IRCommand::registerCaptureCommands();
    IRPrefab::HelpOverlay::registerToggleCommand(KeyMouseButtons::kKeyButtonGraveAccent);
    IRPrefab::PerfHud::registerToggleCommand();
    IRCommand::createCommand<IRCommand::GUI_ZOOM_IN>(
        InputTypes::KEY_MOUSE,
        ButtonStatuses::PRESSED,
        KeyMouseButtons::kKeyButtonEqual,
        IRInput::kModifierControl
    );
    IRCommand::createCommand<IRCommand::GUI_ZOOM_OUT>(
        InputTypes::KEY_MOUSE,
        ButtonStatuses::PRESSED,
        KeyMouseButtons::kKeyButtonMinus,
        IRInput::kModifierControl
    );
}
