#ifndef EDITOR_DOCKS_H
#define EDITOR_DOCKS_H

#include <irreden/render/layout.hpp>
#include <irreden/render/trixel_rect.hpp>
#include <irreden/render/widgets.hpp>
#include <irreden/ir_input.hpp>
#include <algorithm>
#include <array>
#include <span>
#include <unordered_map>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <unordered_set>

namespace IRVoxelEditor {

enum class DockSide { LEFT, RIGHT, FLOATING };

class EditorDocks {
    using Id = IREntity::EntityId;
    using Point = IRMath::ivec2;
    struct Slot {
        DockSide side_;
        Point point_;
    };
    struct Member {
        Id id_;
        Point home_;
    };
    struct Group {
        std::string key_;
        Id panel_;
        Point home_, size_, pos_;
        Slot default_, slot_;
        std::vector<Member> members_;
        bool visible_ = false;
    };
    std::filesystem::path m_path;
    std::vector<Group> m_groups;
    std::array<Id, 2> m_rails{IREntity::kNullEntity, IREntity::kNullEntity};
    Point m_canvas{0}, m_grab{0}, m_start{0};
    std::array<int, 2> m_widths{}, m_scroll{};
    std::unordered_map<Id, bool> m_clipped;
    std::unordered_map<Id, std::string> m_clippedTitles;
    std::unordered_map<std::string, Slot> m_unmatched;
    std::string m_status;
    bool m_pointerOwned = false, m_joined = false;
    int m_drag = -1;
    bool m_moved = false, m_dirty = true;
    IRRender::RectFillScratch m_scratch;
    static constexpr int kTop = 108, kGap = 4;

    Group *find(std::string_view key) {
        for (auto &g : m_groups)
            if (g.key_ == key)
                return &g;
        return nullptr;
    }
    static bool inside(Point p, Point origin, Point size) {
        return p.x >= origin.x && p.y >= origin.y && p.x < origin.x + size.x &&
               p.y < origin.y + size.y;
    }
    Point clamp(Point pos, Point size) const {
        return IRMath::clamp(
            pos,
            Point(kGap, kTop),
            IRMath::max(Point(kGap, kTop), m_canvas - size - Point(kGap))
        );
    }
    void move(Group &g, Point pos) {
        if (g.pos_ == pos)
            return;
        g.pos_ = pos;
        std::erase_if(g.members_, [](const Member &m) { return !IREntity::entityExists(m.id_); });
        for (const auto &m : g.members_)
            IREntity::getComponent<IRComponents::C_GuiPosition>(m.id_).pos_ =
                m.home_ + pos - g.home_;
    }
    int sideFor(const Group &g) const {
        return m_joined ? 0 : static_cast<int>(g.slot_.side_);
    }
    void arrange() {
        if (m_canvas.x <= 0 || m_canvas.y <= 0)
            return;
        m_widths = {};
        for (const auto &g : m_groups)
            if (g.visible_ && g.slot_.side_ != DockSide::FLOATING) {
                const int side = static_cast<int>(g.slot_.side_);
                m_widths[side] = IRMath::max(m_widths[side], g.size_.x + 2 * kGap);
            }
        m_joined = m_widths[0] + m_widths[1] + 64 > m_canvas.x;
        if (m_joined) {
            m_widths[0] = IRMath::max(m_widths[0], m_widths[1]);
            m_widths[1] = 0;
        }
        for (int side = 0; side < 2; ++side) {
            std::vector<Group *> docked;
            int contentHeight = kGap;
            for (auto &g : m_groups)
                if (g.visible_ && g.slot_.side_ != DockSide::FLOATING && sideFor(g) == side) {
                    docked.push_back(&g);
                    contentHeight += g.size_.y + kGap;
                }
            std::stable_sort(docked.begin(), docked.end(), [](const Group *a, const Group *b) {
                return a->slot_.point_.y < b->slot_.point_.y;
            });
            m_scroll[side] = IRMath::clamp(
                m_scroll[side],
                0,
                IRMath::max(0, contentHeight - (m_canvas.y - kTop))
            );
            int y = kTop + kGap - m_scroll[side];
            for (auto *g : docked) {
                const int x = side == 0 ? kGap : m_canvas.x - g->size_.x - kGap;
                move(*g, {x, y});
                y += g->size_.y + kGap;
            }
            const auto id = m_rails[side];
            const Point size(m_widths[side], IRMath::max(0, m_canvas.y - kTop));
            IREntity::getComponent<IRComponents::C_Widget>(id).size_ = size;
            IREntity::getComponent<IRComponents::C_HitBox2DGui>(id).size_ = size;
            IREntity::getComponent<IRComponents::C_GuiPosition>(id).pos_ = {
                side == 0 ? 0 : m_canvas.x - size.x,
                kTop
            };
            IRPrefab::Widget::setVisible(id, size.x > 0 && size.y > 0);
        }
        for (auto &g : m_groups)
            if (g.visible_ && g.slot_.side_ == DockSide::FLOATING) {
                if (g.size_.y > m_canvas.y - kTop - 2 * kGap ||
                    g.size_.x > m_canvas.x - m_widths[0] - m_widths[1] - 2 * kGap) {
                    g.slot_.side_ = DockSide::LEFT;
                    arrange();
                    return;
                }
                auto pos = clamp(g.slot_.point_, g.size_);
                pos.x = IRMath::clamp(
                    pos.x,
                    m_widths[0] + kGap,
                    m_canvas.x - m_widths[1] - g.size_.x - kGap
                );
                bool overlap = false;
                for (const auto &other : m_groups)
                    if (&other != &g && other.visible_ && other.slot_.side_ == DockSide::FLOATING)
                        overlap = overlap || (pos.x < other.pos_.x + other.size_.x &&
                                              other.pos_.x < pos.x + g.size_.x &&
                                              pos.y < other.pos_.y + other.size_.y &&
                                              other.pos_.y < pos.y + g.size_.y);
                if (overlap) {
                    g.slot_.side_ = DockSide::LEFT;
                    arrange();
                    return;
                }
                move(g, pos);
            }
        m_dirty = false;
    }
    void clip() {
        for (const auto &g : m_groups) {
            if (!g.visible_)
                continue;
            const int side = sideFor(g);
            const Point minimum(
                g.slot_.side_ == DockSide::FLOATING ? 0
                : side == 0                         ? 0
                                                    : m_canvas.x - m_widths[side],
                kTop
            );
            const Point maximum(
                g.slot_.side_ == DockSide::FLOATING ? m_canvas.x
                : side == 0                         ? m_widths[side]
                                                    : m_canvas.x,
                m_canvas.y
            );
            for (const auto &member : g.members_) {
                if (!IREntity::entityExists(member.id_) || !IRPrefab::Widget::isVisible(member.id_))
                    continue;
                auto &widget = IREntity::getComponent<IRComponents::C_Widget>(member.id_);
                auto &pos = IREntity::getComponent<IRComponents::C_GuiPosition>(member.id_).pos_;
                const auto end = pos + widget.size_;
                const auto low = IRMath::max(minimum, pos), high = IRMath::min(maximum, end);
                if (member.id_ == g.panel_ && high.x > low.x && high.y > low.y) {
                    if (low.y > pos.y) {
                        auto &title =
                            IREntity::getComponent<IRComponents::C_WidgetPanel>(member.id_).title_;
                        if (!title.empty()) {
                            m_clippedTitles.emplace(member.id_, std::move(title));
                            title.clear();
                        }
                    }
                    pos = low;
                    widget.size_ = high - low;
                    if (auto box =
                            IREntity::getComponentOptional<IRComponents::C_HitBox2DGui>(member.id_))
                        (*box)->size_ = widget.size_;
                } else if (
                    pos.x < minimum.x || pos.y < minimum.y || end.x > maximum.x || end.y > maximum.y
                ) {
                    m_clipped.emplace(member.id_, true);
                    IRPrefab::Widget::setVisible(member.id_, false);
                }
            }
        }
    }
    void saveAfterGesture() {
        m_status = save() ? "" : "Layout could not be saved. Check the layout file location.";
    }
    bool save() const {
        if (m_path.empty())
            return false;
        std::error_code ec;
        if (!m_path.parent_path().empty())
            std::filesystem::create_directories(m_path.parent_path(), ec);
        if (ec)
            return false;
        auto temporary = m_path;
        temporary += ".tmp";
        std::ofstream stream(temporary, std::ios::trunc);
        stream << "IRREDEN_EDITOR_LAYOUT 1\n";
        for (const auto &g : m_groups)
            stream << std::quoted(g.key_) << ' ' << static_cast<int>(g.slot_.side_) << ' '
                   << g.slot_.point_.x << ' ' << g.slot_.point_.y << '\n';
        for (const auto &[key, slot] : m_unmatched)
            stream << std::quoted(key) << ' ' << static_cast<int>(slot.side_) << ' '
                   << slot.point_.x << ' ' << slot.point_.y << '\n';
        stream.close();
        if (!stream)
            return false;
        std::filesystem::rename(temporary, m_path, ec);
        return !ec;
    }

  public:
    const std::filesystem::path &path() const {
        return m_path;
    }
    const std::string &status() const {
        return m_status;
    }
    void restoreClipped() {
        for (auto &[id, title] : m_clippedTitles) {
            if (!IREntity::entityExists(id))
                continue;
            auto &current = IREntity::getComponent<IRComponents::C_WidgetPanel>(id).title_;
            if (current.empty())
                current = std::move(title);
        }
        m_clippedTitles.clear();
        for (const auto &[id, visible] : m_clipped)
            if (IREntity::entityExists(id))
                IRPrefab::Widget::setVisible(id, visible);
        m_clipped.clear();
        for (const auto &g : m_groups) {
            IREntity::getComponent<IRComponents::C_Widget>(g.panel_).size_ = g.size_;
            IREntity::getComponent<IRComponents::C_GuiPosition>(g.panel_).pos_ = g.pos_;
            if (auto box = IREntity::getComponentOptional<IRComponents::C_HitBox2DGui>(g.panel_))
                (*box)->size_ = g.size_;
        }
    }
    bool cancelDrag() {
        if (m_drag < 0)
            return false;
        m_drag = -1;
        m_pointerOwned = true;
        arrange();
        return true;
    }
    void configure(std::filesystem::path path) {
        m_path = std::move(path);
        for (auto &id : m_rails) {
            id = IRPrefab::Widget::makeHitRegion({0, 0}, {1, 1}, -100);
            auto &widget = IREntity::getComponent<IRComponents::C_Widget>(id);
            widget.kind_ = IRComponents::WidgetKind::PANEL;
            widget.coversBelow_ = true;
            // The ordered backdrop pass owns rendering; no C_WidgetPanel renderer participates.
            IRPrefab::Widget::setVisible(id, false);
        }
    }
    using Snapshot = std::unordered_set<Id>;
    Snapshot before() const {
        Snapshot ids;
        if (!m_path.empty())
            IREntity::forEachComponent<IRComponents::C_Widget>([&](Id id, const auto &) {
                ids.insert(id);
            });
        return ids;
    }
    void add(std::string key, Id panel, DockSide side, std::span<const Id> members) {
        if (m_path.empty())
            return;
        const auto home = IREntity::getComponent<IRComponents::C_GuiPosition>(panel).pos_;
        const auto size = IREntity::getComponent<IRComponents::C_Widget>(panel).size_;
        const Slot slot{side, home};
        auto &g =
            m_groups.emplace_back(Group{std::move(key), panel, home, size, home, slot, slot, {}});
        for (auto id : members)
            if (auto pos = IREntity::getComponentOptional<IRComponents::C_GuiPosition>(id))
                g.members_.push_back({id, (*pos)->pos_});
    }
    void collect(std::string key, Id panel, DockSide side, const Snapshot &previous) {
        if (m_path.empty())
            return;
        std::vector<Id> ids;
        IREntity::forEachComponent<IRComponents::C_Widget>([&](Id id, const auto &) {
            if (!previous.contains(id))
                ids.push_back(id);
        });
        add(std::move(key), panel, side, ids);
    }
    void attach(std::string_view key, Id widget) {
        auto *g = find(key);
        if (!g || widget == IREntity::kNullEntity)
            return;
        for (const auto &m : g->members_)
            if (m.id_ == widget)
                return;
        auto &pos = IREntity::getComponent<IRComponents::C_GuiPosition>(widget).pos_;
        std::erase_if(g->members_, [&](const Member &m) {
            return m.id_ == widget || !IREntity::entityExists(m.id_);
        });
        g->members_.push_back({widget, pos});
        pos += g->pos_ - g->home_;
    }
    Point homePosition(Id id, Point fallback) const {
        for (const auto rail : m_rails)
            if (rail == id)
                return {-1, -1};
        for (const auto &g : m_groups)
            for (const auto &m : g.members_)
                if (m.id_ == id)
                    return m.home_;
        return fallback;
    }
    Point position(std::string_view key) const {
        for (const auto &g : m_groups)
            if (g.key_ == key)
                return g.pos_;
        return {-1, -1};
    }
    bool contains(IRMath::vec2 mouse) const {
        if (m_path.empty())
            return false;
        if (m_drag >= 0 || m_pointerOwned)
            return true;
        const Point p(IRMath::floor(mouse));
        if (p.y >= kTop && (p.x < m_widths[0] || p.x >= m_canvas.x - m_widths[1]))
            return true;
        for (const auto &g : m_groups)
            if (g.visible_ && inside(p, g.pos_, g.size_))
                return true;
        return false;
    }
    void reset(bool persist = true) {
        for (auto &g : m_groups)
            g.slot_ = g.default_;
        m_drag = -1;
        m_dirty = true;
        m_scroll = {};
        if (!m_path.empty()) {
            arrange();
            if (persist)
                saveAfterGesture();
        }
    }
    bool load() {
        if (m_path.empty())
            return false;
        std::ifstream stream(m_path);
        std::string magic;
        int version = 0;
        if (!(stream >> magic >> version) || magic != "IRREDEN_EDITOR_LAYOUT" || version != 1)
            return false;
        auto slots = std::vector<Slot>{};
        for (const auto &g : m_groups)
            slots.push_back(g.default_);
        std::unordered_map<std::string, Slot> unmatched;
        std::string key;
        int side;
        Point point;
        while (stream >> std::ws && !stream.eof()) {
            if (!(stream >> std::quoted(key) >> side >> point.x >> point.y) || side < 0 ||
                side > 2 || point.x < -100000 || point.x > 100000 || point.y < -100000 ||
                point.y > 100000)
                return false;
            if (auto *g = find(key))
                slots[static_cast<std::size_t>(g - m_groups.data())] = {
                    static_cast<DockSide>(side),
                    point
                };
            else
                unmatched[key] = {static_cast<DockSide>(side), point};
            if (unmatched.size() > 256)
                return false;
        }
        for (std::size_t i = 0; i < slots.size(); ++i)
            m_groups[i].slot_ = slots[i];
        m_unmatched = std::move(unmatched);
        m_drag = -1;
        m_dirty = true;
        arrange();
        return true;
    }
    bool update(bool allowDrag) {
        m_pointerOwned = false;
        if (m_path.empty())
            return false;
        const Point canvas = IRPrefab::Layout::guiCanvasSize();
        if (m_canvas != canvas) {
            m_canvas = canvas;
            m_dirty = true;
        }
        for (auto &g : m_groups) {
            const bool visible = IRPrefab::Widget::isVisible(g.panel_);
            if (visible != g.visible_) {
                g.visible_ = visible;
                m_dirty = true;
            }
        }
        if (m_dirty && m_drag < 0)
            arrange();
        const Point mouse(IRMath::floor(IRPrefab::Layout::mousePositionInGuiTrixels()));
        if (!allowDrag || (m_drag >= 0 && !m_groups[m_drag].visible_)) {
            if (m_drag >= 0) {
                m_drag = -1;
                arrange();
            }
            clip();
            return false;
        }
        const auto hovered = IRPrefab::Widget::hoveredWidget();
        bool scrollSurface = hovered == m_rails[0] || hovered == m_rails[1];
        for (const auto &g : m_groups)
            scrollSurface =
                scrollSurface || (hovered == g.panel_ && g.slot_.side_ != DockSide::FLOATING);
        const bool overRail =
            mouse.y >= kTop && (mouse.x < m_widths[0] || mouse.x >= m_canvas.x - m_widths[1]);
        if (m_drag < 0 && scrollSurface && overRail) {
            const auto wheel = IRInput::scrollOffset().y;
            if (wheel != 0) {
                const int side = mouse.x < m_widths[0] ? 0 : 1;
                m_scroll[side] -= IRMath::roundHalfUp(wheel * 36.0f);
                arrange();
            }
        }
        clip();
        if (m_drag < 0) {
            if (IRInput::heldKeyModifiers() != IRInput::kModifierNone ||
                !IRInput::checkKeyMouseButton(IRInput::kMouseButtonLeft, IRInput::PRESSED))
                return false;
            for (int i = static_cast<int>(m_groups.size()) - 1; i >= 0; --i) {
                const auto &g = m_groups[i];
                if (g.visible_ && mouse.y >= kTop && inside(mouse, g.pos_, {g.size_.x, 15})) {
                    m_drag = i;
                    m_start = mouse;
                    m_grab = mouse - g.pos_;
                    m_moved = false;
                    break;
                }
            }
            if (m_drag < 0)
                return false;
        }
        m_pointerOwned = true;
        auto &g = m_groups[m_drag];
        const auto delta = IRMath::abs(mouse - m_start);
        m_moved = m_moved || delta.x >= 3 || delta.y >= 3;
        const auto pos = clamp(mouse - m_grab, g.size_);
        if (m_moved)
            move(g, pos);
        if (IRInput::checkKeyMouseButton(IRInput::kMouseButtonLeft, IRInput::RELEASED)) {
            if (m_moved) {
                const auto side = mouse.x <= IRMath::max(14, m_widths[0]) || pos.x <= kGap + 8
                                      ? DockSide::LEFT
                                  : mouse.x >= canvas.x - IRMath::max(14, m_widths[1]) ||
                                          pos.x + g.size_.x >= canvas.x - kGap - 8
                                      ? DockSide::RIGHT
                                      : DockSide::FLOATING;
                g.slot_ = {side, pos};
                if (side != DockSide::FLOATING) {
                    for (auto &other : m_groups) {
                        if (&other != &g && other.visible_ && other.slot_.side_ == side)
                            other.slot_.point_.y = other.pos_.y + other.size_.y / 2;
                    }
                    g.slot_.point_.y = mouse.y;
                }
            }
            m_drag = -1;
            arrange();
            if (m_moved)
                saveAfterGesture();
        }
        return true;
    }
    void draw(IRComponents::C_TriangleCanvasTextures &canvas) {
        for (int side = 0; side < 2; ++side) {
            const int width = m_widths[side];
            if (width <= 0)
                continue;
            const int x = side == 0 ? 0 : m_canvas.x - width;
            const int height = m_canvas.y - kTop;
            IRRender::fillRect(
                canvas,
                {x, kTop},
                {width, height},
                {18, 24, 31, 255},
                IRRender::kWidgetBackgroundDistance + 2,
                m_scratch
            );
            IRRender::fillRect(
                canvas,
                {side == 0 ? x + width - 2 : x, kTop},
                {2, height},
                {80, 104, 117, 255},
                IRRender::kWidgetBorderDistance,
                m_scratch
            );
        }
    }
};
} // namespace IRVoxelEditor
#endif
