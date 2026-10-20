#include <gtest/gtest.h>

#include <irreden/ir_entity.hpp>
#include <irreden/entity/entity_manager.hpp>
#include <irreden/render/gui_layout_audit.hpp>
#include <irreden/render/widget_text_layout.hpp>
#include <irreden/render/widgets.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace {

using IRComponents::C_Widget;
using IRComponents::C_WidgetDropdown;
using IRMath::ivec2;
using IRPrefab::GuiAudit::Finding;
using IRPrefab::GuiAudit::FindingKind;
using IRPrefab::GuiAudit::report;

// Both canvas dimensions halve to an odd number, so the parity alignment
// leaves an origin whose x + y is even where it is: every fixture below sits
// on such an origin and its glyph cells follow from the glyph metrics alone
// (LARGE: 6x11 cells on an 8-trixel step, so an n-glyph run is 8n - 2 wide).
constexpr ivec2 kCanvas{400, 300};

Finding
finding(FindingKind kind, IREntity::EntityId a, IREntity::EntityId b, ivec2 pos, ivec2 size) {
    return Finding{kind, a, b, pos, size};
}

std::string describe(const std::vector<Finding> &findings) {
    std::string out;
    for (const Finding &f : findings) {
        out += std::string(IRPrefab::GuiAudit::kindName(f.kind_)) + " " + std::to_string(f.a_) +
               " " + std::to_string(f.b_) + " (" + std::to_string(f.pos_.x) + "," +
               std::to_string(f.pos_.y) + " " + std::to_string(f.size_.x) + "x" +
               std::to_string(f.size_.y) + ")\n";
    }
    return out;
}

int countKind(const std::vector<Finding> &findings, FindingKind kind) {
    int count = 0;
    for (const Finding &f : findings) {
        count += f.kind_ == kind ? 1 : 0;
    }
    return count;
}

class GuiLayoutAudit : public testing::Test {
  protected:
    void TearDown() override {
        m_entityManager.destroyAllEntities();
    }

    // An open dropdown whose strip (y 48..102) covers two button rows.
    IREntity::EntityId buildDropdownOverRows() {
        IRPrefab::Widget::makePanel(ivec2(0, 0), ivec2(300, 200));
        const IREntity::EntityId dropdown = IRPrefab::Widget::makeDropdown(
            ivec2(10, 30),
            ivec2(100, 18),
            {"ONE", "TWO", "THREE"},
            0,
            18
        );
        IREntity::getComponent<C_WidgetDropdown>(dropdown).isOpen_ = true;
        IRPrefab::Widget::makeButton(ivec2(10, 50), ivec2(100, 14), "ROW A");
        IRPrefab::Widget::makeButton(ivec2(10, 70), ivec2(100, 14), "ROW B");
        return dropdown;
    }

    // The audited text extents of `owner`, in collection order.
    std::vector<IRPrefab::GuiRect> auditedRuns(IREntity::EntityId owner) {
        std::vector<IRPrefab::GuiRect> runs;
        const IRPrefab::GuiAudit::detail::Layout layout =
            IRPrefab::GuiAudit::detail::Collector{kCanvas}.collect();
        for (const IRPrefab::GuiAudit::detail::Element &element : layout.elements_) {
            if (element.owner_ == owner &&
                element.surface_ == IRPrefab::GuiAudit::detail::Surface::TEXT) {
                runs.push_back(element.rect_);
            }
        }
        return runs;
    }

    IRPrefab::GuiRect queued(const IRPrefab::WidgetText::TextRun &run) {
        std::vector<IRRender::GlyphDrawCommand> commands;
        IRPrefab::WidgetText::queue(commands, run, kCanvas, IRMath::Color{});
        return IRPrefab::WidgetText::drawnCellBounds(commands, m_fontBuffer);
    }

    void expectRuns(IREntity::EntityId owner, const std::vector<IRPrefab::GuiRect> &expected) {
        const std::vector<IRPrefab::GuiRect> runs = auditedRuns(owner);
        ASSERT_EQ(runs.size(), expected.size());
        for (std::size_t i = 0; i < runs.size(); ++i) {
            EXPECT_EQ(runs[i].pos_, expected[i].pos_) << "run " << i;
            EXPECT_EQ(runs[i].size_, expected[i].size_) << "run " << i;
        }
    }

    template <typename T> const T &data(IREntity::EntityId id) {
        return IREntity::getComponent<T>(id);
    }

    const IRPrefab::Widget::WidgetTheme &theme() {
        return IRPrefab::Widget::defaultTheme();
    }

    IREntity::EntityManager m_entityManager;
    std::vector<std::uint32_t> m_fontBuffer = IRPrefab::GuiText::buildFontBuffer();
};

// The renderer-text cases: the audit measures each run through the helper its
// renderer queues, so a run's audited extent is the glyph-cell union of exactly
// the commands that helper call queues.

TEST_F(GuiLayoutAudit, OneDefectPerKindGivesOneFindingPerKind) {
    const IREntity::EntityId panel = IRPrefab::Widget::makePanel(ivec2(0, 0), ivec2(300, 200));
    const IREntity::EntityId overlapA =
        IRPrefab::Widget::makeButton(ivec2(10, 20), ivec2(40, 14), "");
    const IREntity::EntityId overlapB =
        IRPrefab::Widget::makeButton(ivec2(40, 20), ivec2(40, 14), "");
    // "ABCDEFGH" spans x 10..72, y 60..71.
    const IREntity::EntityId label = IRPrefab::Widget::makeLabel(ivec2(10, 60), "ABCDEFGH");
    const IREntity::EntityId crossed =
        IRPrefab::Widget::makeButton(ivec2(60, 58), ivec2(30, 14), "");
    // "WIDE" (30 wide) centred in 20: the even-rounded offset -4 puts it at x 6..36.
    const IREntity::EntityId clipped =
        IRPrefab::Widget::makeButton(ivec2(10, 100), ivec2(20, 14), "WIDE");
    const IREntity::EntityId leaving =
        IRPrefab::Widget::makeCheckbox(ivec2(250, 150), ivec2(80, 14), "");
    // Its origin lies outside the panel, so it is offscreen and nothing else.
    const IREntity::EntityId offscreen =
        IRPrefab::Widget::makeHitRegion(ivec2(380, 250), ivec2(40, 20));

    const std::vector<Finding> findings = report(kCanvas);

    const std::vector<Finding> expected{
        finding(FindingKind::OVERLAP, overlapA, overlapB, ivec2(40, 20), ivec2(10, 14)),
        finding(FindingKind::TEXT_OVER_WIDGET, label, crossed, ivec2(60, 60), ivec2(12, 11)),
        finding(FindingKind::TEXT_CLIPPED, clipped, clipped, ivec2(6, 100), ivec2(30, 11)),
        finding(FindingKind::OUTSIDE_PARENT, leaving, panel, ivec2(300, 150), ivec2(30, 14)),
        finding(
            FindingKind::OFFSCREEN,
            offscreen,
            IREntity::kNullEntity,
            ivec2(400, 250),
            ivec2(20, 20)
        ),
    };
    EXPECT_EQ(findings, expected) << describe(findings);
}

TEST_F(GuiLayoutAudit, CleanPanelGivesNoFindings) {
    IRPrefab::Widget::makePanel(ivec2(0, 0), ivec2(200, 150), "TOOLS");
    IRPrefab::Widget::makeButton(ivec2(10, 30), ivec2(60, 14), "OK");
    IRPrefab::Widget::makeLabel(ivec2(80, 30), "HELLO");
    IRPrefab::Widget::makeCheckbox(ivec2(10, 50), ivec2(80, 14), "SHOW");
    IRPrefab::Widget::makeSlider(ivec2(10, 70), ivec2(121, 14), "SIZE", 0.0f, 1.0f, 0.5f);
    IRPrefab::Widget::makeTextInput(ivec2(10, 90), ivec2(120, 14), "NAME");
    IRPrefab::Widget::makeList(ivec2(10, 110), ivec2(120, 26), {"A", "B"}, 0, 13);

    const std::vector<Finding> findings = report(kCanvas);

    EXPECT_TRUE(findings.empty()) << describe(findings);
}

TEST_F(GuiLayoutAudit, OpenDropdownStripCoversTheRowsBelowIt) {
    buildDropdownOverRows();

    const std::vector<Finding> findings = report(kCanvas);

    EXPECT_TRUE(findings.empty()) << describe(findings);
}

TEST_F(GuiLayoutAudit, WithdrawnStripCoverReportsTheOverlap) {
    const IREntity::EntityId dropdown = buildDropdownOverRows();
    IRPrefab::Widget::setCoversBelow(dropdown, false);

    const std::vector<Finding> findings = report(kCanvas);

    int namingDropdown = 0;
    for (const Finding &f : findings) {
        if (f.kind_ == FindingKind::OVERLAP && (f.a_ == dropdown || f.b_ == dropdown)) {
            ++namingDropdown;
        }
    }
    EXPECT_GE(namingDropdown, 1) << describe(findings);
}

TEST_F(GuiLayoutAudit, LabelInsidePanelIsCoveredAndOneLeavingItIsOutsideParent) {
    const IREntity::EntityId panel = IRPrefab::Widget::makePanel(ivec2(0, 0), ivec2(100, 60));
    const IREntity::EntityId label = IRPrefab::Widget::makeLabel(ivec2(10, 20), "HELLO");

    const std::vector<Finding> inside = report(kCanvas);
    EXPECT_TRUE(inside.empty()) << describe(inside);

    // "HELLO" spans x 80..118 from here, 18 trixels past the panel's edge.
    IREntity::getComponent<IRComponents::C_GuiPosition>(label).pos_ = ivec2(80, 20);
    const std::vector<Finding> leaving = report(kCanvas);

    const std::vector<Finding> expected{
        finding(FindingKind::OUTSIDE_PARENT, label, panel, ivec2(100, 20), ivec2(18, 11)),
    };
    EXPECT_EQ(leaving, expected) << describe(leaving);
}

TEST_F(GuiLayoutAudit, WidgetOverPanelTitleIsTextOverWidget) {
    // "TITLE" (38 wide) centred in the 200 x 22 title bar: x 80..118, y 4..15.
    const IREntity::EntityId panel =
        IRPrefab::Widget::makePanel(ivec2(0, 0), ivec2(200, 100), "TITLE");
    const IREntity::EntityId button = IRPrefab::Widget::makeButton(ivec2(80, 2), ivec2(40, 14), "");

    const std::vector<Finding> findings = report(kCanvas);

    const std::vector<Finding> expected{
        finding(FindingKind::TEXT_OVER_WIDGET, panel, button, ivec2(80, 4), ivec2(38, 11)),
    };
    EXPECT_EQ(findings, expected) << describe(findings);
}

TEST_F(GuiLayoutAudit, OverlappingCaptionedButtonsAreOneOverlap) {
    // Captions at x 14..44 and 34..64 each reach into the other button.
    const IREntity::EntityId first =
        IRPrefab::Widget::makeButton(ivec2(10, 10), ivec2(40, 14), "AAAA");
    const IREntity::EntityId second =
        IRPrefab::Widget::makeButton(ivec2(30, 10), ivec2(40, 14), "BBBB");

    const std::vector<Finding> findings = report(kCanvas);

    const std::vector<Finding> expected{
        finding(FindingKind::OVERLAP, first, second, ivec2(30, 10), ivec2(20, 14)),
    };
    EXPECT_EQ(findings, expected) << describe(findings);
}

TEST_F(GuiLayoutAudit, TwoOverlappingLabelsAreOneFinding) {
    IRPrefab::Widget::makeLabel(ivec2(10, 10), "FIRST");
    IRPrefab::Widget::makeLabel(ivec2(30, 14), "SECOND");

    const std::vector<Finding> findings = report(kCanvas);

    EXPECT_EQ(findings.size(), 1u) << describe(findings);
    EXPECT_EQ(countKind(findings, FindingKind::TEXT_OVER_WIDGET), 1) << describe(findings);
}

TEST_F(GuiLayoutAudit, DeclaredCoverDropsOverlapButNotOwnBounds) {
    const IREntity::EntityId overlay =
        IRPrefab::Widget::makeButton(ivec2(10, 10), ivec2(40, 14), "");
    IRPrefab::Widget::makeButton(ivec2(20, 10), ivec2(40, 14), "");
    const IREntity::EntityId offscreen =
        IRPrefab::Widget::makeHitRegion(ivec2(390, 10), ivec2(20, 14));
    IRPrefab::Widget::setCoversBelow(overlay, true);
    IRPrefab::Widget::setCoversBelow(offscreen, true);

    const std::vector<Finding> findings = report(kCanvas);

    const std::vector<Finding> expected{
        finding(
            FindingKind::OFFSCREEN,
            offscreen,
            IREntity::kNullEntity,
            ivec2(400, 10),
            ivec2(10, 14)
        ),
    };
    EXPECT_EQ(findings, expected) << describe(findings);
}

TEST_F(GuiLayoutAudit, DeclaredCoverTakesTheOverlaysCaptionToo) {
    // Captions at x 14..44 and 34..64 each reach into the other button.
    const IREntity::EntityId overlay =
        IRPrefab::Widget::makeButton(ivec2(10, 10), ivec2(40, 14), "AAAA");
    IRPrefab::Widget::makeButton(ivec2(30, 10), ivec2(40, 14), "BBBB");
    IRPrefab::Widget::setCoversBelow(overlay, true);

    const std::vector<Finding> findings = report(kCanvas);

    EXPECT_TRUE(findings.empty()) << describe(findings);
}

TEST_F(GuiLayoutAudit, CoveringDropdownHeaderIsStillAudited) {
    const IREntity::EntityId dropdown =
        IRPrefab::Widget::makeDropdown(ivec2(10, 10), ivec2(100, 18), {"ONE", "TWO"}, 0, 18);
    const IREntity::EntityId label = IRPrefab::Widget::makeLabel(ivec2(10, 12), "LABEL");
    ASSERT_TRUE(IRPrefab::Widget::coversBelow(dropdown));

    const std::vector<Finding> findings = report(kCanvas);

    ASSERT_EQ(findings.size(), 1u) << describe(findings);
    EXPECT_EQ(findings[0].kind_, FindingKind::TEXT_OVER_WIDGET);
    EXPECT_EQ(findings[0].a_, IRMath::min(dropdown, label));
    EXPECT_EQ(findings[0].b_, IRMath::max(dropdown, label));
}

TEST_F(GuiLayoutAudit, TwoReportsOverTheSameStateAreEqual) {
    IRPrefab::Widget::makeButton(ivec2(10, 10), ivec2(40, 14), "AAAA");
    IRPrefab::Widget::makeButton(ivec2(30, 10), ivec2(40, 14), "BBBB");
    IRPrefab::Widget::makeLabel(ivec2(380, 50), "OFF THE EDGE");
    IRPrefab::Widget::makeHitRegion(ivec2(390, 280), ivec2(20, 30));

    const std::vector<Finding> first = report(kCanvas);
    const std::vector<Finding> second = report(kCanvas);

    EXPECT_FALSE(first.empty());
    EXPECT_EQ(first, second);
}

TEST_F(GuiLayoutAudit, NoGuiCanvasGivesAnEmptyLiveReport) {
    IRPrefab::Widget::makeButton(ivec2(10, 10), ivec2(40, 14), "AAAA");
    IRPrefab::Widget::makeButton(ivec2(30, 10), ivec2(40, 14), "BBBB");

    EXPECT_TRUE(report().empty());
}

TEST_F(GuiLayoutAudit, SpacesAddNoCell) {
    const std::vector<std::uint32_t> fontBuffer = IRPrefab::GuiText::buildFontBuffer();
    std::vector<IRRender::GlyphDrawCommand> commands;
    IRPrefab::WidgetText::TextRun run;
    run.text_ = "  AB ";
    run.pos_ = ivec2(10, 20);
    IRPrefab::WidgetText::queue(commands, run, kCanvas, IRMath::Color{});

    const IRPrefab::GuiRect bounds = IRPrefab::WidgetText::drawnCellBounds(commands, fontBuffer);

    EXPECT_EQ(bounds.pos_, ivec2(26, 20));
    EXPECT_EQ(bounds.size_, ivec2(14, 11));
}

TEST_F(GuiLayoutAudit, ButtonCaption) {
    const IREntity::EntityId id =
        IRPrefab::Widget::makeButton(ivec2(10, 10), ivec2(40, 14), "AAAA");
    expectRuns(
        id,
        {queued(
            IRPrefab::WidgetText::buttonCaption(
                data<C_Widget>(id),
                data<IRComponents::C_WidgetButton>(id),
                ivec2(10, 10)
            )
        )}
    );
    // Centred: (40 - 30) / 2 = 5, even-rounded to 4; (14 - 11) / 2 = 1, to 0.
    expectRuns(id, {{ivec2(14, 10), ivec2(30, 11)}});
}

TEST_F(GuiLayoutAudit, CheckboxAndRadioCaptions) {
    const IREntity::EntityId checkbox =
        IRPrefab::Widget::makeCheckbox(ivec2(10, 30), ivec2(90, 14), "SHOW");
    const IREntity::EntityId radio =
        IRPrefab::Widget::makeRadio(ivec2(10, 50), ivec2(90, 14), "PICK", 1, 0);
    expectRuns(
        checkbox,
        {queued(
            IRPrefab::WidgetText::checkboxCaption(
                data<C_Widget>(checkbox),
                data<IRComponents::C_WidgetCheckbox>(checkbox),
                ivec2(10, 30),
                theme()
            )
        )}
    );
    expectRuns(
        radio,
        {queued(
            IRPrefab::WidgetText::radioCaption(
                data<C_Widget>(radio),
                data<IRComponents::C_WidgetRadio>(radio),
                ivec2(10, 50),
                theme()
            )
        )}
    );
    // Box 16 + padding 8 from the origin.
    expectRuns(checkbox, {{ivec2(34, 30), ivec2(30, 11)}});
}

TEST_F(GuiLayoutAudit, SliderLabelAndValue) {
    const IREntity::EntityId id =
        IRPrefab::Widget::makeSlider(ivec2(10, 70), ivec2(121, 30), "SIZE", 0.0f, 1.0f, 0.5f);
    const auto &slider = data<IRComponents::C_WidgetSlider>(id);
    const IRPrefab::WidgetText::SliderValueText value =
        IRPrefab::WidgetText::sliderValueText(slider);
    expectRuns(
        id,
        {queued(IRPrefab::WidgetText::sliderLabel(slider, ivec2(10, 70))),
         queued(
             IRPrefab::WidgetText::sliderValue(data<C_Widget>(id), slider, ivec2(10, 70), value)
         )}
    );
}

TEST_F(GuiLayoutAudit, PanelTitle) {
    const IREntity::EntityId id =
        IRPrefab::Widget::makePanel(ivec2(0, 0), ivec2(200, 100), "TITLE");
    expectRuns(
        id,
        {queued(
            IRPrefab::WidgetText::panelTitle(
                data<C_Widget>(id),
                data<IRComponents::C_WidgetPanel>(id),
                ivec2(0, 0),
                theme()
            )
        )}
    );
}

TEST_F(GuiLayoutAudit, NumberFieldValueAndEditBuffer) {
    const IREntity::EntityId id =
        IRPrefab::Widget::makeNumberField(ivec2(10, 10), ivec2(80, 14), 2.5f, 0.0f, 10.0f, 0.5f);
    const auto &field = data<IRComponents::C_WidgetNumberField>(id);
    const std::string formatted = field.formattedValue();
    expectRuns(
        id,
        {queued(
            IRPrefab::WidgetText::
                numberFieldText(data<C_Widget>(id), field, ivec2(10, 10), theme(), formatted)
        )}
    );

    auto &editing = IREntity::getComponent<IRComponents::C_WidgetNumberField>(id);
    editing.editing_ = true;
    editing.editBuffer_ = "12";
    expectRuns(
        id,
        {queued(
            IRPrefab::WidgetText::numberFieldText(
                data<C_Widget>(id),
                editing,
                ivec2(10, 10),
                theme(),
                editing.editBuffer_
            )
        )}
    );
}

TEST_F(GuiLayoutAudit, TextInputAndListRows) {
    const IREntity::EntityId input =
        IRPrefab::Widget::makeTextInput(ivec2(10, 10), ivec2(120, 14), "NAME");
    const IREntity::EntityId list =
        IRPrefab::Widget::makeList(ivec2(10, 30), ivec2(120, 26), {"ONE", "TWO", "THREE"}, 0, 13);
    expectRuns(
        input,
        {queued(
            IRPrefab::WidgetText::textInputText(
                data<C_Widget>(input),
                data<IRComponents::C_WidgetTextInput>(input),
                ivec2(10, 10),
                theme()
            )
        )}
    );
    // Two whole rows fit the view; the third item is not drawn.
    const auto &listData = data<IRComponents::C_WidgetList>(list);
    expectRuns(
        list,
        {queued(IRPrefab::WidgetText::listRow(listData, ivec2(10, 30), theme(), 0, 0)),
         queued(IRPrefab::WidgetText::listRow(listData, ivec2(10, 30), theme(), 1, 1))}
    );
}

TEST_F(GuiLayoutAudit, DropdownHeaderAndOpenStripRows) {
    const IREntity::EntityId id =
        IRPrefab::Widget::makeDropdown(ivec2(10, 10), ivec2(100, 18), {"ONE", "TWO"}, 1, 18);
    IREntity::getComponent<C_WidgetDropdown>(id).isOpen_ = true;
    const auto &dd = data<C_WidgetDropdown>(id);
    const C_Widget &widget = data<C_Widget>(id);

    // A covering strip is no subject: only the header is audited.
    expectRuns(
        id,
        {queued(IRPrefab::WidgetText::dropdownHeader(widget, dd, ivec2(10, 10), theme()))}
    );

    IRPrefab::Widget::setCoversBelow(id, false);
    expectRuns(
        id,
        {queued(IRPrefab::WidgetText::dropdownHeader(widget, dd, ivec2(10, 10), theme())),
         queued(IRPrefab::WidgetText::dropdownStripRow(widget, dd, ivec2(10, 10), theme(), 0)),
         queued(IRPrefab::WidgetText::dropdownStripRow(widget, dd, ivec2(10, 10), theme(), 1))}
    );
}

TEST_F(GuiLayoutAudit, LabelAndFreeText) {
    const IREntity::EntityId label = IRPrefab::Widget::makeLabel(
        ivec2(10, 10),
        "SMALL",
        IRMath::Color{0, 0, 0, 0},
        IRRender::TextSize::SMALL
    );
    IRComponents::C_TextStyle style{};
    style.alignH_ = IRComponents::TextAlignH::CENTER;
    style.boxWidth_ = 200;
    const IREntity::EntityId text = IREntity::createEntity(
        IRComponents::C_TextSegment{"FREE TEXT"},
        IRComponents::C_GuiPosition{ivec2(20, 40)},
        IRComponents::C_GuiElement{},
        style
    );

    expectRuns(
        label,
        {queued(
            IRPrefab::WidgetText::label(data<IRComponents::C_WidgetLabel>(label), ivec2(10, 10))
        )}
    );

    std::vector<IRRender::GlyphDrawCommand> commands;
    IRPrefab::WidgetText::StyledTextScratch scratch;
    IRPrefab::WidgetText::queueStyledText(
        commands,
        "FREE TEXT",
        ivec2(20, 40),
        kCanvas,
        style,
        scratch
    );
    expectRuns(text, {IRPrefab::WidgetText::drawnCellBounds(commands, m_fontBuffer)});
}

TEST_F(GuiLayoutAudit, VoxelFreeText) {
    const IRComponents::C_TextStyle style =
        IRComponents::C_TextStyle::voxel(IRRender::VoxelFaceColors{}, 2);
    const IREntity::EntityId text = IREntity::createEntity(
        IRComponents::C_TextSegment{"VOX"},
        IRComponents::C_GuiPosition{ivec2(20, 40)},
        IRComponents::C_GuiElement{},
        style
    );

    std::vector<IRRender::GlyphDrawCommand> commands;
    IRPrefab::WidgetText::StyledTextScratch scratch;
    IRPrefab::WidgetText::queueStyledText(commands, "VOX", ivec2(20, 40), kCanvas, style, scratch);
    const IRPrefab::GuiRect expected =
        IRPrefab::WidgetText::drawnCellBounds(commands, m_fontBuffer);

    ASSERT_FALSE(IRPrefab::isEmpty(expected));
    expectRuns(text, {expected});
}

} // namespace
