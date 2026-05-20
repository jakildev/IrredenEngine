#ifndef COMPONENT_LAYOUT_LEAF_H
#define COMPONENT_LAYOUT_LEAF_H

namespace IRComponents {

// Tag that marks a widget entity as a leaf in the layout tree. The
// layout compute system reads this to locate the computed position and
// size for the node and writes them into C_GuiPosition / C_Widget.
struct C_LayoutLeaf {
    int nodeIdx_ = -1;
};

} // namespace IRComponents

#endif /* COMPONENT_LAYOUT_LEAF_H */
