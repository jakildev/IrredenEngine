#ifndef IR_PER_AXIS_CELL_DISPATCH
#define IR_PER_AXIS_CELL_DISPATCH

#include <metal_stdlib>
using namespace metal;

// Per-axis indirect regions append compute arguments after the draw command.
// The finalizer and occupied-cell consumers share this word offset and tile size.
constant uint kDispatchArgsBaseUint = 8u;
constant uint kPerAxisCellComputeTile = 256u;

// The finalizer folds large lists into multiple workgroup rows.
inline uint perAxisCellInvocationIndex(uint groupX, uint groupY, uint groupsX, uint localIndex) {
    return (groupX + groupY * groupsX) * kPerAxisCellComputeTile + localIndex;
}

inline int2 perAxisCellPixel(uint linearCell, int width) {
    return int2(int(linearCell) % width, int(linearCell) / width);
}

#endif
