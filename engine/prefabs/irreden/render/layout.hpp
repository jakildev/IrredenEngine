#ifndef IRREDEN_LAYOUT_H
#define IRREDEN_LAYOUT_H

#include <irreden/ir_entity.hpp>
#include <irreden/ir_math.hpp>
#include <irreden/ir_render.hpp>

#include <irreden/common/components/component_tags_all.hpp>
#include <irreden/render/components/component_gui_position.hpp>
#include <irreden/render/components/component_widget.hpp>
#include <irreden/render/components/component_layout_leaf.hpp>
#include <irreden/render/components/component_splitter.hpp>
#include <irreden/render/components/component_triangle_canvas_textures.hpp>
#include <irreden/render/components/component_trixel_framebuffer.hpp>
#include <irreden/input/components/component_hitbox_2d_gui.hpp>

#include <string>
#include <vector>
#include <string_view>

namespace IRPrefab::Layout {

// -----------------------------------------------------------------------
// Data model
// -----------------------------------------------------------------------

enum class SizeMode { FIXED_PX, FRACTION, CONTENT };

struct SizeSpec {
    SizeMode mode_ = SizeMode::FRACTION;
    float value_ = 1.0f; // pixels for FIXED_PX; [0,1] weight for FRACTION
    int minPx_ = 0;
    int maxPx_ = 32767;
};

struct LayoutNode {
    enum class Type { ROW, COLUMN, LEAF };

    Type type_ = Type::LEAF;
    SizeSpec spec_;
    std::string id_; // stable name used for serialization

    int parent_ = -1;
    std::vector<int> children_;

    // LEAF only: the widget entity this node controls
    IREntity::EntityId widgetEntity_ = IREntity::kNullEntity;
    // Splitter entity after children_[i] for i in [0, children_.size()-2]
    std::vector<IREntity::EntityId> splitterEntities_;

    // Filled by computeNode(); read by LAYOUT_COMPUTE tick
    IRMath::ivec2 pos_ = {};
    IRMath::ivec2 size_ = {};
};

static constexpr int kSplitterThickness = 6;
static constexpr int kDockTargetSize = 40; // square dock-preview quad

// -----------------------------------------------------------------------
// Global layout state (one dockspace per creation)
// -----------------------------------------------------------------------

struct LayoutState {
    std::vector<LayoutNode> nodes_;
    int root_ = -1;
    IRMath::ivec2 rootPos_ = {};
    IRMath::ivec2 rootSize_ = {};

    // Splitter drag
    int dragSplitterParent_ = -1;
    int dragSplitterChildIdx_ = -1;
    IRMath::ivec2 dragStartMouse_ = {};
    int dragStartSizeA_ = 0;
    int dragStartSizeB_ = 0;

    // Panel drag-to-dock
    IREntity::EntityId draggedPanelEntity_ = IREntity::kNullEntity;
    int draggedPanelNodeIdx_ = -1;
    IRMath::ivec2 dragOffset_ = {};
    IRMath::ivec2 dragCurrentPos_ = {};
};

inline LayoutState g_layout;
inline LayoutState &getLayout() {
    return g_layout;
}

// -----------------------------------------------------------------------
// Tree-building API (called once at init)
// -----------------------------------------------------------------------

// Returns new node index.
inline int addNode(LayoutNode node, int parentIdx) {
    int idx = static_cast<int>(g_layout.nodes_.size());
    node.parent_ = parentIdx;
    g_layout.nodes_.push_back(std::move(node));
    if (parentIdx >= 0) {
        g_layout.nodes_[parentIdx].children_.push_back(idx);
    }
    return idx;
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
    LayoutNode n;
    n.type_ = LayoutNode::Type::LEAF;
    n.spec_ = spec;
    n.id_ = std::move(id);
    n.widgetEntity_ = widget;
    // Stamp the C_LayoutLeaf component onto the widget entity
    int idx = static_cast<int>(g_layout.nodes_.size());
    n.parent_ = parentIdx;
    g_layout.nodes_.push_back(std::move(n));
    if (parentIdx >= 0) {
        g_layout.nodes_[parentIdx].children_.push_back(idx);
    }
    IREntity::setComponent(widget, IRComponents::C_LayoutLeaf{idx});
    return idx;
}

// Call after all nodes are added. Creates splitter entities between every
// pair of adjacent children in ROW/COLUMN nodes.
inline void buildSplitters();

// Set root bounds and root node index. Call before any compute().
inline void setRoot(int nodeIdx, IRMath::ivec2 pos, IRMath::ivec2 size) {
    g_layout.root_ = nodeIdx;
    g_layout.rootPos_ = pos;
    g_layout.rootSize_ = size;
}

// -----------------------------------------------------------------------
// Layout computation (called each frame by LAYOUT_COMPUTE beginTick)
// -----------------------------------------------------------------------

inline void computeNode(int nodeIdx, IRMath::ivec2 pos, IRMath::ivec2 size);

inline void compute() {
    if (g_layout.root_ < 0)
        return;
    computeNode(g_layout.root_, g_layout.rootPos_, g_layout.rootSize_);
}

inline const LayoutNode &getNode(int idx) {
    return g_layout.nodes_[idx];
}

inline void computeNode(int nodeIdx, IRMath::ivec2 pos, IRMath::ivec2 size) {
    auto &node = g_layout.nodes_[nodeIdx];
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
        auto &child = g_layout.nodes_[ci];
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
        auto &child = g_layout.nodes_[ci];

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
    for (int pi = 0; pi < static_cast<int>(g_layout.nodes_.size()); ++pi) {
        auto &node = g_layout.nodes_[pi];
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
}

// -----------------------------------------------------------------------
// Splitter drag
// -----------------------------------------------------------------------

inline bool isDraggingSplitter() {
    return g_layout.dragSplitterParent_ >= 0;
}

inline void beginSplitterDrag(int parentNodeIdx, int childIdx, IRMath::ivec2 mousePosGui) {
    auto &node = g_layout.nodes_[parentNodeIdx];
    if (childIdx < 0 || childIdx + 1 >= static_cast<int>(node.children_.size()))
        return;

    g_layout.dragSplitterParent_ = parentNodeIdx;
    g_layout.dragSplitterChildIdx_ = childIdx;
    g_layout.dragStartMouse_ = mousePosGui;

    const bool isRow = (node.type_ == LayoutNode::Type::ROW);
    const int axis = isRow ? 0 : 1;
    g_layout.dragStartSizeA_ = g_layout.nodes_[node.children_[childIdx]].size_[axis];
    g_layout.dragStartSizeB_ = g_layout.nodes_[node.children_[childIdx + 1]].size_[axis];
}

inline void updateSplitterDrag(IRMath::ivec2 mousePosGui) {
    if (!isDraggingSplitter())
        return;

    auto &parentNode = g_layout.nodes_[g_layout.dragSplitterParent_];
    const bool isRow = (parentNode.type_ == LayoutNode::Type::ROW);
    const int axis = isRow ? 0 : 1;

    const int delta = mousePosGui[axis] - g_layout.dragStartMouse_[axis];

    auto &childA = g_layout.nodes_[parentNode.children_[g_layout.dragSplitterChildIdx_]];
    auto &childB = g_layout.nodes_[parentNode.children_[g_layout.dragSplitterChildIdx_ + 1]];

    const int totalPx = g_layout.dragStartSizeA_ + g_layout.dragStartSizeB_;
    const int newA = IRMath::clamp(
        g_layout.dragStartSizeA_ + delta,
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
    g_layout.dragSplitterParent_ = -1;
    g_layout.dragSplitterChildIdx_ = -1;
}

// -----------------------------------------------------------------------
// Panel drag-to-dock
// -----------------------------------------------------------------------

inline bool isDraggingPanel() {
    return g_layout.draggedPanelEntity_ != IREntity::kNullEntity;
}

inline IREntity::EntityId getDraggedPanelEntity() {
    return g_layout.draggedPanelEntity_;
}
inline int getDraggedPanelNodeIdx() {
    return g_layout.draggedPanelNodeIdx_;
}
inline IRMath::ivec2 getDragCurrentPos() {
    return g_layout.dragCurrentPos_;
}

inline void beginPanelDrag(
    int nodeIdx, IREntity::EntityId entity, IRMath::ivec2 mousePos, IRMath::ivec2 panelPos
) {
    g_layout.draggedPanelNodeIdx_ = nodeIdx;
    g_layout.draggedPanelEntity_ = entity;
    g_layout.dragOffset_ = mousePos - panelPos;
    g_layout.dragCurrentPos_ = panelPos;
}

inline void updatePanelDrag(IRMath::ivec2 mousePos) {
    g_layout.dragCurrentPos_ = mousePos - g_layout.dragOffset_;
}

// Returns the node index of the leaf whose center the mouse is nearest
// to, excluding the dragged panel's own node. Returns -1 if the mouse
// is not over any leaf zone.
inline int checkDropTarget(IRMath::ivec2 mousePos) {
    int best = -1;
    int bestDist2 = kDockTargetSize * kDockTargetSize;
    for (int i = 0; i < static_cast<int>(g_layout.nodes_.size()); ++i) {
        if (i == g_layout.draggedPanelNodeIdx_)
            continue;
        const auto &n = g_layout.nodes_[i];
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
    auto &from = g_layout.nodes_[fromNodeIdx];
    auto &to = g_layout.nodes_[toNodeIdx];
    if (from.type_ != LayoutNode::Type::LEAF || to.type_ != LayoutNode::Type::LEAF)
        return;

    std::swap(from.widgetEntity_, to.widgetEntity_);

    // Re-stamp C_LayoutLeaf on both entities so the compute tick binds correctly
    if (from.widgetEntity_ != IREntity::kNullEntity)
        IREntity::setComponent(from.widgetEntity_, IRComponents::C_LayoutLeaf{fromNodeIdx});
    if (to.widgetEntity_ != IREntity::kNullEntity)
        IREntity::setComponent(to.widgetEntity_, IRComponents::C_LayoutLeaf{toNodeIdx});
}

inline void cancelPanelDrag() {
    g_layout.draggedPanelEntity_ = IREntity::kNullEntity;
    g_layout.draggedPanelNodeIdx_ = -1;
}

inline void endPanelDrag(IRMath::ivec2 mousePos) {
    const int dropTarget = checkDropTarget(mousePos);
    if (dropTarget >= 0) {
        endPanelDragImpl(g_layout.draggedPanelNodeIdx_, dropTarget);
    }
    cancelPanelDrag();
}

// -----------------------------------------------------------------------
// Input helpers (shared by widget input systems)
// -----------------------------------------------------------------------

// Returns current mouse position in GUI trixel space. Call once per frame
// in beginTick() and cache on System<N>.
inline IRMath::vec2 mousePositionInGuiTrixels() {
    const IREntity::EntityId guiCanvas = IRRender::getCanvas("gui");
    const auto &canvasTextures =
        IREntity::getComponent<IRComponents::C_TriangleCanvasTextures>(guiCanvas);
    const auto &framebuffer =
        IREntity::getComponent<IRComponents::C_TrixelCanvasFramebuffer>("mainFramebuffer");
    const IRMath::vec2 fbRes = IRMath::vec2(framebuffer.getResolutionPlusBuffer());
    const IRMath::vec2 guiSize = IRMath::vec2(canvasTextures.size_);
    return IRRender::getMousePositionOutputView() / fbRes * guiSize;
}

// -----------------------------------------------------------------------
// Serialization — stores each leaf's stable `id_` → current pixel size.
// Format:
// {"v":1,"leafSizes":[{"id":"left","w":240,"h":600},...],"splitters":[{"parent":0,"i":0,"a":240,"b":600},...]}
// -----------------------------------------------------------------------

inline std::string serialize() {
    std::string out;
    out.reserve(256);
    out += "{\"v\":1,\"leafSizes\":[";
    bool firstLeaf = true;
    for (const auto &n : g_layout.nodes_) {
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
    for (int pi = 0; pi < static_cast<int>(g_layout.nodes_.size()); ++pi) {
        const auto &pn = g_layout.nodes_[pi];
        if (pn.type_ == LayoutNode::Type::LEAF)
            continue;
        const bool isRow = (pn.type_ == LayoutNode::Type::ROW);
        const int axis = isRow ? 0 : 1;
        for (int i = 0; i + 1 < static_cast<int>(pn.children_.size()); ++i) {
            const auto &a = g_layout.nodes_[pn.children_[i]];
            const auto &b = g_layout.nodes_[pn.children_[i + 1]];
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
    auto findField = [&](std::string_view key, size_t from, size_t to) -> size_t {
        const std::string needle = std::string("\"") + std::string(key) + "\":";
        const size_t pos = json.find(needle, from);
        return (pos != std::string_view::npos && pos < to) ? pos + needle.size()
                                                           : std::string_view::npos;
    };
    auto readInt = [&](size_t pos) -> int {
        if (pos == std::string_view::npos)
            return -1;
        size_t end = pos;
        while (end < json.size() && (std::isdigit(json[end]) || json[end] == '-'))
            ++end;
        int val = 0;
        bool neg = false;
        size_t s = pos;
        if (s < end && json[s] == '-') {
            neg = true;
            ++s;
        }
        for (; s < end; ++s)
            val = val * 10 + (json[s] - '0');
        return neg ? -val : val;
    };

    // Read splitters array
    size_t splPos = json.find("\"splitters\":[");
    if (splPos == std::string_view::npos)
        return false;
    splPos += 13; // skip past "\"splitters\":["

    size_t cur = splPos;
    while (cur < json.size() && json[cur] != ']') {
        if (json[cur] != '{') {
            ++cur;
            continue;
        }
        size_t entryEnd = json.find('}', cur);
        if (entryEnd == std::string_view::npos)
            break;

        const int pi = readInt(findField("p", cur, entryEnd));
        const int idx = readInt(findField("i", cur, entryEnd));
        const int sizeA = readInt(findField("a", cur, entryEnd));
        const int sizeB = readInt(findField("b", cur, entryEnd));

        if (pi >= 0 && pi < static_cast<int>(g_layout.nodes_.size()) && idx >= 0 && sizeA > 0 &&
            sizeB > 0) {
            auto &pn = g_layout.nodes_[pi];
            if (idx + 1 < static_cast<int>(pn.children_.size())) {
                auto &childA = g_layout.nodes_[pn.children_[idx]];
                auto &childB = g_layout.nodes_[pn.children_[idx + 1]];
                childA.spec_.mode_ = SizeMode::FIXED_PX;
                childA.spec_.value_ = static_cast<float>(sizeA);
                childB.spec_.mode_ = SizeMode::FIXED_PX;
                childB.spec_.value_ = static_cast<float>(sizeB);
            }
        }
        cur = entryEnd + 1;
    }
    return true;
}

} // namespace IRPrefab::Layout

#endif /* IRREDEN_LAYOUT_H */
