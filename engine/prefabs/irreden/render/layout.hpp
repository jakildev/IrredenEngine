#ifndef IRREDEN_LAYOUT_H
#define IRREDEN_LAYOUT_H

#include <irreden/ir_entity.hpp>
#include <irreden/ir_math.hpp>
#include <irreden/ir_render.hpp>

#include <irreden/common/components/component_tags_all.hpp>
#include <irreden/render/components/component_gui_position.hpp>
#include <irreden/render/components/component_widget.hpp>
#include <irreden/render/components/component_layout_leaf.hpp>
#include <irreden/render/components/component_layout_state.hpp>
#include <irreden/render/components/component_splitter.hpp>
#include <irreden/render/components/component_triangle_canvas_textures.hpp>
#include <irreden/render/components/component_trixel_framebuffer.hpp>
#include <irreden/input/components/component_hitbox_2d_gui.hpp>
#include <irreden/render/layout_types.hpp>

#include <string>
#include <vector>
#include <string_view>
#include <cctype>
#include <cstdint>
#include <limits>

namespace IRPrefab::Layout {

// -----------------------------------------------------------------------
// Singleton state accessor
// -----------------------------------------------------------------------

inline IRComponents::C_LayoutState &getLayout() {
    return IREntity::singleton<IRComponents::C_LayoutState>();
}

// -----------------------------------------------------------------------
// Tree-building API (called once at init)
// -----------------------------------------------------------------------

// Returns new node index.
inline int addNode(LayoutNode node, int parentIdx) {
    auto &ls = getLayout();
    int idx = static_cast<int>(ls.nodes_.size());
    node.parent_ = parentIdx;
    ls.nodes_.push_back(std::move(node));
    if (parentIdx >= 0) {
        ls.nodes_[parentIdx].children_.push_back(idx);
    }
    return idx;
}

inline bool isContainerNode(const LayoutNode &node) {
    return node.type_ == LayoutNode::Type::ROW || node.type_ == LayoutNode::Type::COLUMN;
}

inline int makeRow(int parentIdx, SizeSpec spec, std::string id = "") {
    LayoutNode n;
    n.type_ = LayoutNode::Type::ROW;
    n.spec_ = spec;
    n.id_ = std::move(id);
    return addNode(std::move(n), parentIdx);
}

inline int makeColumn(int parentIdx, SizeSpec spec, std::string id = "") {
    LayoutNode n;
    n.type_ = LayoutNode::Type::COLUMN;
    n.spec_ = spec;
    n.id_ = std::move(id);
    return addNode(std::move(n), parentIdx);
}

inline int makeLeaf(int parentIdx, SizeSpec spec, IREntity::EntityId widget, std::string id = "") {
    auto &ls = getLayout();
    LayoutNode n;
    n.type_ = LayoutNode::Type::LEAF;
    n.spec_ = spec;
    n.id_ = std::move(id);
    n.widgetEntity_ = widget;
    // Stamp the C_LayoutLeaf component onto the widget entity
    int idx = static_cast<int>(ls.nodes_.size());
    n.parent_ = parentIdx;
    ls.nodes_.push_back(std::move(n));
    if (parentIdx >= 0) {
        ls.nodes_[parentIdx].children_.push_back(idx);
    }
    IREntity::setComponent(widget, IRComponents::C_LayoutLeaf{idx});
    return idx;
}

// Call after all nodes are added. Creates splitter entities between every
// pair of adjacent children in ROW/COLUMN nodes.
inline void buildSplitters();

// Set root bounds and root node index. Call before any compute().
inline void setRoot(int nodeIdx, IRMath::ivec2 pos, IRMath::ivec2 size) {
    auto &ls = getLayout();
    ls.root_ = nodeIdx;
    ls.rootPos_ = pos;
    ls.rootSize_ = size;
}

inline std::uint64_t revision() {
    return getLayout().revision_;
}

inline IRMath::ivec4 leafRect(IREntity::EntityId widget) {
    const auto &leaf = IREntity::getComponent<IRComponents::C_LayoutLeaf>(widget);
    const auto &node = getLayout().nodes_[leaf.nodeIdx_];
    return IRMath::ivec4(node.pos_.x, node.pos_.y, node.size_.x, node.size_.y);
}

// -----------------------------------------------------------------------
// Layout computation (called each frame by LAYOUT_COMPUTE beginTick)
// -----------------------------------------------------------------------

inline void computeNode(int nodeIdx, IRMath::ivec2 pos, IRMath::ivec2 size);

inline void compute() {
    auto &ls = getLayout();
    if (ls.root_ < 0)
        return;
    ls.rectanglesChanged_ = false;
    computeNode(ls.root_, ls.rootPos_, ls.rootSize_);
    if (ls.rectanglesChanged_) {
        ++ls.revision_;
    }
}

inline const LayoutNode &getNode(int idx) {
    return getLayout().nodes_[idx];
}

inline void computeNode(int nodeIdx, IRMath::ivec2 pos, IRMath::ivec2 size) {
    auto &ls = getLayout();
    auto &node = ls.nodes_[nodeIdx];
    ls.rectanglesChanged_ = ls.rectanglesChanged_ || node.pos_ != pos || node.size_ != size;
    node.pos_ = pos;
    node.size_ = size;

    if (node.type_ == LayoutNode::Type::LEAF) {
        // Positions are pulled by LAYOUT_COMPUTE tick; nothing to recurse.
        return;
    }

    const bool isRow = (node.type_ == LayoutNode::Type::ROW);
    const int axis = isRow ? 0 : 1;
    const int cross = isRow ? 1 : 0;

    const int numChildren = static_cast<int>(node.children_.size());
    if (numChildren == 0)
        return;

    const int numSplitters = numChildren - 1;
    const int splitterTotalPx = numSplitters * kSplitterThickness;
    const int availableMain = size[axis] - splitterTotalPx;

    // Compute fixed total and fraction weight total
    int fixedTotal = 0;
    float fractionTotal = 0.0f;
    for (int ci : node.children_) {
        auto &child = ls.nodes_[ci];
        if (child.spec_.mode_ == SizeMode::FIXED_PX) {
            fixedTotal += IRMath::clamp(
                static_cast<int>(child.spec_.value_),
                child.spec_.minPx_,
                child.spec_.maxPx_
            );
        } else {
            fractionTotal += child.spec_.value_;
        }
    }
    const int fractionPool = availableMain - fixedTotal;

    // Assign positions and recurse
    int cursor = pos[axis];
    for (int i = 0; i < numChildren; ++i) {
        const int ci = node.children_[i];
        auto &child = ls.nodes_[ci];

        int childMain;
        if (child.spec_.mode_ == SizeMode::FIXED_PX) {
            childMain = IRMath::clamp(
                static_cast<int>(child.spec_.value_),
                child.spec_.minPx_,
                child.spec_.maxPx_
            );
        } else {
            const float frac = (fractionTotal > 0.0f) ? (child.spec_.value_ / fractionTotal) : 1.0f;
            childMain = IRMath::clamp(
                static_cast<int>(frac * static_cast<float>(fractionPool)),
                child.spec_.minPx_,
                child.spec_.maxPx_
            );
        }

        IRMath::ivec2 childPos;
        childPos[axis] = cursor;
        childPos[cross] = pos[cross];
        IRMath::ivec2 childSize;
        childSize[axis] = childMain;
        childSize[cross] = size[cross];

        computeNode(ci, childPos, childSize);
        cursor += childMain;

        // Position splitter entity between this child and the next
        if (i < numSplitters && i < static_cast<int>(node.splitterEntities_.size())) {
            const IREntity::EntityId se = node.splitterEntities_[i];
            if (se != IREntity::kNullEntity) {
                auto &sp = IREntity::getComponent<IRComponents::C_GuiPosition>(se);
                sp.pos_[axis] = cursor;
                sp.pos_[cross] = pos[cross];

                IRMath::ivec2 splitterSize;
                splitterSize[axis] = kSplitterThickness;
                splitterSize[cross] = size[cross];

                auto &sw = IREntity::getComponent<IRComponents::C_Widget>(se);
                sw.size_ = splitterSize;
                auto &sh = IREntity::getComponent<IRComponents::C_HitBox2DGui>(se);
                sh.size_ = splitterSize;
            }
            cursor += kSplitterThickness;
        }
    }
}

// -----------------------------------------------------------------------
// Splitter entity creation
// -----------------------------------------------------------------------

inline void buildSplitters() {
    auto &ls = getLayout();
    for (int pi = 0; pi < static_cast<int>(ls.nodes_.size()); ++pi) {
        auto &node = ls.nodes_[pi];
        if (node.type_ == LayoutNode::Type::LEAF)
            continue;
        const int numChildren = static_cast<int>(node.children_.size());
        const bool isRow = (node.type_ == LayoutNode::Type::ROW);

        node.splitterEntities_.resize(numChildren > 0 ? numChildren - 1 : 0, IREntity::kNullEntity);

        for (int i = 0; i < numChildren - 1; ++i) {
            IRComponents::C_Splitter splitterData;
            splitterData.parentNodeIdx_ = pi;
            splitterData.childIdx_ = i;
            splitterData.isRow_ = isRow;

            // Start with a placeholder size; compute() will set the real bounds
            IRMath::ivec2 dummySize(kSplitterThickness, kSplitterThickness);
            IREntity::EntityId se = IREntity::createEntity(
                IRComponents::C_Widget{IRComponents::WidgetKind::PANEL, dummySize},
                IRComponents::C_GuiPosition{IRMath::ivec2(0, 0)},
                IRComponents::C_GuiElement{},
                IRComponents::C_WidgetState{},
                splitterData,
                IRComponents::C_HitBox2DGui{dummySize}
            );
            node.splitterEntities_[i] = se;
        }
    }
    ls.topologySealed_ = true;
}

// -----------------------------------------------------------------------
// Splitter drag
// -----------------------------------------------------------------------

inline bool isDraggingSplitter() {
    return getLayout().dragSplitterParent_ >= 0;
}

inline void beginSplitterDrag(int parentNodeIdx, int childIdx, IRMath::ivec2 mousePosGui) {
    auto &ls = getLayout();
    auto &node = ls.nodes_[parentNodeIdx];
    if (childIdx < 0 || childIdx + 1 >= static_cast<int>(node.children_.size()))
        return;

    ls.dragSplitterParent_ = parentNodeIdx;
    ls.dragSplitterChildIdx_ = childIdx;
    ls.dragStartMouse_ = mousePosGui;

    const bool isRow = (node.type_ == LayoutNode::Type::ROW);
    const int axis = isRow ? 0 : 1;
    ls.dragStartSizeA_ = ls.nodes_[node.children_[childIdx]].size_[axis];
    ls.dragStartSizeB_ = ls.nodes_[node.children_[childIdx + 1]].size_[axis];
}

inline void updateSplitterDrag(IRMath::ivec2 mousePosGui) {
    if (!isDraggingSplitter())
        return;

    auto &ls = getLayout();
    auto &parentNode = ls.nodes_[ls.dragSplitterParent_];
    const bool isRow = (parentNode.type_ == LayoutNode::Type::ROW);
    const int axis = isRow ? 0 : 1;

    const int delta = mousePosGui[axis] - ls.dragStartMouse_[axis];

    auto &childA = ls.nodes_[parentNode.children_[ls.dragSplitterChildIdx_]];
    auto &childB = ls.nodes_[parentNode.children_[ls.dragSplitterChildIdx_ + 1]];

    const int totalPx = ls.dragStartSizeA_ + ls.dragStartSizeB_;
    const int newA = IRMath::clamp(
        ls.dragStartSizeA_ + delta,
        childA.spec_.minPx_,
        totalPx - childB.spec_.minPx_
    );
    const int newB = IRMath::clamp(totalPx - newA, childB.spec_.minPx_, childB.spec_.maxPx_);

    // Convert to FIXED_PX so the result is stable across frames
    childA.spec_.mode_ = SizeMode::FIXED_PX;
    childA.spec_.value_ = static_cast<float>(newA);
    childB.spec_.mode_ = SizeMode::FIXED_PX;
    childB.spec_.value_ = static_cast<float>(newB);
}

inline void endSplitterDrag() {
    auto &ls = getLayout();
    ls.dragSplitterParent_ = -1;
    ls.dragSplitterChildIdx_ = -1;
}

// -----------------------------------------------------------------------
// Panel drag-to-dock
// -----------------------------------------------------------------------

inline bool isDraggingPanel() {
    return getLayout().draggedPanelEntity_ != IREntity::kNullEntity;
}

inline IREntity::EntityId getDraggedPanelEntity() {
    return getLayout().draggedPanelEntity_;
}
inline int getDraggedPanelNodeIdx() {
    return getLayout().draggedPanelNodeIdx_;
}
inline IRMath::ivec2 getDragCurrentPos() {
    return getLayout().dragCurrentPos_;
}

inline void beginPanelDrag(
    int nodeIdx, IREntity::EntityId entity, IRMath::ivec2 mousePos, IRMath::ivec2 panelPos
) {
    auto &ls = getLayout();
    ls.draggedPanelNodeIdx_ = nodeIdx;
    ls.draggedPanelEntity_ = entity;
    ls.dragOffset_ = mousePos - panelPos;
    ls.dragCurrentPos_ = panelPos;
}

inline void updatePanelDrag(IRMath::ivec2 mousePos) {
    auto &ls = getLayout();
    ls.dragCurrentPos_ = mousePos - ls.dragOffset_;
}

// Returns the node index of the leaf whose center the mouse is nearest
// to, excluding the dragged panel's own node. Returns -1 if the mouse
// is not over any leaf zone.
inline int checkDropTarget(IRMath::ivec2 mousePos) {
    auto &ls = getLayout();
    int best = -1;
    int bestDist2 = kDockTargetSize * kDockTargetSize;
    for (int i = 0; i < static_cast<int>(ls.nodes_.size()); ++i) {
        if (i == ls.draggedPanelNodeIdx_)
            continue;
        const auto &n = ls.nodes_[i];
        if (n.type_ != LayoutNode::Type::LEAF)
            continue;
        const IRMath::ivec2 center = n.pos_ + n.size_ / 2;
        const int dx = mousePos.x - center.x;
        const int dy = mousePos.y - center.y;
        const int d2 = dx * dx + dy * dy;
        if (d2 < bestDist2) {
            bestDist2 = d2;
            best = i;
        }
    }
    return best;
}

// Swap the widget entities of two LEAF nodes, updating their C_LayoutLeaf
// components.
// Private to endPanelDrag; call endPanelDrag instead to ensure drag state is cleared.
inline void endPanelDragImpl(int fromNodeIdx, int toNodeIdx) {
    if (fromNodeIdx < 0 || toNodeIdx < 0)
        return;
    auto &ls = getLayout();
    auto &from = ls.nodes_[fromNodeIdx];
    auto &to = ls.nodes_[toNodeIdx];
    if (from.type_ != LayoutNode::Type::LEAF || to.type_ != LayoutNode::Type::LEAF)
        return;

    std::swap(from.widgetEntity_, to.widgetEntity_);

    // Re-stamp C_LayoutLeaf on both entities so the compute tick binds correctly
    if (from.widgetEntity_ != IREntity::kNullEntity)
        IREntity::setComponent(from.widgetEntity_, IRComponents::C_LayoutLeaf{fromNodeIdx});
    if (to.widgetEntity_ != IREntity::kNullEntity)
        IREntity::setComponent(to.widgetEntity_, IRComponents::C_LayoutLeaf{toNodeIdx});
    ++ls.revision_;
}

inline void cancelPanelDrag() {
    auto &ls = getLayout();
    ls.draggedPanelEntity_ = IREntity::kNullEntity;
    ls.draggedPanelNodeIdx_ = -1;
}

inline void endPanelDrag(IRMath::ivec2 mousePos) {
    const int dropTarget = checkDropTarget(mousePos);
    if (dropTarget >= 0) {
        endPanelDragImpl(getLayout().draggedPanelNodeIdx_, dropTarget);
    }
    cancelPanelDrag();
}

// -----------------------------------------------------------------------
// Input helpers (shared by widget input systems)
// -----------------------------------------------------------------------

// Returns current mouse position in GUI trixel space. Call once per frame
// in beginTick() and cache on System<N>.
// The "gui" canvas size in trixels, or zero with no render manager or no such
// canvas (headless).
inline IRMath::ivec2 guiCanvasSize() {
    if (IRRender::g_renderManager == nullptr) {
        return IRMath::ivec2(0);
    }
    const IREntity::EntityId guiCanvas = IRRender::getCanvas("gui");
    if (guiCanvas == IREntity::kNullEntity) {
        return IRMath::ivec2(0);
    }
    return IREntity::getComponent<IRComponents::C_TriangleCanvasTextures>(guiCanvas).size_;
}

inline IRMath::vec2 mousePositionInGuiTrixels() {
    const IREntity::EntityId guiCanvas = IRRender::getCanvas("gui");
    const auto &canvasTextures =
        IREntity::getComponent<IRComponents::C_TriangleCanvasTextures>(guiCanvas);
    const auto &framebuffer =
        IREntity::getComponent<IRComponents::C_TrixelCanvasFramebuffer>("mainFramebuffer");
    const IRMath::vec2 fbRes = IRMath::vec2(framebuffer.getResolutionPlusBuffer());
    const IRMath::vec2 guiSize = IRMath::vec2(canvasTextures.size_);
    return IRRender::getMousePositionMainFramebuffer() / fbRes * guiSize;
}

// -----------------------------------------------------------------------
// Serialization — stores each leaf's stable `id_` → current pixel size.
// Format:
// {"v":1,"leafSizes":[{"id":"left","w":240,"h":600},...],"splitters":[{"parent":0,"i":0,"a":240,"b":600},...]}
// -----------------------------------------------------------------------

inline std::string serialize() {
    const auto &ls = getLayout();
    std::string out;
    out.reserve(256);
    out += "{\"v\":1,\"leafSizes\":[";
    bool firstLeaf = true;
    for (const auto &n : ls.nodes_) {
        if (n.type_ != LayoutNode::Type::LEAF || n.id_.empty())
            continue;
        if (!firstLeaf)
            out += ',';
        firstLeaf = false;
        out += "{\"id\":\"";
        out += n.id_;
        out += "\",\"w\":";
        out += std::to_string(n.size_.x);
        out += ",\"h\":";
        out += std::to_string(n.size_.y);
        out += '}';
    }
    out += "],\"splitters\":[";
    bool firstSpl = true;
    for (int pi = 0; pi < static_cast<int>(ls.nodes_.size()); ++pi) {
        const auto &pn = ls.nodes_[pi];
        if (pn.type_ == LayoutNode::Type::LEAF)
            continue;
        const bool isRow = (pn.type_ == LayoutNode::Type::ROW);
        const int axis = isRow ? 0 : 1;
        for (int i = 0; i + 1 < static_cast<int>(pn.children_.size()); ++i) {
            const auto &a = ls.nodes_[pn.children_[i]];
            const auto &b = ls.nodes_[pn.children_[i + 1]];
            if (!firstSpl)
                out += ',';
            firstSpl = false;
            out += "{\"p\":";
            out += std::to_string(pi);
            out += ",\"i\":";
            out += std::to_string(i);
            out += ",\"a\":";
            out += std::to_string(a.size_[axis]);
            out += ",\"b\":";
            out += std::to_string(b.size_[axis]);
            out += '}';
        }
    }
    out += "]}";
    return out;
}

// Minimal parser for the above format. Restores splitter sizes (not leaf
// IDs since those are always driven by compute). Returns true on success.
inline bool deserialize(std::string_view json) {
    struct Allocation {
        int parent_;
        int child_;
        int sizeA_;
        int sizeB_;
    };

    auto skipSpace = [&json](std::size_t &cursor) {
        while (cursor < json.size() && std::isspace(static_cast<unsigned char>(json[cursor]))) {
            ++cursor;
        }
    };
    auto consume = [&json, &skipSpace](std::size_t &cursor, std::string_view token) {
        skipSpace(cursor);
        if (json.substr(cursor, token.size()) != token) {
            return false;
        }
        cursor += token.size();
        return true;
    };
    auto readUnsigned = [&json, &skipSpace](std::size_t &cursor, int &value) {
        skipSpace(cursor);
        if (cursor >= json.size() || !std::isdigit(static_cast<unsigned char>(json[cursor]))) {
            return false;
        }
        long long parsed = 0;
        while (cursor < json.size() && std::isdigit(static_cast<unsigned char>(json[cursor]))) {
            parsed = parsed * 10 + (json[cursor] - '0');
            if (parsed > std::numeric_limits<int>::max()) {
                return false;
            }
            ++cursor;
        }
        value = static_cast<int>(parsed);
        return true;
    };

    const std::size_t version = json.find("\"v\":1,");
    const std::size_t splitters = json.find("\"splitters\"");
    if (version == std::string_view::npos || splitters == std::string_view::npos) {
        return false;
    }

    auto &ls = getLayout();
    std::vector<Allocation> parsed;
    std::size_t expectedCount = 0;
    for (const auto &node : ls.nodes_) {
        if (isContainerNode(node) && !node.children_.empty()) {
            expectedCount += node.children_.size() - 1;
        }
    }
    parsed.reserve(expectedCount);

    std::size_t cursor = splitters + std::string_view("\"splitters\"").size();
    if (!consume(cursor, ":[")) {
        return false;
    }
    while (true) {
        skipSpace(cursor);
        if (cursor < json.size() && json[cursor] == ']') {
            ++cursor;
            break;
        }
        if (!parsed.empty() && !consume(cursor, ",")) {
            return false;
        }
        Allocation value{};
        if (!consume(cursor, "{\"p\":") || !readUnsigned(cursor, value.parent_) ||
            !consume(cursor, ",\"i\":")) {
            return false;
        }
        if (!readUnsigned(cursor, value.child_) || !consume(cursor, ",\"a\":") ||
            !readUnsigned(cursor, value.sizeA_) || value.sizeA_ <= 0 ||
            !consume(cursor, ",\"b\":") || !readUnsigned(cursor, value.sizeB_) ||
            value.sizeB_ <= 0 || !consume(cursor, "}")) {
            return false;
        }
        parsed.push_back(value);
    }
    if (parsed.size() != expectedCount || !consume(cursor, "}")) {
        return false;
    }
    skipSpace(cursor);
    if (cursor != json.size()) {
        return false;
    }

    std::size_t record = 0;
    for (int parent = 0; parent < static_cast<int>(ls.nodes_.size()); ++parent) {
        const auto &node = ls.nodes_[parent];
        if (!isContainerNode(node)) {
            continue;
        }
        for (int child = 0; child + 1 < static_cast<int>(node.children_.size()); ++child) {
            const auto &allocation = parsed[record++];
            if (allocation.parent_ != parent || allocation.child_ != child) {
                return false;
            }
        }
    }

    for (const auto &allocation : parsed) {
        auto &parent = ls.nodes_[allocation.parent_];
        auto &childA = ls.nodes_[parent.children_[allocation.child_]];
        auto &childB = ls.nodes_[parent.children_[allocation.child_ + 1]];
        childA.spec_.mode_ = SizeMode::FIXED_PX;
        childA.spec_.value_ = static_cast<float>(allocation.sizeA_);
        childB.spec_.mode_ = SizeMode::FIXED_PX;
        childB.spec_.value_ = static_cast<float>(allocation.sizeB_);
    }
    return true;
}

} // namespace IRPrefab::Layout

#endif /* IRREDEN_LAYOUT_H */
