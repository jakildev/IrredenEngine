#ifndef IR_PER_AXIS_CELL_DISPATCH
#define IR_PER_AXIS_CELL_DISPATCH

// Per-axis indirect regions append compute arguments after the draw command.
// The finalizer and occupied-cell consumers share this word offset and tile size.
const uint kDispatchArgsBaseUint = 8u;
const uint kPerAxisCellComputeTile = 256u;

// The finalizer folds large lists into multiple workgroup rows.
uint perAxisCellInvocationIndex(uint groupX, uint groupY, uint groupsX, uint localIndex) {
    return (groupX + groupY * groupsX) * kPerAxisCellComputeTile + localIndex;
}

ivec2 perAxisCellPixel(uint linearCell, int width) {
    return ivec2(int(linearCell) % width, int(linearCell) / width);
}

#endif
