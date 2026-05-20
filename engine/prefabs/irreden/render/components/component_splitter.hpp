#ifndef COMPONENT_SPLITTER_H
#define COMPONENT_SPLITTER_H

namespace IRComponents {

// Marks a widget entity as a draggable splitter between two adjacent
// children in a layout container. The splitter's C_GuiPosition and
// C_Widget::size_ are owned by the layout compute system — do not set
// them manually.
struct C_Splitter {
    int parentNodeIdx_ = -1; // layout tree index of the containing ROW/COLUMN
    int childIdx_ = 0;       // index within parent's children array just before this splitter
    bool isRow_ = true;      // true = vertical bar between row children; false = horizontal
};

} // namespace IRComponents

#endif /* COMPONENT_SPLITTER_H */
