#ifndef GUI_LAYOUT_AUDIT_H
#define GUI_LAYOUT_AUDIT_H

// A deterministic layout audit over the live GUI: every pair of widgets or
// text runs that touch, and every element that leaves its bounds, decided from
// the widget rects and the glyph cells the renderers queue (widget_text_layout.hpp).
// A free function, run on request only: it allocates and walks every widget.
//
// Input set. Widgets (`C_Widget` + `C_GuiPosition`) and free-text entities
// (`C_TextSegment` + `C_GuiPosition` + `C_GuiElement` + `C_TextStyle`), at
// their current component state. Text queued straight onto a glyph batch (the
// help overlay, a creation's own runs) has no owning entity and is not audited.
// - A widget's rect is `pos_` + `C_Widget::size_`; a LABEL's is its text extent.
// - A widget with `C_HitBox2DGui` is interactive, over `pos_` + the hitbox's
//   `size_`. A DROPDOWN's interactive rect is its header (`C_Widget::size_`)
//   plus, while open, the strip the `C_WidgetDropdown` helpers place.
// - A text run's extent is the union of the glyph cells it draws (spaces
//   add nothing), at the explicit canvas size's parity alignment.
// - Rects are half-open: edges that only touch do not intersect.
//
// Kinds, one finding per defect, sorted by (kind, a, b, x, y, w, h):
// - overlap (a, b): two interactive rects intersect; a < b; the intersection.
// - text-over-widget (a, b): a text run crosses a widget or run of another
//   entity; a < b; the bounding box of every such intersection. A pair that
//   already has an overlap gets none.
// - text-clipped (owner, owner | 0): a run passes its owner's text region
//   (the widget rect; a dropdown header less its chevron reserve; an open
//   strip's rect for its rows), or the canvas edge (b = 0). LABELs and free
//   text are bounded by the canvas edge only. The part outside, boxed.
// - outside-parent (widget, panel): a widget is not inside its containing
//   panel, the smallest-area other PANEL whose rect holds the widget's origin.
//   The part outside, boxed.
// - offscreen (widget, 0): a non-LABEL widget passes the canvas. The part
//   outside, boxed.
//
// Declared intent. `C_Widget::coversBelow_` drops an overlap or
// text-over-widget finding whose pair includes the widget's covering surface:
// a PANEL's background (never its title run), a DROPDOWN's open strip and its
// rows (never its header; a covering strip is never a subject at all), a
// LABEL's text, any other kind's whole rect and every run it draws.
// text-clipped, outside-parent and offscreen concern an element's own bounds
// and are never dropped. Declare a cover only for a deliberate overlay; fix
// the layout for anything else.

#include <irreden/ir_entity.hpp>
#include <irreden/ir_math.hpp>
#include <irreden/ir_profile.hpp>
#include <irreden/ir_render.hpp>

#include <irreden/common/components/component_tags_all.hpp>
#include <irreden/input/components/component_hitbox_2d_gui.hpp>
#include <irreden/render/components/component_gui_position.hpp>
#include <irreden/render/components/component_text_segment.hpp>
#include <irreden/render/components/component_text_style.hpp>
#include <irreden/render/components/component_triangle_canvas_textures.hpp>
#include <irreden/render/components/component_widget.hpp>
#include <irreden/render/gui_rect.hpp>
#include <irreden/render/gui_text_batch.hpp>
#include <irreden/render/widget_text_layout.hpp>
#include <irreden/render/widget_theme.hpp>

#include <algorithm>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

namespace IRPrefab::GuiAudit {

enum class FindingKind {
    OVERLAP,
    TEXT_OVER_WIDGET,
    TEXT_CLIPPED,
    OUTSIDE_PARENT,
    OFFSCREEN,
};

// The log name gui-verify reads.
inline const char *kindName(FindingKind kind) {
    switch (kind) {
    case FindingKind::OVERLAP:
        return "overlap";
    case FindingKind::TEXT_OVER_WIDGET:
        return "text-over-widget";
    case FindingKind::TEXT_CLIPPED:
        return "text-clipped";
    case FindingKind::OUTSIDE_PARENT:
        return "outside-parent";
    case FindingKind::OFFSCREEN:
        return "offscreen";
    }
    return "unknown";
}

struct Finding {
    FindingKind kind_ = FindingKind::OVERLAP;
    IREntity::EntityId a_ = IREntity::kNullEntity;
    IREntity::EntityId b_ = IREntity::kNullEntity;
    IRMath::ivec2 pos_ = IRMath::ivec2(0);
    IRMath::ivec2 size_ = IRMath::ivec2(0);

    bool operator==(const Finding &) const = default;
};

// The capture hook's flag. A creation that wires the hook registers it on
// `IREngine::args()` before init and arms `GuiTest::LatchState::layoutCheck_`
// from it; an unwired creation rejects the flag as unknown.
inline constexpr char kLayoutCheckFlag[] = "--gui-layout-check";
inline constexpr char kLayoutCheckFlagHelp[] =
    "log a GUI-LAYOUT line per GUI layout audit finding, and a GUI-LAYOUT-SUMMARY, on "
    "every GUI-test capture frame";

namespace detail {

// What an element is to its owner. HIT is the interactive rect (overlap only);
// BODY the drawn rect, STRIP an open dropdown's strip (both kinds), TEXT a run.
enum class Surface { BODY, HIT, STRIP, TEXT };

struct Element {
    IREntity::EntityId owner_ = IREntity::kNullEntity;
    GuiRect rect_{};
    Surface surface_ = Surface::BODY;
    bool interactive_ = false;
    bool covers_ = false;
    // TEXT only: the region the run must stay inside, when the owner has one.
    bool hasTextRegion_ = false;
    GuiRect textRegion_{};
};

// A widget's own rect, for the bounds kinds.
struct WidgetBox {
    IREntity::EntityId id_ = IREntity::kNullEntity;
    IRComponents::WidgetKind kind_ = IRComponents::WidgetKind::PANEL;
    GuiRect rect_{};
    IRMath::ivec2 origin_ = IRMath::ivec2(0);
};

struct Layout {
    std::vector<Element> elements_;
    std::vector<WidgetBox> widgets_;
};

class Collector {
  public:
    explicit Collector(IRMath::ivec2 canvasSize)
        : m_canvasSize{canvasSize}
        , m_theme{IRPrefab::Widget::defaultTheme()}
        , m_fontBuffer{IRPrefab::GuiText::buildFontBuffer()} {}

    Layout collect() {
        IREntity::forEachComponent<IRComponents::C_Widget>(
            [this](IREntity::EntityId &id, IRComponents::C_Widget &widget) {
                addWidget(id, widget);
            }
        );
        IREntity::forEachComponent<IRComponents::C_TextSegment>(
            [this](IREntity::EntityId &id, IRComponents::C_TextSegment &segment) {
                addFreeText(id, segment);
            }
        );
        return std::move(m_layout);
    }

  private:
    // The extent of `run` as the renderers queue it; empty when it draws nothing.
    GuiRect runExtent(const IRPrefab::WidgetText::TextRun &run) {
        m_scratch.clear();
        IRPrefab::WidgetText::queue(m_scratch, run, m_canvasSize, IRMath::Color{});
        return IRPrefab::WidgetText::drawnCellBounds(m_scratch, m_fontBuffer);
    }

    void addRun(
        IREntity::EntityId owner,
        const IRPrefab::WidgetText::TextRun &run,
        std::optional<GuiRect> textRegion,
        bool covers = false
    ) {
        addText(owner, runExtent(run), textRegion, covers);
    }

    // A text element over `extent`; nothing when the run drew nothing.
    void addText(
        IREntity::EntityId owner,
        const GuiRect &extent,
        std::optional<GuiRect> textRegion,
        bool covers = false
    ) {
        if (isEmpty(extent)) {
            return;
        }
        Element element;
        element.owner_ = owner;
        element.rect_ = extent;
        element.surface_ = Surface::TEXT;
        element.covers_ = covers;
        element.hasTextRegion_ = textRegion.has_value();
        element.textRegion_ = textRegion.value_or(GuiRect{});
        m_layout.elements_.push_back(element);
    }

    template <typename T> const T *component(IREntity::EntityId id) {
        return IREntity::getComponentOptional<T>(id).value_or(nullptr);
    }

    void addWidget(IREntity::EntityId id, const IRComponents::C_Widget &widget) {
        const auto *guiPos = component<IRComponents::C_GuiPosition>(id);
        if (guiPos == nullptr) {
            return;
        }
        const IRMath::ivec2 pos = guiPos->pos_;
        if (widget.kind_ == IRComponents::WidgetKind::LABEL) {
            addLabel(id, widget, pos);
            return;
        }

        const GuiRect rect{pos, widget.size_};
        if (isEmpty(rect)) {
            return;
        }
        m_layout.widgets_.push_back({id, widget.kind_, rect, pos});
        const bool isDropdown = widget.kind_ == IRComponents::WidgetKind::DROPDOWN;
        const bool bodyCovers = widget.coversBelow_ && !isDropdown;
        const bool textCovers = bodyCovers && widget.kind_ != IRComponents::WidgetKind::PANEL;
        m_layout.elements_.push_back({id, rect, Surface::BODY, false, bodyCovers});

        const auto *hitbox = component<IRComponents::C_HitBox2DGui>(id);
        if (hitbox != nullptr) {
            const GuiRect hitRect = isDropdown ? rect : GuiRect{pos, hitbox->size_};
            m_layout.elements_.push_back({id, hitRect, Surface::HIT, true, bodyCovers});
        }
        addWidgetText(id, widget, pos, rect, hitbox != nullptr, textCovers);
    }

    void addLabel(IREntity::EntityId id, const IRComponents::C_Widget &widget, IRMath::ivec2 pos) {
        const auto *labelData = component<IRComponents::C_WidgetLabel>(id);
        if (labelData == nullptr) {
            return;
        }
        const GuiRect extent = runExtent(IRPrefab::WidgetText::label(*labelData, pos));
        if (isEmpty(extent)) {
            return;
        }
        m_layout.widgets_.push_back({id, widget.kind_, extent, pos});
        addText(id, extent, std::nullopt, widget.coversBelow_);
    }

    void addWidgetText(
        IREntity::EntityId id,
        const IRComponents::C_Widget &widget,
        IRMath::ivec2 pos,
        const GuiRect &rect,
        bool interactive,
        bool covers
    ) {
        namespace Text = IRPrefab::WidgetText;
        if (const auto *button = component<IRComponents::C_WidgetButton>(id)) {
            addRun(id, Text::buttonCaption(widget, *button, pos), rect, covers);
        }
        if (const auto *checkbox = component<IRComponents::C_WidgetCheckbox>(id)) {
            addRun(id, Text::checkboxCaption(widget, *checkbox, pos, m_theme), rect, covers);
        }
        if (const auto *radio = component<IRComponents::C_WidgetRadio>(id)) {
            addRun(id, Text::radioCaption(widget, *radio, pos, m_theme), rect, covers);
        }
        if (const auto *slider = component<IRComponents::C_WidgetSlider>(id)) {
            addRun(id, Text::sliderLabel(*slider, pos), rect, covers);
            if (!slider->label_.empty()) {
                const Text::SliderValueText value = Text::sliderValueText(*slider);
                addRun(id, Text::sliderValue(widget, *slider, pos, value), rect, covers);
            }
        }
        if (const auto *panel = component<IRComponents::C_WidgetPanel>(id)) {
            addRun(id, Text::panelTitle(widget, *panel, pos, m_theme), rect);
        }
        if (const auto *field = component<IRComponents::C_WidgetNumberField>(id)) {
            field->formatValue(m_formatted);
            const std::string_view shown = Text::numberFieldShownText(*field, m_formatted);
            addRun(id, Text::numberFieldText(widget, *field, pos, m_theme, shown), rect, covers);
        }
        if (const auto *textInput = component<IRComponents::C_WidgetTextInput>(id)) {
            addRun(id, Text::textInputText(widget, *textInput, pos, m_theme), rect, covers);
        }
        if (const auto *list = component<IRComponents::C_WidgetList>(id)) {
            const int rows = list->visibleRows(widget.size_.y);
            const int top = list->topIndex(widget.size_.y);
            const int n = static_cast<int>(list->items_.size());
            for (int row = 0; row < rows && top + row < n; ++row) {
                addRun(id, Text::listRow(*list, pos, m_theme, row, top + row), rect, covers);
            }
        }
        if (const auto *dd = component<IRComponents::C_WidgetDropdown>(id)) {
            addRun(
                id,
                Text::dropdownHeader(widget, *dd, pos, m_theme),
                Text::dropdownHeaderTextRegion(widget, pos, m_theme)
            );
            const int n = static_cast<int>(dd->items_.size());
            if (dd->isOpen_ && n > 0 && !widget.coversBelow_) {
                const GuiRect strip = Text::dropdownStripRect(widget, *dd, pos);
                m_layout.elements_.push_back({id, strip, Surface::STRIP, interactive, false});
                for (int i = 0; i < n; ++i) {
                    addRun(id, Text::dropdownStripRow(widget, *dd, pos, m_theme, i), strip);
                }
            }
        }
    }

    void addFreeText(IREntity::EntityId id, const IRComponents::C_TextSegment &segment) {
        const auto *guiPos = component<IRComponents::C_GuiPosition>(id);
        const auto *style = component<IRComponents::C_TextStyle>(id);
        if (guiPos == nullptr || style == nullptr ||
            component<IRComponents::C_GuiElement>(id) == nullptr) {
            return;
        }
        m_scratch.clear();
        IRPrefab::WidgetText::queueStyledText(
            m_scratch,
            segment.text_,
            guiPos->pos_,
            m_canvasSize,
            *style,
            m_styledScratch
        );
        addText(id, IRPrefab::WidgetText::drawnCellBounds(m_scratch, m_fontBuffer), std::nullopt);
    }

    IRMath::ivec2 m_canvasSize;
    IRPrefab::Widget::WidgetTheme m_theme;
    std::vector<std::uint32_t> m_fontBuffer;
    std::vector<IRRender::GlyphDrawCommand> m_scratch;
    IRPrefab::WidgetText::StyledTextScratch m_styledScratch;
    std::string m_formatted;
    Layout m_layout;
};

using PairKey = std::pair<IREntity::EntityId, IREntity::EntityId>;

inline PairKey pairKey(IREntity::EntityId a, IREntity::EntityId b) {
    return a < b ? PairKey{a, b} : PairKey{b, a};
}

inline bool showsText(Surface surface) {
    return surface == Surface::BODY || surface == Surface::STRIP || surface == Surface::TEXT;
}

inline Finding
makeFinding(FindingKind kind, IREntity::EntityId a, IREntity::EntityId b, const GuiRect &rect) {
    return Finding{kind, a, b, rect.pos_, rect.size_};
}

inline void pairFindings(const Layout &layout, std::vector<Finding> &findings) {
    std::map<PairKey, GuiRect> overlaps;
    std::map<PairKey, GuiRect> textCrossings;
    const std::vector<Element> &elements = layout.elements_;
    for (std::size_t i = 0; i < elements.size(); ++i) {
        const Element &first = elements[i];
        for (std::size_t j = i + 1; j < elements.size(); ++j) {
            const Element &second = elements[j];
            if (first.owner_ == second.owner_ || first.covers_ || second.covers_) {
                continue;
            }
            const GuiRect hit = intersection(first.rect_, second.rect_);
            if (isEmpty(hit)) {
                continue;
            }
            const PairKey key = pairKey(first.owner_, second.owner_);
            if (first.interactive_ && second.interactive_) {
                overlaps[key] = boundingBox(overlaps[key], hit);
            }
            const bool textPair = (first.surface_ == Surface::TEXT && showsText(second.surface_)) ||
                                  (second.surface_ == Surface::TEXT && showsText(first.surface_));
            if (textPair) {
                textCrossings[key] = boundingBox(textCrossings[key], hit);
            }
        }
    }
    for (const auto &[key, rect] : overlaps) {
        findings.push_back(makeFinding(FindingKind::OVERLAP, key.first, key.second, rect));
    }
    for (const auto &[key, rect] : textCrossings) {
        if (overlaps.count(key) == 0) {
            findings.push_back(
                makeFinding(FindingKind::TEXT_OVER_WIDGET, key.first, key.second, rect)
            );
        }
    }
}

inline void
boundsFindings(const Layout &layout, const GuiRect &canvas, std::vector<Finding> &findings) {
    for (const Element &element : layout.elements_) {
        if (element.surface_ != Surface::TEXT) {
            continue;
        }
        const bool passesRegion =
            element.hasTextRegion_ && !contains(element.textRegion_, element.rect_);
        const GuiRect bound =
            element.hasTextRegion_ ? intersection(element.textRegion_, canvas) : canvas;
        const GuiRect outside = outsidePart(element.rect_, bound);
        if (isEmpty(outside)) {
            continue;
        }
        findings.push_back(makeFinding(
            FindingKind::TEXT_CLIPPED,
            element.owner_,
            passesRegion ? element.owner_ : IREntity::kNullEntity,
            outside
        ));
    }

    for (const WidgetBox &widget : layout.widgets_) {
        const WidgetBox *parent = nullptr;
        for (const WidgetBox &panel : layout.widgets_) {
            if (panel.kind_ != IRComponents::WidgetKind::PANEL || panel.id_ == widget.id_ ||
                !contains(panel.rect_, GuiRect{widget.origin_, IRMath::ivec2(1)})) {
                continue;
            }
            const int area = panel.rect_.size_.x * panel.rect_.size_.y;
            if (parent == nullptr) {
                parent = &panel;
                continue;
            }
            const int parentArea = parent->rect_.size_.x * parent->rect_.size_.y;
            if (area < parentArea || (area == parentArea && panel.id_ < parent->id_)) {
                parent = &panel;
            }
        }
        if (parent != nullptr) {
            const GuiRect outside = outsidePart(widget.rect_, parent->rect_);
            if (!isEmpty(outside)) {
                findings.push_back(
                    makeFinding(FindingKind::OUTSIDE_PARENT, widget.id_, parent->id_, outside)
                );
            }
        }
        if (widget.kind_ != IRComponents::WidgetKind::LABEL) {
            const GuiRect outside = outsidePart(widget.rect_, canvas);
            if (!isEmpty(outside)) {
                findings.push_back(
                    makeFinding(FindingKind::OFFSCREEN, widget.id_, IREntity::kNullEntity, outside)
                );
            }
        }
    }
}

inline auto sortKey(const Finding &finding) {
    return std::make_tuple(
        static_cast<int>(finding.kind_),
        finding.a_,
        finding.b_,
        finding.pos_.x,
        finding.pos_.y,
        finding.size_.x,
        finding.size_.y
    );
}

} // namespace detail

// Every finding over the live widget and free-text set, laid out against a
// GUI canvas of @p canvasSize — the headless form.
inline std::vector<Finding> report(IRMath::ivec2 canvasSize) {
    const detail::Layout layout = detail::Collector{canvasSize}.collect();
    std::vector<Finding> findings;
    detail::pairFindings(layout, findings);
    detail::boundsFindings(layout, GuiRect{IRMath::ivec2(0), canvasSize}, findings);
    std::sort(findings.begin(), findings.end(), [](const Finding &lhs, const Finding &rhs) {
        return detail::sortKey(lhs) < detail::sortKey(rhs);
    });
    return findings;
}

// The same against the live "gui" canvas; empty when there is none.
inline std::vector<Finding> report() {
    if (IRRender::g_renderManager == nullptr) {
        return {};
    }
    const IREntity::EntityId guiCanvas = IRRender::getCanvas("gui");
    if (guiCanvas == IREntity::kNullEntity) {
        return {};
    }
    return report(IREntity::getComponent<IRComponents::C_TriangleCanvasTextures>(guiCanvas).size_);
}

// Logs the live report for one capture frame, the lines gui-verify parses:
//   GUI-LAYOUT <kind> <idA> <idB> <x> <y> <w> <h> shot=<i> label=<shot>
//   GUI-LAYOUT-SUMMARY <n> shot=<i> label=<shot>
// The summary is logged with n = 0 too, so a shot that never reached the
// audit is told apart from a clean one.
inline void logReport(int shotIndex, const char *shotLabel) {
    const std::vector<Finding> findings = report();
    const char *label = shotLabel != nullptr ? shotLabel : "";
    for (const Finding &finding : findings) {
        IR_LOG_INFO(
            "GUI-LAYOUT {} {} {} {} {} {} {} shot={} label={}",
            kindName(finding.kind_),
            finding.a_,
            finding.b_,
            finding.pos_.x,
            finding.pos_.y,
            finding.size_.x,
            finding.size_.y,
            shotIndex,
            label
        );
    }
    IR_LOG_INFO("GUI-LAYOUT-SUMMARY {} shot={} label={}", findings.size(), shotIndex, label);
}

} // namespace IRPrefab::GuiAudit

#endif /* GUI_LAYOUT_AUDIT_H */
