#ifndef IRREDEN_LAYOUT_TYPES_H
#define IRREDEN_LAYOUT_TYPES_H

#include <irreden/ir_entity.hpp>
#include <irreden/ir_math.hpp>

#include <string>
#include <vector>

namespace IRPrefab::Layout {

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

} // namespace IRPrefab::Layout

#endif /* IRREDEN_LAYOUT_TYPES_H */
